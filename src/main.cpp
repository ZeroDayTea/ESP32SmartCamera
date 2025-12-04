/*
 * main.cpp
 *
 * coordinates the various modules of sd card, modem, camera, ml model,
 * uploader, logger and ota. simple state machine that captures frames,
 * classifies them, queues uploads, handles logs, performs ota checks
 * and manages deep sleep triggered by a pir sensor
 */

#include <Arduino.h>
#include <esp_sleep.h>

#include "config.h"
#include "sd_utils.h"
#include "camera_module.h"
#include "ml_model.h"
#include "modem_module.h"
#include "uploader.h"
#include "logger.h"
#include "ota_manager.h"

// define the rtc gpio used for pir wakeup
#ifndef PIR_PIN
#define PIR_PIN GPIO_NUM_32
#endif

// persist wake count across deep sleep
RTC_DATA_ATTR int wakeCount = 0;

// global module instances
SDUtils sd;
ModemModule modem;
CameraModule camera;
MLModel model;
Uploader *uploader = nullptr;
Logger *logger = nullptr;
OTAManager *ota = nullptr;

void setup() {
    Serial.begin(115200);
    wakeCount++;
    // initialise subsystems
    sd.begin();
    modem.begin(SerialAT);
    camera.begin();
    model.begin();
    uploader = new Uploader(sd, modem);
    logger = new Logger(sd);
    ota = new OTAManager(sd, modem, camera);
    // on first boot after ota, commit new image if healthy
    ota->healthCheck();
    // check for available updates
    if (ota->checkForUpdate()) {
        ota->performUpdate();
    }
}

void loop() {
    // capture a frame
    camera_fb_t *fb = camera.capture();
    if (fb) {
        // classify and queue if interesting
        if (model.isInteresting(fb)) {
            String filename = String("/") + modem.getCurrentDateTime() + ".jpg";
            sd.write(filename, fb->buf, fb->len);
            uploader->enqueue(filename);
        }
        camera.release(fb);
    }
    // process any pending uploads
    uploader->process();
    // send daily report every 24 hours
    static unsigned long lastReport = 0;
    if (millis() - lastReport > 86400000UL) {
        lastReport = millis();
        logger->sendDailyReport(modem);
    }
    // sleep until pir triggers; configure external wakeup on rtc gpio
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIR_PIN, 1);
    // enter deep sleep. device will reboot on wake
    esp_deep_sleep_start();
}
