/*
 * sd_utils.cpp
 *
 * wraps the sd card initialization and exposes simple helpers for opening, writing and
 * deleting files. all error conditions are logged using esp_log but
 * the functions themselves simply return false on failure
 */

#include "sd_utils.h"

#include <esp_log.h>

bool SDUtils::begin() {
    SPI.begin(SD_SCLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    if (!SD.begin(SD_CS_PIN)) {
        ESP_LOGE(TAG, "sd card mount failed");
        return false;
    }
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        ESP_LOGE(TAG, "no sd card attached");
        return false;
    }
    return true;
}

File SDUtils::open(const String &path, uint8_t mode) {
    return SD.open(path.c_str(), mode);
}

bool SDUtils::write(const String &path, const uint8_t *data, size_t len) {
    File file = SD.open(path.c_str(), FILE_WRITE);
    if (!file) {
        ESP_LOGE(TAG, "failed to open %s for write", path.c_str());
        return false;
    }
    size_t written = file.write(data, len);
    file.close();
    return written == len;
}

bool SDUtils::write(const String &path, const String &content) {
    return write(path, (const uint8_t *)content.c_str(), content.length());
}

bool SDUtils::append(const String &path, const String &content) {
    File file = SD.open(path.c_str(), FILE_APPEND);
    if (!file) {
        ESP_LOGE(TAG, "failed to open %s for append", path.c_str());
        return false;
    }
    size_t written = file.print(content);
    file.close();
    return written == content.length();
}

String SDUtils::info() {
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    uint64_t usedSpace = cardSize - (SD.totalBytes() / (1024 * 1024));
    String sdInfo = "SD:" + String(usedSpace) + "/" + String(cardSize) + "M";
    return sdInfo;
}

bool SDUtils::remove(const String &path) {
    return SD.remove(path.c_str());
}
