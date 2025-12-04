/*
 * camera_module.cpp
 *
 * this sets up the esp_camera using the provided pin definitions
 * frames are captured in jpeg format. minimal logs report success or failure.
 */

#include "camera_module.h"

#include <esp_log.h>

bool CameraModule::begin() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = CAM_Y2_PIN;
    config.pin_d1 = CAM_Y3_PIN;
    config.pin_d2 = CAM_Y4_PIN;
    config.pin_d3 = CAM_Y5_PIN;
    config.pin_d4 = CAM_Y6_PIN;
    config.pin_d5 = CAM_Y7_PIN;
    config.pin_d6 = CAM_Y8_PIN;
    config.pin_d7 = CAM_Y9_PIN;
    config.pin_xclk = CAM_XCLK_PIN;
    config.pin_pclk = CAM_PCLK_PIN;
    config.pin_vsync = CAM_VSYNC_PIN;
    config.pin_href = CAM_HREF_PIN;
    config.pin_sccb_sda = CAM_SIOD_PIN;
    config.pin_sccb_scl = CAM_SIOC_PIN;
    config.pin_pwdn = CAM_PWDN_PIN;
    config.pin_reset = CAM_RESET_PIN;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    if (psramFound()) {
        config.frame_size = FRAMESIZE_UXGA;
        config.jpeg_quality = 10;
        config.fb_count = 2;
    } else {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "camera init failed: 0x%x", err);
        return false;
    }
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        // flip image to correct orientation
        s->set_vflip(s, 1);
    }
    pinMode(CAM_IR_PIN, OUTPUT);
    digitalWrite(CAM_IR_PIN, HIGH);
    configured = true;
    return true;
}

camera_fb_t *CameraModule::capture() {
    if (!configured) return nullptr;
    return esp_camera_fb_get();
}

void CameraModule::release(camera_fb_t *fb) {
    if (fb) {
        esp_camera_fb_return(fb);
    }
}
