/*
 * uploader.h
 *
 * reads each file from the sd card and transmits it in 4096 byte chunks.
 * a crc32 is calculated per chunk and the chunk is staged to the modem efs using a temporary
 * name. receipt of an "OK" response from the modem is treated as
 * a real acknowledgement. an exponential backoff with a cap of five
 * minutes is applied between retries
 */

#ifndef UPLOADER_H_
#define UPLOADER_H_

#include <Arduino.h>
#include <queue>

#include "sd_utils.h"
#include "modem_module.h"

class Uploader {
public:
    Uploader(SDUtils &sd, ModemModule &modem);
    /** add a file path to the upload queue */
    void enqueue(const String &path);
    /** process queued files; stops on first failure to allow retry later */
    void process();
private:
    bool uploadFile(const String &path);
    uint32_t crc32(const uint8_t *data, size_t len);
    SDUtils &sd_;
    ModemModule &modem_;
    std::queue<String> queue_;
};

#endif // UPLOADER_H_
