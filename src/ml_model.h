/*
 * ml_model.h
 *
 * simple ml inference engine for the smart camera.
 * this version uses the tensorflow lite for microcontrollers library to
 * load a quantized model from model_data.h and run inference
 */

#ifndef ML_MODEL_H_
#define ML_MODEL_H_

#include <Arduino.h>
#include <esp_camera.h>

class MLModel {
public:
    /** initialize the model and any resources */
    bool begin();
    /** return true if the given frame is considered interesting */
    bool isInteresting(const camera_fb_t *fb);
};

#endif // ML_MODEL_H_
