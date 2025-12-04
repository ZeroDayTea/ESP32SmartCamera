/*
 * ota_manager.h
 *
 * handles over‑the‑air firmware updates. this class checks whether a new
 * version is available, downloads the manifest and firmware, verifies
 * the signature and hash, flashes the update to the alternate ota
 * partition and performs a health check on next boot. persistent
 * version and pending flags are stored in preferences
 */

#ifndef OTA_MANAGER_H_
#define OTA_MANAGER_H_

#include <Arduino.h>
#include <Preferences.h>

#include "sd_utils.h"
#include "modem_module.h"
#include "camera_module.h"

class OTAManager {
public:
    OTAManager(SDUtils &sd, ModemModule &modem, CameraModule &camera);
    /** return true if an update is available on the remote server */
    bool checkForUpdate();
    /** perform an update sequence: download, verify and flash */
    void performUpdate();
    /** on boot, validate the new image and commit or rollback */
    void healthCheck();
private:
    bool verifyManifest(const String &manifest, String &version, String &hash, String &sig);
    bool verifySignature(const String &hash, const String &sig);
    bool downloadFirmware(const String &version);
    bool flashFirmware(const String &version, const String &expectedHash);
    SDUtils &sd_;
    ModemModule &modem_;
    Preferences prefs_;
    CameraModule &camera_;
};

#endif // OTA_MANAGER_H_
