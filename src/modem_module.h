/*
 * modem_module.h
 *
 * simple interface for sending at commands, managing ftp connections,
 * interacting with the efs and performing http transfers
 */

#ifndef MODEM_MODULE_H_
#define MODEM_MODULE_H_

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>
#include <HardwareSerial.h>

#include <SD.h>

#include "config.h"

class ModemModule {
public:
    /** initialize the modem and bring up the cellular connection */
    bool begin(HardwareSerial &serialAt);
    /** send an at command and wait for a specific response */
    String sendAT(const String &cmd, const String &desiredResponse, unsigned long timeout);
    /** remove all files stored on the modem efs */
    bool clearEFS();
    /** start the ftp subsystem on the modem and login */
    bool startFTP();
    /** stop the ftp subsystem and logout */
    void stopFTP();
    /** upload a file located in efs to the ftp server */
    bool sendFileToFTP(const String &filename);
    /** write a binary buffer into the modem efs */
    bool writeFileToEFS(const String &filename, const uint8_t *buf, size_t len);
    /** write a string into the modem efs */
    bool writeStringToEFS(const String &filename, const String &content);
    /** delete a file from the modem efs */
    bool deleteFileFromEFS(const String &filename);
    /** retrieve the modem imei */
    String getIMEI();
    /** synchronize the rtc time via ntp and return the year */
    int syncTime();
    /** return the current date/time as yyddmmhhmmss string */
    String getCurrentDateTime();
    /** return the current date/time as dd/mm/yyyy hh:mm:ss */
    String getFormattedDateTime();
    /** perform an http get to a path relative to the configured host */
    bool httpGet(const String &path, String &out);
    /** download a binary resource in chunks and write to a file */
    bool httpGetBinary(const String &path, File &file, size_t chunkSize);
    /** initialize the http client for subsequent requests */
    bool initHTTP(const String &host, int port);
private:
    TinyGsm *modem = nullptr;
    TinyGsmClient *client = nullptr;
    HttpClient *http = nullptr;
};

#endif // MODEM_MODULE_H_
