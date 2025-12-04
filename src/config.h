/*
 * configuration constants for the smart camera project
 *
 * this header contains constants such as device
 * identifiers, pin definitions and ota endpoints
 *
 * pin definitions dependent on physical wiring
 */

#ifndef CONFIG_H_
#define CONFIG_H_

#include <Arduino.h>

// basic device identification
#define DEVICENAME "sanwildsmartcam00"
#define LOG_FILE_NAME "/log.txt"
#define FIRMWARE_FILE_NAME "/firmware.bin"
#define TAG "SmartCamera"

// ota server configuration
#define OTA_UPDATE_URL "http://13.246.234.82/"
#define OTA_UPDATE_PORT 80
// endpoints must be concatenated with the device name when building URLs
#define OTA_UPDATE_ENDPOINT String("") + DEVICENAME + "-firmware.bin"
#define OTA_VERSION_ENDPOINT String("") + DEVICENAME + "-version.txt"

// time conversion factors
#define uS_TO_S_FACTOR 1000000ULL

// gsm configuration
#define SerialAT Serial1
// #define DUMP_AT_COMMANDS
#define GSM_BAUD 9600
#define TINY_GSM_MODEM_SIM7600
#define GNSS_MODE 2
#define DPO_MODE true

#if !defined(TINY_GSM_RX_BUFFER)
#define TINY_GSM_RX_BUFFER 650
#endif

#define TINY_GSM_USE_GPRS true
extern const char apn[];
extern const char gprsUser[];
extern const char gprsPass[];

// pin assignments for the esp32s3 camera module
#define SD_MISO_PIN      40
#define SD_MOSI_PIN      38
#define SD_SCLK_PIN      39
#define SD_CS_PIN        47
#define PCIE_PWR_PIN     48
#define PCIE_TX_PIN      45
#define PCIE_RX_PIN      46
#define PCIE_LED_PIN     21
#define MIC_IIS_WS_PIN   42
#define MIC_IIS_SCK_PIN  41
#define MIC_IIS_DATA_PIN 2
#define CAM_PWDN_PIN     -1
#define CAM_RESET_PIN    -1
#define CAM_XCLK_PIN     14
#define CAM_SIOD_PIN     4
#define CAM_SIOC_PIN     5
#define CAM_Y9_PIN       15
#define CAM_Y8_PIN       16
#define CAM_Y7_PIN       17
#define CAM_Y6_PIN       12
#define CAM_Y5_PIN       10
#define CAM_Y4_PIN       8
#define CAM_Y3_PIN       9
#define CAM_Y2_PIN       11
#define CAM_VSYNC_PIN    6
#define CAM_HREF_PIN     7
#define CAM_PCLK_PIN     13
#define BUTTON_PIN       0
#define PWR_ON_PIN       1
#define SERIAL_RX_PIN    44
#define SERIAL_TX_PIN    43
#define BAT_VOLT_PIN     -1

// infrared filter control
#define CAM_IR_PIN       18

// pir sensor gpio (rtc capable) used to wake from deep sleep
#define PIR_PIN          32

#endif // CONFIG_H_
