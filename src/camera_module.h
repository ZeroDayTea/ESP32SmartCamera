/*
 * camera_module.cpp
 *
 * this sets up the esp_camera using the provided pin definitions
 * frames are captured in jpeg format. minimal logs report success or failure.
 */

#ifndef CAMERA_MODULE_H_
#define CAMERA_MODULE_H_

#include <Arduino.h>
#include <esp_camera.h>

#include "config.h"

class CameraModule {
public:
    /** initialise the camera hardware and return true on success */
    bool begin();
    /** capture a frame and return the buffer. returns null on error */
    camera_fb_t *capture();
    /** release a previously captured frame buffer */
    void release(camera_fb_t *fb);
private:
    bool configured = false;
};

#endif // CAMERA_MODULE_H_
