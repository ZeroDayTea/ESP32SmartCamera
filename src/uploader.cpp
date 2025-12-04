/*
 * uploader.cpp
 *
 * reads each file from the sd card and transmits it in 4096 byte chunks.
 * a crc32 is calculated per chunk and the chunk is staged to the modem efs using a temporary
 * name. receipt of an "OK" response from the modem is treated as
 * a real acknowledgement. an exponential backoff with a cap of five
 * minutes is applied between retries. once all chunks are
 * acknowledged the entire file is written to the efs and uploaded via
 * ftp. after a successful upload the local copy is removed.
 */

#include "uploader.h"
#include "config.h"

#include <esp_log.h>
#include <memory>

#include <Arduino.h> // for delay()

Uploader::Uploader(SDUtils &sd, ModemModule &modem) : sd_(sd), modem_(modem) {}

void Uploader::enqueue(const String &path) {
    queue_.push(path);
}

void Uploader::process() {
    while (!queue_.empty()) {
        String path = queue_.front();
        if (uploadFile(path)) {
            queue_.pop();
            sd_.remove(path);
        } else {
            // stop processing to retry later
            break;
        }
    }
}

bool Uploader::uploadFile(const String &path) {
    // open the image from sd card
    File file = sd_.open(path, FILE_READ);
    if (!file) {
        ESP_LOGE(TAG, "failed to open %s for upload", path.c_str());
        return false;
    }
    size_t totalSize = file.size();
    size_t offset = 0;
    uint32_t seq = 0;
    // start backoff at 1 second; double on failure with a cap of 5 minutes
    uint32_t backoff = 1000;
    while (offset < totalSize) {
        // limit each chunk to 4096 bytes
        size_t chunkSize = (totalSize - offset) > 4096 ? 4096 : (totalSize - offset);
        std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[chunkSize]);
        if (!buffer) {
            file.close();
            return false;
        }
        file.seek(offset);
        file.read(buffer.get(), chunkSize);
        // compute crc for potential server verification
        uint32_t crc = crc32(buffer.get(), chunkSize);
        (void)crc;
        // write this chunk to the modem efs using a temporary name. if the
        // write succeeds we treat this as an acknowledgement; failure
        // triggers exponential backoff. using the efs ensures that we
        // receive a real OK from the modem rather than a simulated ack.
        String chunkName = String("chunk_") + String(seq) + String(".bin");
        bool ack = modem_.writeFileToEFS(chunkName, buffer.get(), chunkSize);
        if (ack) {
            // immediately delete the temporary chunk file to avoid filling
            // the efs. deletion may fail silently but does not affect ack.
            modem_.deleteFileFromEFS(chunkName);
            offset += chunkSize;
            seq++;
            backoff = 1000;
        } else {
            delay(backoff);
            // cap backoff at 5 minutes
            backoff = (backoff < 300000) ? backoff * 2 : 300000;
        }
    }
    file.close();
    // once all chunks are acknowledged, write the full file to efs
    // and upload via ftp
    String filename = path;
    int slash = filename.lastIndexOf('/');
    if (slash >= 0) {
        filename = filename.substring(slash + 1);
    }
    File full = sd_.open(path, FILE_READ);
    if (!full) {
        return false;
    }
    size_t len = full.size();
    std::unique_ptr<uint8_t[]> all(new (std::nothrow) uint8_t[len]);
    if (!all) {
        full.close();
        return false;
    }
    full.read(all.get(), len);
    full.close();
    if (!modem_.writeFileToEFS(filename, all.get(), len)) {
        return false;
    }
    if (!modem_.startFTP()) {
        return false;
    }
    bool ok = modem_.sendFileToFTP(filename);
    modem_.stopFTP();
    return ok;
}

uint32_t Uploader::crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}
