/*
 * logger.h
 *
 * simple logging and daily report dispatch. this module writes
 * messages to the sd card and sends the accumulated log via the modem
 * once per day. the filename for the daily report includes a timestamp.
 */

#ifndef LOGGER_H_
#define LOGGER_H_

#include <Arduino.h>

#include "sd_utils.h"
#include "modem_module.h"

class Logger {
public:
    explicit Logger(SDUtils &sd) : sd_(sd) {}
    /** append a message to the log file */
    void log(const String &msg);
    /** send the accumulated log file via ftp and clear it */
    void sendDailyReport(ModemModule &modem);
private:
    SDUtils &sd_;
};

#endif // LOGGER_H_