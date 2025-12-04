/*
 * logger.cpp
 *
 * simple logging and daily report dispatch. this module writes
 * messages to the sd card and sends the accumulated log via the modem
 * once per day. the filename for the daily report includes a timestamp.
 */

#include "logger.h"
#include "config.h"

#include <esp_log.h>

void Logger::log(const String &msg) {
    sd_.append(LOG_FILE_NAME, msg + "\n");
}

void Logger::sendDailyReport(ModemModule &modem) {
    File f = sd_.open(LOG_FILE_NAME, FILE_READ);
    if (!f) {
        return;
    }
    String content;
    while (f.available()) {
        content += (char)f.read();
    }
    f.close();
    if (content.length() == 0) {
        return;
    }
    String reportName = String("report-") + modem.getCurrentDateTime() + ".txt";
    modem.writeStringToEFS(reportName, content);
    if (modem.startFTP()) {
        modem.sendFileToFTP(reportName);
        modem.stopFTP();
    }
    sd_.remove(LOG_FILE_NAME);
}
