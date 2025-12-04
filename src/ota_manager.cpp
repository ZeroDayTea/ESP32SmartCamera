/*
 * ota_manager.cpp
 *
 */

#include "ota_manager.h"
#include "config.h"

#include <esp_ota_ops.h>
#include <esp_log.h>
#include <mbedtls/sha256.h>
// include mbedtls headers for signature verification
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <vector>
#include <algorithm>
#include "camera_module.h"
#include "public_key.h"

OTAManager::OTAManager(SDUtils &sd, ModemModule &modem, CameraModule &camera)
    : sd_(sd), modem_(modem), camera_(camera) {
    prefs_.begin("ota", false);
}

bool OTAManager::checkForUpdate() {
    String remote;
    if (!modem_.httpGet(OTA_VERSION_ENDPOINT, remote)) {
        return false;
    }
    String current = prefs_.getString("version", "");
    remote.trim();
    if (remote.length() == 0) {
        return false;
    }
    // compare semantic version numbers. update only if remote is newer than current
    auto parse = [](const String &v) {
        std::vector<int> parts;
        int last = 0;
        int pos = 0;
        while ((pos = v.indexOf('.', last)) >= 0) {
            parts.push_back(v.substring(last, pos).toInt());
            last = pos + 1;
        }
        parts.push_back(v.substring(last).toInt());
        return parts;
    };
    std::vector<int> r = parse(remote);
    std::vector<int> c = parse(current);
    size_t n = std::max(r.size(), c.size());
    r.resize(n);
    c.resize(n);
    for (size_t i = 0; i < n; ++i) {
        if (r[i] > c[i]) return true;
        if (r[i] < c[i]) return false;
    }
    return false;
}

void OTAManager::performUpdate() {
    String manifest;
    if (!modem_.httpGet(String(DEVICENAME) + "-manifest.json", manifest)) {
        return;
    }
    String version, hash, sig;
    if (!verifyManifest(manifest, version, hash, sig)) {
        return;
    }
    if (!verifySignature(hash, sig)) {
        return;
    }
    if (!downloadFirmware(version)) {
        return;
    }
    flashFirmware(version, hash);
}

bool OTAManager::verifyManifest(const String &manifest, String &version, String &hash, String &sig) {
    int vi = manifest.indexOf("\"version\":\"");
    if (vi < 0) return false;
    int vs = manifest.indexOf("\"", vi + 11);
    version = manifest.substring(vi + 11, vs);
    int hi = manifest.indexOf("\"hash\":\"");
    int hs = manifest.indexOf("\"", hi + 8);
    hash = manifest.substring(hi + 8, hs);
    int si = manifest.indexOf("\"sig\":\"");
    int ss = manifest.indexOf("\"", si + 7);
    sig = manifest.substring(si + 7, ss);
    return version.length() > 0 && hash.length() > 0 && sig.length() > 0;
}

bool OTAManager::verifySignature(const String &hash, const String &sig) {
    // decode hex string to binary digest
    size_t hashLen = hash.length() / 2;
    std::vector<unsigned char> hashBin(hashLen);
    for (size_t i = 0; i < hashLen; ++i) {
        unsigned int byteVal;
        // parse two hex characters into a byte
        sscanf(hash.substring(i * 2, i * 2 + 2).c_str(), "%02x", &byteVal);
        hashBin[i] = static_cast<unsigned char>(byteVal);
    }
    // decode base64 signature into binary
    size_t expectedLen = (sig.length() * 3) / 4 + 3;
    std::vector<unsigned char> sigBin(expectedLen);
    size_t sigOutLen = 0;
    int b64ret = mbedtls_base64_decode(sigBin.data(), expectedLen, &sigOutLen,
                                       reinterpret_cast<const unsigned char *>(sig.c_str()), sig.length());
    if (b64ret != 0) {
        ESP_LOGE(TAG, "base64 decode failed: %d", b64ret);
        return false;
    }
    sigBin.resize(sigOutLen);
    // parse the public key from the embedded pem
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int ret = mbedtls_pk_parse_public_key(&pk, public_key_pem, public_key_pem_len);
    if (ret != 0) {
        ESP_LOGE(TAG, "failed to parse public key: %d", ret);
        mbedtls_pk_free(&pk);
        return false;
    }
    // verify the signature against the sha256 hash
    ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hashBin.data(), hashBin.size(),
                            sigBin.data(), sigBin.size());
    mbedtls_pk_free(&pk);
    return ret == 0;
}

bool OTAManager::downloadFirmware(const String &version) {
    String endpoint = OTA_UPDATE_ENDPOINT; // default endpoint includes device name
    File fw = sd_.open(FIRMWARE_FILE_NAME, FILE_WRITE);
    if (!fw) {
        ESP_LOGE(TAG, "failed to open firmware file for writing");
        return false;
    }
    modem_.httpGetBinary(endpoint, fw, 1024);
    fw.close();
    return true;
}

bool OTAManager::flashFirmware(const String &version, const String &expectedHash) {
    // compute sha256 of downloaded firmware
    File fw = sd_.open(FIRMWARE_FILE_NAME, FILE_READ);
    if (!fw) {
        return false;
    }
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0);
    uint8_t buf[1024];
    while (true) {
        size_t n = fw.read(buf, sizeof(buf));
        if (n == 0) break;
        mbedtls_sha256_update_ret(&ctx, buf, n);
    }
    uint8_t digest[32];
    mbedtls_sha256_finish_ret(&ctx, digest);
    mbedtls_sha256_free(&ctx);
    fw.seek(0);
    // convert computed digest into hex string
    char hexDigest[65] = {0};
    for (int i = 0; i < 32; i++) {
        sprintf(&hexDigest[i * 2], "%02x", digest[i]);
    }
    String digestHex = String(hexDigest);
    // compare with expected hash from manifest; return false if mismatch
    if (digestHex != expectedHash) {
        fw.close();
        ESP_LOGE(TAG, "firmware hash mismatch");
        return false;
    }
    // write to next ota partition
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (!next) {
        fw.close();
        return false;
    }
    esp_ota_handle_t handle;
    if (esp_ota_begin(next, OTA_SIZE_UNKNOWN, &handle) != ESP_OK) {
        fw.close();
        return false;
    }
    while (true) {
        size_t n = fw.read(buf, sizeof(buf));
        if (n == 0) break;
        if (esp_ota_write(handle, buf, n) != ESP_OK) {
            esp_ota_end(handle);
            fw.close();
            return false;
        }
    }
    fw.close();
    if (esp_ota_end(handle) != ESP_OK) {
        return false;
    }
    // mark pending and reboot
    prefs_.putString("pending", version);
    esp_ota_set_boot_partition(next);
    ESP.restart();
    return true;
}

void OTAManager::healthCheck() {
    // determine if the current image is pending verification. if no pending
    // update exists we simply return
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
        return;
    }
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }
    // attempt to re‑initialise the sd card, camera and modem
    // if any initialisation fails the app is marked invalid and rolled back
    bool ok = true;
    if (!sd_.begin()) {
        ok = false;
    }
    if (!camera_.begin()) {
        ok = false;
    }
    // ping modem by retrieving the imei; empty string indicates failure
    if (modem_.getIMEI().length() == 0) {
        ok = false;
    }
    if (ok) {
        esp_ota_mark_app_valid_cancel_rollback();
        // move pending version to current and clear pending flag
        if (prefs_.isKey("pending")) {
            prefs_.putString("version", prefs_.getString("pending"));
            prefs_.remove("pending");
        }
    } else {
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}
