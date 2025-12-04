/*
 * ml_model.cpp
 *
 * implementation of a simple ml inference engine for the smart camera.
 * this version uses the tensorflow lite for microcontrollers library to
 * load a quantized model from model_data.h and run inference
 */

#include "ml_model.h"
#include "model_data.h"

#include <TensorFlowLite_ESP32.h>
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"

using namespace tflite;

namespace {
// adjust to model size
constexpr int kTensorArenaSize = 35 * 1024;
static uint8_t tensor_arena[kTensorArenaSize];
}

static const tflite::Model *model_ = nullptr;
static tflite::MicroInterpreter *interpreter_ = nullptr;
static TfLiteTensor *input_ = nullptr;

bool MLModel::begin() {
    // load tflite model from compiled array
    model_ = tflite::GetModel(g_tflite_model);
    if (model_ == nullptr || model_->version() != TFLITE_SCHEMA_VERSION) {
        return false;
    }
    // all ops resolver registers all built‑in ops
    static AllOpsResolver resolver;
    // construct interpreter with model, resolver and tensor arena
    interpreter_ = new (std::nothrow) MicroInterpreter(model_, resolver, tensor_arena,
                                                       kTensorArenaSize, nullptr);
    if (!interpreter_) {
        return false;
    }
    // allocate tensors. failure indicates insufficient arena size.
    TfLiteStatus allocStatus = interpreter_->AllocateTensors();
    if (allocStatus != kTfLiteOk) {
        return false;
    }
    input_ = interpreter_->input(0);
    return true;
}

bool MLModel::isInteresting(const camera_fb_t *fb) {
    if (!interpreter_ || !input_) {
        return false;
    }
    // should decode fb->buf into the expected input format
    memset(input_->data.uint8, 0, input_->bytes);
    // run inference
    if (interpreter_->Invoke() != kTfLiteOk) {
        return false;
    }
    // obtain first output tensor and apply threshold on first element
    TfLiteTensor *output = interpreter_->output(0);
    if (output->type == kTfLiteUInt8) {
        return output->data.uint8[0] > 128; // binary decision model
    } else if (output->type == kTfLiteFloat32) {
        return output->data.f[0] > 0.5f; // confidence float-based decision
    }
    return false;
}
