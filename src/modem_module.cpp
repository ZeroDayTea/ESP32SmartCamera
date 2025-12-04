/*
 * modem_module.cpp
 *
 * implements the modem_module interface. the modem is initialized
 * on the provided serial port, network registration is waited for and
 * ftp and http interactions are performed using tinygsm cmds
 */

#include "modem_module.h"
#include "secrets.h"

#include <esp_log.h>
#include <Preferences.h>

ModemModule::ModemModule() {}

bool ModemModule::begin(HardwareSerial &serialAt) {
    // power cycle the modem
    pinMode(PCIE_PWR_PIN, OUTPUT);
    digitalWrite(PCIE_PWR_PIN, HIGH);
    delay(300);
    digitalWrite(PCIE_PWR_PIN, LOW);
    delay(3000);
    // start serial and create modem/client/http instances
    serialAt.begin(115200, SERIAL_8N1, PCIE_RX_PIN, PCIE_TX_PIN);
    modem = new TinyGsm(serialAt);
    client = new TinyGsmClient(*modem);
    http = nullptr;
    // wait for modem restart
    while (!modem->init()) {
        ESP_LOGI(TAG, "Failed to restart modem, delaying 3s and retrying");
        delay(3000);
    }
    ESP_LOGI(TAG, "Initialized modem");
    // wait for network registration
    String result;
    while (true) {
        modem->sendAT("+CREG?");
        if (modem->waitResponse(5000, result) == 1) {
            if (result.indexOf("+CREG: 0,1") > 0 || result.indexOf("+CREG: 0,5") > 0) {
                break;
            }
        }
        delay(2000);
    }
    // select network mode (38 = auto/gsm/lte)
    modem->sendAT("+CNMP=38");
    modem->waitResponse(10000);
    return true;
}

String ModemModule::sendAT(const String &cmd, const String &desiredResponse, unsigned long timeout) {
    modem->sendAT(cmd);
    String result;
    unsigned long startTime = millis();
    bool ok = false;
    while (!ok && millis() - startTime < timeout) {
        String s = modem->stream.readStringUntil('\n');
        if (desiredResponse.length() == 0) {
            ok = true;
        } else {
            ok = s.indexOf(desiredResponse) >= 0;
        }
        result += s;
    }
    return result;
}

bool ModemModule::clearEFS() {
    // change to efs drive
    modem->sendAT("+FSCD=E:");
    if (modem->waitResponse(10000) != 1) {
        ESP_LOGI(TAG, "Failed to change directory to E:");
        return false;
    }
    // delete all files
    modem->sendAT("+FSDEL=*.*");
    if (modem->waitResponse(10000) != 1) {
        ESP_LOGI(TAG, "Failed to delete files from EFS");
        return false;
    }
    return true;
}

bool ModemModule::startFTP() {
    String response = sendAT("+CFTPSSTART", "+CFTPSSTART:", 10000);
    if (response.indexOf("ERROR") >= 0) {
        stopFTP();
        sendAT("+CFTPSSTART", "OK", 5000);
    }
    String loginCmd = String("+CFTPSLOGIN=\"") + FTP_SERVER + "\"," + String(FTP_PORT) + ",\"" + FTP_USER + "\",\"" + FTP_PASS + "\",0";
    response = sendAT(loginCmd, "+CFTPSLOGIN:", 20000);
    if (response.indexOf("CFTPSLOGIN: 0") >= 0) {
        return true;
    }
    ESP_LOGI(TAG, "Failed to login FTP");
    return false;
}

void ModemModule::stopFTP() {
    String response = sendAT("+CFTPSLOGOUT", "+CFTPSLOGOUT:", 2000);
    (void)response;
    sendAT("+CFTPSSTOP", "+CFTPSSTOP:", 2000);
}

bool ModemModule::sendFileToFTP(const String &filename) {
    String cmd = String("+CFTPSPUTFILE=\"/") + filename + "\",3";
    String response = sendAT(cmd, "+CFTPSPUTFILE:", 100000);
    return response.indexOf("+CFTPSPUTFILE: 0") >= 0;
}

bool ModemModule::writeFileToEFS(const String &filename, const uint8_t *buf, size_t len) {
    // wait for prompt (no direct way to wait for '>')
    modem->sendAT("+FSCD=E:");
    modem->waitResponse(2000);
    String cmd = String("+CFTRANRX=\"e:/") + filename + "\"," + String(len);
    modem->sendAT(cmd);
    // proceed to write bytes
    modem->stream.write(buf, len);
    modem->stream.flush();
    unsigned long start = millis();
    while (millis() - start < 25000) {
        if (modem->stream.available()) {
            String resp = modem->stream.readStringUntil('\n');
            if (resp.indexOf("OK") >= 0) {
                return true;
            }
        }
    }
    return false;
}

bool ModemModule::writeStringToEFS(const String &filename, const String &content) {
    return writeFileToEFS(filename, (const uint8_t *)content.c_str(), content.length());
}

String ModemModule::getIMEI() {
    modem->sendAT("+CGSN");
    String buf;
    if (modem->waitResponse(10000, buf) != 1) {
        return String("");
    }
    // response includes command echo so extract digits
    int start = 2;
    int end = start + 15;
    if (buf.length() >= end) {
        return buf.substring(start, end);
    }
    return String("");
}

int ModemModule::syncTime() {
    modem->sendAT("+CTZU=1");
    modem->waitResponse(10000);
    modem->sendAT("+CNTP=\"pool.ntp.org\",8");
    modem->waitResponse(10000);
    modem->sendAT("+CNTP");
    if (modem->waitResponse(10000) != 1) {
        return -1;
    }
    char buffer[128];
    String response;
    modem->sendAT("+CCLK?");
    if (modem->waitResponse(10000, response) != 1) {
        return -1;
    }
    int firstQuote = response.indexOf('"');
    int lastQuote = response.lastIndexOf('"');
    if (firstQuote < 0 || lastQuote <= firstQuote) {
        return -1;
    }
    String timeStr = response.substring(firstQuote + 1, lastQuote);
    int year = timeStr.substring(0, 2).toInt();
    return year;
}

String ModemModule::getCurrentDateTime() {
    modem->sendAT("+CCLK?");
    String time;
    if (modem->waitResponse(10000, time) != 1) {
        return String("");
    }
    int start = time.indexOf('"') + 1;
    int end = time.lastIndexOf('"');
    if (start <= 0 || end <= start) {
        return String("");
    }
    String t = time.substring(start, end);
    int year = t.substring(0, 2).toInt();
    int month = t.substring(3, 5).toInt();
    int day = t.substring(6, 8).toInt();
    int hour = t.substring(9, 11).toInt();
    int minute = t.substring(12, 14).toInt();
    int second = t.substring(15, 17).toInt();
    char result[20];
    snprintf(result, sizeof(result), "%02d%02d20%02d%02d%02d%02d", day, month, year, hour, minute, second);
    return String(result);
}

String ModemModule::getFormattedDateTime() {
    modem->sendAT("+CCLK?");
    String time;
    if (modem->waitResponse(10000, time) != 1) {
        return String("");
    }
    int start = time.indexOf('"') + 1;
    int end = time.indexOf('"', start);
    if (start <= 0 || end <= start) {
        return String("");
    }
    String t = time.substring(start, end);
    int year = t.substring(0, 2).toInt();
    int month = t.substring(3, 5).toInt();
    int day = t.substring(6, 8).toInt();
    int hour = t.substring(9, 11).toInt();
    int minute = t.substring(12, 14).toInt();
    int second = t.substring(15, 17).toInt();
    char result[20];
    snprintf(result, sizeof(result), "%02d/%02d/20%02d %02d:%02d:%02d", day, month, year, hour, minute, second);
    return String(result);
}

bool ModemModule::initHTTP(const String &host, int port) {
    if (http) {
        delete http;
        http = nullptr;
    }
    http = new HttpClient(*client, host.c_str(), port);
    return true;
}

bool ModemModule::httpGet(const String &path, String &out) {
    // use at commands to perform http GET
    sendAT("+HTTPINIT", "", 10000);
    sendAT(String("+HTTPPARA=\"URL\",\"") + OTA_UPDATE_URL + path + "\"", "", 10000);
    String resp = sendAT("+HTTPACTION=0", "+HTTPACTION: 0,200", 20000);
    // read response body
    String body = sendAT("+HTTPREAD=99", "+HTTPREAD: 0", 20000);
    // terminate http session
    sendAT("+HTTPTERM", "", 10000);
    // extract content between CRLF after header
    int idx = body.indexOf("\r\n");
    if (idx >= 0) {
        out = body.substring(idx + 2);
    } else {
        out = body;
    }
    return true;
}

bool ModemModule::httpGetBinary(const String &path, File &file, size_t chunkSize) {
    // download binary file from server in chunks and write to sd file
    sendAT("+HTTPINIT", "", 10000);
    sendAT(String("+HTTPPARA=\"URL\",\"") + OTA_UPDATE_URL + path + "\"", "", 10000);
    String response = sendAT("+HTTPACTION=0", "+HTTPACTION: 0,200", 20000);
    size_t offset = 0;
    while (true) {
        String readCmd = String("+HTTPREAD=") + offset + "," + chunkSize;
        modem->sendAT(readCmd);
        String body;
        unsigned long start = millis();
        bool ok = false;
        while (!ok && millis() - start < 10000) {
            String line = modem->stream.readStringUntil('\n');
            ok = line.indexOf("+HTTPREAD: 0") >= 0;
            body += line;
            body += '\n';
        }
        if (body.indexOf("ERROR") >= 0) {
            break;
        }
        int startIdx = body.indexOf('\r', body.indexOf("+HTTPREAD:")) + 2;
        int endIdx = body.length() - 15;
        if (startIdx < 0 || endIdx <= startIdx) {
            break;
        }
        String chunk = body.substring(startIdx, endIdx);
        file.print(chunk);
        offset += chunk.length();
        if (chunk.length() < chunkSize) {
            break;
        }
    }
    file.flush();
    sendAT("+HTTPTERM", "", 10000);
    return true;
}
