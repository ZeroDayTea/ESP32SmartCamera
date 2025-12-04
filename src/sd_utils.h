/*
 * sd_utils.h
 *
* wraps the sd card initialization and exposes simple helpers for opening, writing and
 * deleting files. all error conditions are logged using esp_log but
 * the functions themselves simply return false on failure
 */

#ifndef SD_UTILS_H_
#define SD_UTILS_H_

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

#include "config.h"

class SDUtils {
public:
    /** mount the sd card and prepare spi */
    bool begin();
    /** open a file on the sd card. mode follows arduino fs (e.g. FILE_READ) */
    File open(const String &path, uint8_t mode);
    /** write a binary buffer to a file on the sd card */
    bool write(const String &path, const uint8_t *data, size_t len);
    /** write a string to a file on the sd card */
    bool write(const String &path, const String &content);
    /** append a string to a file on the sd card */
    bool append(const String &path, const String &content);
    /** return a human readable summary of free/total space */
    String info();
    /** remove a file from the sd card */
    bool remove(const String &path);
private:
};

#endif // SD_UTILS_H_
