// #include <stdarg.h>
// #include <stdio.h>
#include <WiFi.h>
#include <ESP32_FTPClient.h>
#include <esp_camera.h>
#include "config.h"
#include "secrets.h"
#include <esp_sntp.h>
#include <esp_log.h>
#include <esp32-hal-log.h>
#include <HardwareSerial.h>
#include <StreamDebugger.h>
#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <Preferences.h>
#include <Update.h>

// globals
#ifdef DUMP_AT_COMMANDS
  StreamDebugger debugger(SerialAT, Serial);
#endif
#ifdef DUMP_AT_COMMANDS
  TinyGsm modem(debugger);
#else
  TinyGsm modem(SerialAT);
#endif

TinyGsmClient client(modem);
HttpClient http(client, OTA_UPDATE_URL, OTA_UPDATE_PORT);
File log_file;

String IMEI = "";
String GPSPosition = "";
String LogContent = "";
String newFirmwareVersion = "";
Preferences preferences;

// function prototypes
String getCurrentDateTime();
String getFormattedDateTime();
String getFormattedImageName();
String getFormattedReportName();
String getSDCardInfo();
void takePhoto();
void sendLogFile();
void initializeConnectionWifi();
void initializeCamera();
String convertToDMS(String coord, String direction);
void getGPSPosition();
void getIMEI();
int syncTime();
void initializeModem();
void initializeSDCard();
int sdCardLogOutput(const char *format, va_list args);
void clearEFS();
void stopFtp(void);
boolean initFtp(void);
int sendFileToFtp(String imageFileName);
boolean sendFileToEFS(String imageFileName, camera_fb_t * fb);
boolean sendPhoto(camera_fb_t * fb);

// datetime as string of numbers
String getCurrentDateTime() {
  String time;
  modem.sendAT("+CCLK?");
  if (modem.waitResponse(10000, time) != 1) {
    ESP_LOGI(TAG, "Failed to get time");
    return "";
  }
  time = time.substring(time.indexOf('"') + 1, time.lastIndexOf('"'));
  int year = time.substring(0, 2).toInt();
  int month = time.substring(3, 5).toInt();
  int day = time.substring(6, 8).toInt();
  int hour = time.substring(9, 11).toInt();
  int minute = time.substring(12, 14).toInt();
  int second = time.substring(15, 17).toInt();

  char result[20];
  snprintf(result, sizeof(result), "%02d%02d20%02d%02d%02d%02d", day, month, year, hour, minute, second);
  return String(result);
}

// datetime as human readable string
String getFormattedDateTime() {
  String time;
  modem.sendAT("+CCLK?");
  if (modem.waitResponse(10000, time) != 1) {
    ESP_LOGI(TAG, "Failed to get time");
    return "";
  }
  time = time.substring(time.indexOf("\"") + 1, time.indexOf("\""));
  int year = time.substring(0, 2).toInt();
  int month = time.substring(3, 5).toInt();
  int day = time.substring(6, 8).toInt();
  int hour = time.substring(9, 11).toInt();
  int minute = time.substring(12, 14).toInt();
  int second = time.substring(15, 17).toInt();

  char result[20];
  snprintf(result, sizeof(result), "%02d/%02d/20%02d %02d:%02d:%02d", day, month, year, hour, minute, second);
  return String(result);
}

// formatted filename for image upload
String getFormattedImageName() {
  return String(DEVICENAME) + "-" + getCurrentDateTime() + ".jpg";
}

// formatted filename for daily report upload
String getFormattedReportName() {
  String dateTime = getCurrentDateTime();
  return dateTime + "-" + String(DEVICENAME) + "-DailyReport.txt";
}

// return the SD card information for logging
String getSDCardInfo() {
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  uint64_t usedSpace = cardSize - (SD.totalBytes() / (1024 * 1024));
  String sdInfo = "SD:" + String(usedSpace) + "/" + String(cardSize) + "M";
  return sdInfo;
}

String sendATCommand(String command, String desiredResponse, unsigned long timeout) {
  modem.sendAT(command);
  String result;
  unsigned long startTime = millis();
  boolean ok = false;
  while (!ok && millis() - startTime < timeout) {
    String s = modem.stream.readStringUntil('\n');
    ok = s.indexOf(desiredResponse) >= 0;
    result += s;
  }
  return result;
}

// delete all files in sim7600 EFS to prevent clutter of images
void clearEFS() {
  ESP_LOGI(TAG, "Clearing EFS...");

  // directory for storing all images
  modem.sendAT("+FSCD=E:");
  if (modem.waitResponse(10000) != 1) {
      ESP_LOGI(TAG, "Failed to change directory to E:");
      return;
  }

  modem.sendAT("+FSLS");
  if (modem.waitResponse(5000) != 1) {
    ESP_LOGI(TAG, "Failed to check EFS file listing");
  }

  modem.sendAT("+FSDEL=*.*");
  if (modem.waitResponse(10000) != 1) {
    ESP_LOGI(TAG, "Failed to delete image files");
  } else {
    ESP_LOGI(TAG, "Successfully cleared EFS");
  }
}


// start FTP service on modem and login
boolean initFtp(void) {
  String response = sendATCommand("+CFTPSSTART", "+CFTPSSTART:", 10000);
  if (response.indexOf("ERROR") > 0) {
    ESP_LOGI(TAG, "Failed to start FTP service on modem");
    stopFtp();
    sendATCommand("+CFTPSSTART", "OK", 5000);
  } else {
    ESP_LOGI(TAG, "Started FTP service on modem");
  }

  String loginCommand = String("+CFTPSLOGIN=\"") + FTP_SERVER + "\"," + String(FTP_PORT) + ",\"" + FTP_USER + "\",\"" + FTP_PASS + "\",0";
  response = sendATCommand(loginCommand, "+CFTPSLOGIN:", 20000);
  if (response.indexOf("CFTPSLOGIN: 0") > 0) {
    ESP_LOGI(TAG, "Logged in FTP");
  } else {
    ESP_LOGI(TAG, "Failed to login FTP");
    return false;
  }

  // modem.sendAT("+CFTPTYPE=I");
  // // modem.waitResponse(2000, result);
  // if (modem.waitResponse(5000) != 1) {
  //   ESP_LOGI(TAG, "Failed to set FTP type");
  // }

  // modem.sendAT("+CFTPSCWD=\"/\"");
  // // modem.waitResponse(2000, result);
  // if (modem.waitResponse(5000) != 1) {
  //   ESP_LOGI(TAG, "Failed to set FTP folder");
  // }

  return true;
}

// logout and stop FTP service on modem
void stopFtp(void) {
  String response = sendATCommand("+CFTPSLOGOUT", "+CTFPSLOGOUT:", 2000);
  if (response.indexOf("+CFTPSLOGOUT: 0") >= 0) {
    ESP_LOGI(TAG, "Logged out FTP");
  } else {
    ESP_LOGI(TAG, "Failed to log out FTP");
  }

  response = sendATCommand("+CFTPSSTOP", "+CFTPSSTOP:", 2000);
  if (response.indexOf("+CFTPSSTOP: 0") >= 0) {
    ESP_LOGI(TAG, "Stopped FTP service on modem");
  } else {
    ESP_LOGI(TAG, "Failed to stop FTP service on modem");
  }
}

// send file to FTP server (must be logged in first)
int sendFileToFtp(String fileName) {
  String putCommand = String("+CFTPSPUTFILE=") + "\"/" + fileName + "\"," + "3";
  String response = sendATCommand(putCommand, "+CFTPSPUTFILE:", 100000);
  if (response.indexOf("+CFTPSPUTFILE: 0") >= 0) {
    ESP_LOGI(TAG, "Successfully ran FTP putfile");
    return 0;
  } else {
    ESP_LOGI(TAG, "Failed to run putfile and upload file to ftp");
    return -1;
  }
}

// copy camera data to modem EFS sd card
boolean sendFileToEFS(String imageFileName, camera_fb_t * fb) {
  uint8_t *fbBuf = fb->buf;
  size_t len = fb->len;

  modem.sendAT("+FSCD=E:");
  if (modem.waitResponse(20000) != 1) {
    ESP_LOGI(TAG, "Failed to switch EFS directory");
  }

  ESP_LOGI(TAG, "File length: %d", len);
  String uploadCommand = "+CFTRANRX=";
  uploadCommand = uploadCommand + "\"e:/" + imageFileName + "\"," + len;
  ESP_LOGI(TAG, "upload command: %s", uploadCommand.c_str());
  modem.sendAT(uploadCommand);
  if (modem.waitResponse(2000) != 0) { // TODO: fix waiting for the '>' start
    ESP_LOGI(TAG, "Failed to start file upload to EFS");
  }

  modem.stream.write(fbBuf, len);
  modem.stream.flush();
  // wait for the OK response
  unsigned long startTime = millis();
  while (millis() - startTime < 25000) { // timeout for file transfer to EFS
    if (modem.stream.available()) {
      String response = modem.stream.readStringUntil('\n');
      // ESP_LOGI(TAG, "Response: %s", response.c_str());
      if (response.indexOf("OK") != -1) {
        ESP_LOGI(TAG, "File successfully written to EFS");

        // manual check that file was written
        // modem.sendAT("+FSLS");
        // if (modem.waitResponse(20000) != 1) {
        //   ESP_LOGI(TAG, "Failed to check EFS directory");
        // }

        return true;
      }
    }
  }

  ESP_LOGI(TAG, "Failed to write file to EFS");
  return false;
}

boolean sendLogToEFS(String logFileName, String logFileContents) {
  size_t len = logFileContents.length();

  modem.sendAT("+FSCD=E:");
  if (modem.waitResponse(20000) != 1) {
    ESP_LOGI(TAG, "Failed to switch EFS directory");
  }

  ESP_LOGI(TAG, "File length: %d", len);
  String uploadCommand = "+CFTRANRX=";
  uploadCommand = uploadCommand + "\"e:/" + logFileName + "\"," + len;
  ESP_LOGI(TAG, "upload command: %s", uploadCommand.c_str());
  modem.sendAT(uploadCommand);
  if (modem.waitResponse(2000) != 0) { // TODO: fix waiting for the '>' start
    ESP_LOGI(TAG, "Failed to start file upload to EFS");
  }

  modem.stream.write(logFileContents.c_str(), len);
  modem.stream.flush();
  // wait for the OK response
  unsigned long startTime = millis();
  while (millis() - startTime < 25000) { // timeout for file transfer to EFS
    if (modem.stream.available()) {
      String response = modem.stream.readStringUntil('\n');
      // ESP_LOGI(TAG, "Response: %s", response.c_str());
      if (response.indexOf("OK") != -1) {
        ESP_LOGI(TAG, "File successfully written to EFS");

        // manual check that file was written
        // modem.sendAT("+FSLS");
        // if (modem.waitResponse(20000) != 1) {
        //   ESP_LOGI(TAG, "Failed to check EFS directory");
        // }

        return true;
      }
    }
  }

  ESP_LOGI(TAG, "Failed to write file to EFS");
  return false;
}

// copy file to modem and send it to FTP server
boolean sendPhoto(camera_fb_t * fb) {
  String imageFileName = getFormattedImageName();
  if (!sendFileToEFS(imageFileName, fb)){
    ESP_LOGI(TAG, "Error while sending file to EFS. Is SD card ok ?");
    return false;
  };
  if (!initFtp()) {
    ESP_LOGI(TAG, "Error while conecting to FTP");
    return false;
  };
  int ftpResult = -1;
  int retries = 3;
  while (ftpResult != 0 && retries >= 0) {
    ftpResult = sendFileToFtp(imageFileName);
    retries--;
    if(ftpResult != 0){
      ESP_LOGI(TAG, "Error sending file to FTP, retrying, number of retires left : %d", retries);
    }
  }
  stopFtp();
  if (ftpResult == 0){
    return true;
  } else {
    ESP_LOGI(TAG, "Cannot send file to FTP");
    return false;
  }
}

boolean sendLogFile(String logFileContents) {
  String logFileName = getFormattedImageName();
  if (!sendLogToEFS(logFileName, logFileContents)){
    ESP_LOGI(TAG, "Error while sending file to EFS. Is SD card ok ?");
    return false;
  };
  if (!initFtp()) {
    ESP_LOGI(TAG, "Error while conecting to FTP");
    return false;
  };
  int ftpResult = -1;
  int retries = 3;
  while (ftpResult != 0 && retries >= 0) {
    ftpResult = sendFileToFtp(logFileName);
    retries--;
    if(ftpResult != 0){
      ESP_LOGI(TAG, "Error sending file to FTP, retrying, number of retires left : %d", retries);
    }
  }
  stopFtp();
  if (ftpResult == 0){
    return true;
  } else {
    ESP_LOGI(TAG, "Cannot send file to FTP");
    return false;
  }
}

// take photo, process it, and send to server if needed
void takePhoto() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    ESP_LOGI(TAG, "Camera capture failed");
    return;
  }
  //   unsigned int totalPictures = preferences.getUInt("totalPictures", 0);
  //   totalPictures++;
  //   preferences.putUInt("totalPictures", totalPictures);

  //   ESP_LOGI(TAG, "Total Pictures: %d", totalPictures);

  // send image over WIFI
  // ftp.OpenConnection();
  // ftp.InitFile("Type I");
  // ftp.ChangeWorkDir("/");
  // String filename = getFormattedImageName();
  // ftp.NewFile(filename.c_str());
  // ftp.WriteData(fb->buf, fb->len);
  // ftp.CloseFile();
  // ftp.CloseConnection();

  // send image over 4G if interesting
  int chance = random(200);
  // ESP_LOGI(TAG, "random number generated: %d", chance);
  if(chance == 1) {
    boolean sendPhotoOk = false;
    for (int i=0; i<3; i++) {
      sendPhotoOk = sendPhoto(fb);
      if (sendPhotoOk) {
        break;
      }
    }

    if (!sendPhotoOk) {
      esp_camera_fb_return(fb);
      ESP_LOGI(TAG, "Time: %s", String(esp_timer_get_time()));
      ESP_LOGI(TAG, "Failed to upload photo successfully");
      return;
    } else {
      // return the frame buffer back to the driver for reuse
      esp_camera_fb_return(fb);

      ESP_LOGI(TAG, "Time: %s", String(esp_timer_get_time()));
      ESP_LOGI(TAG, "Photo taken and uploaded successfully");

      unsigned int sendTimes = preferences.getUInt("sendTimes", 0);
      sendTimes++;
      preferences.putUInt("sendTimes", sendTimes);

      ESP_LOGI(TAG, "Send Times: %d", sendTimes);
    }
  }
  esp_camera_fb_return(fb);
}

// send formatted logfile with sensor information
void sendLogFile() {
  String formattedDateTime = getFormattedDateTime();
  getGPSPosition(); // update GPS position data

  unsigned int sendTimes = preferences.getUInt("sendTimes", 0);
  // unsigned int totalPictures = preferences.getUInt("totalPictures", 0);

  LogContent = "IMEI:" + IMEI + "\n";
  LogContent += "CSQ:12\n";
  LogContent += "CamID:" + String(DEVICENAME) + "\n";
  LogContent += "Temp:9C\n";
  LogContent += "Date:" + formattedDateTime + "\n";
  LogContent += "Bat:100%\n";
  LogContent += getSDCardInfo() + "\n";
  LogContent += "Total:" + String("0") + "\n";
  LogContent += "Send:" + String(sendTimes) + "\n";
  LogContent += "GPS:" + GPSPosition + "\n";

  ESP_LOGI(TAG, "Log Content:\n%s", LogContent.c_str());

  // send logfile over WiFI
  // ftp.OpenConnection();
  // ftp.InitFile("Type I");
  // ftp.ChangeWorkDir("/");
  // String fileName = getFormattedReportName();
  // ftp.NewFile(fileName.c_str());
  // unsigned char *data = (unsigned char *)LogContent.c_str();
  // ftp.WriteData(data, LogContent.length());
  // ftp.CloseFile();
  // ftp.CloseConnection();

  // send logfile over 4G


  ESP_LOGI(TAG, "Time: %s", String(esp_timer_get_time()).c_str());
  ESP_LOGI(TAG, "Daily report generated and uploaded successfully");
}

// connect to WiFi
// void initializeConnectionWifi() {
//   ESP_LOGI(TAG, "Connecting to WiFi...");
//   WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
//   while (WiFi.status() != WL_CONNECTED) {
//     delay(1000);
//     ESP_LOGI(TAG, "Connecting to WiFi...");
//   }
//   ESP_LOGI(TAG, "Connected to WiFi");
// }

// initialize the camera
void initializeCamera() {
  ESP_LOGI(TAG, "Initializing camera...");

  // camera settings
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_Y2_PIN;
  config.pin_d1 = CAM_Y3_PIN;
  config.pin_d2 = CAM_Y4_PIN;
  config.pin_d3 = CAM_Y5_PIN;
  config.pin_d4 = CAM_Y6_PIN;
  config.pin_d5 = CAM_Y7_PIN;
  config.pin_d6 = CAM_Y8_PIN;
  config.pin_d7 = CAM_Y9_PIN;
  config.pin_xclk = CAM_XCLK_PIN;
  config.pin_pclk = CAM_PCLK_PIN;
  config.pin_vsync = CAM_VSYNC_PIN;
  config.pin_href = CAM_HREF_PIN;
  config.pin_sccb_sda = CAM_SIOD_PIN;
  config.pin_sccb_scl = CAM_SIOC_PIN;
  config.pin_pwdn = CAM_PWDN_PIN;
  config.pin_reset = CAM_RESET_PIN;
  config.xclk_freq_hz = 20000000; // EXPERIMENTAL: Set to 16MHz on ESP32-S2 or ESP32-S3 to enable EDMA mode
  config.pixel_format = PIXFORMAT_JPEG; // YUV422,GRAYSCALE,RGB565,JPEG

  if (psramFound()) {
    ESP_LOGI(TAG, "Using Framesize UXGA");
    config.frame_size = FRAMESIZE_UXGA; // change for better resolution. do not go above QVGA size when not jpeg
    config.jpeg_quality = 10; // 0-63 with lower number is better quality
    config.fb_count = 2;
  } else {
    ESP_LOGI(TAG, "Using Framesize SVGA");
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

#if defined(CAMERA_MODEL_ESP_EYE)
    pinMode(13, INPUT_PULLUP);
    pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    ESP_LOGI(TAG, "Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  // daytime settings
  s->set_vflip(s, 1);

  // nighttime settings
  // s->set_vflip(s, 1);

  // TODO: on during the day, off during the night
  ESP_LOGI(TAG, "IR Filter On");
  pinMode(CAM_IR_PIN, OUTPUT);
  digitalWrite(CAM_IR_PIN, HIGH);

  ESP_LOGI(TAG, "Camera initialized");
}

// convert ddmm.mmmmmm GPS position to DMS
String convertToDMS(String coord, String direction) {
  // latitude is in ddmm.mmmmmm format while longitude is in dddmm.mmmmmm format while
  int degLength = (coord.length() > 11) ? 3 : 2;
  int degrees = coord.substring(0, degLength).toInt();
  float minutes = coord.substring(degLength).toFloat();

  int wholeMinutes = int(minutes);
  float fractionalMinutes = minutes - wholeMinutes;
  float seconds = fractionalMinutes * 60;

  return direction + String(degrees) + "*" + String(wholeMinutes) + "'" + String(seconds, 2) + "\"";
}

// enables GNSS and gathers position data
void getGPSPosition() {
    ESP_LOGI(TAG, "Enabling GPS/GNSS/GLONASS and gathering position data");

    modem.sendAT("+CGPS=1"); // enable GPS
    if (modem.waitResponse(10000) != 1) {
        ESP_LOGI(TAG, "Failed to enable GPS");
        return;
    }

    String gps_latitude = "";
    String gps_longitude = "";
    while (true) {
        ESP_LOGI(TAG, "Requesting GPS info");

        modem.sendAT("+CGNSSINFO");
        String response = modem.stream.readStringUntil('\n');
        ESP_LOGI(TAG, "%s", response.c_str());

        if (response.indexOf(",N,") != -1 || response.indexOf(",S,") != -1) {
            int latStart = response.indexOf(",", 22) + 1;
            int latEnd = response.indexOf(",", latStart);
            gps_latitude = response.substring(latStart, latEnd);

            int latDirEnd = response.indexOf(",", latEnd + 1);
            String latDir = response.substring(latEnd + 1, latDirEnd);

            int lonStart = response.indexOf(",", latDirEnd) + 1;
            int lonEnd = response.indexOf(",", lonStart);
            gps_longitude = response.substring(lonStart, lonEnd);

            int lonDirEnd = response.indexOf(",", lonEnd + 1);
            String lonDir = response.substring(lonEnd + 1, lonDirEnd);

            String latitudeDMS = convertToDMS(gps_latitude, latDir);
            String longitudeDMS = convertToDMS(gps_longitude, lonDir);

            GPSPosition = latitudeDMS + " " + longitudeDMS;
            ESP_LOGI(TAG, "GPS Position: %s", GPSPosition.c_str());
            break;
        } else {
            ESP_LOGI(TAG, "Couldn't get GPS info, retrying in 10s.");
            delay(15000);
        }
    }
}

// get IMEI number from GSM module
void getIMEI() {
  ESP_LOGI(TAG, "Updating IMEI");
  modem.sendAT("+CGSN");
  String IMEI_buf = "";
  if (modem.waitResponse(10000, IMEI_buf) != 1) {
    ESP_LOGI(TAG, "Failed to get IMEI");
    return;
  }
  IMEI = IMEI_buf.substring(2, 17); // parse AT command response
  ESP_LOGI(TAG, "IMEI: %s", IMEI.c_str());
}

// ensure time is set
int syncTime() {
  ESP_LOGI(TAG, "Syncing Time...");

  modem.sendAT("+CTZU=1");
  if (modem.waitResponse(10000) != 1) {
    ESP_LOGI(TAG, "Failed to enable automatic time update");
  }

  modem.sendAT("+CNTP=\"pool.ntp.org\",8");
  if (modem.waitResponse(10000) != 1) {
    ESP_LOGI(TAG, "Failed to set NTP server");
    return -1;
  }
  modem.sendAT("+CNTP");
  if (modem.waitResponse(10000) != 1) {
    ESP_LOGI(TAG, "Failed to sync time");
    return -1;
  }

  // timezone
  setenv("TZ", "UTC-2", 1);
  tzset();

  String response = "";
  modem.sendAT("+CCLK?");
  if (modem.waitResponse(10000, response) != 1) {
    ESP_LOGI(TAG, "Failed to get time");
  }

  String time = response.substring(response.indexOf('"') + 1, response.lastIndexOf('"'));
  int year = time.substring(0,2).toInt();
  ESP_LOGI(TAG, "Time synced. Current datetime in yy/MM/dd,hh:mm:ss+zz is %s", time.c_str());
  return year;
}

// initiale the T-PCIE modem
void initializeModem() {
  ESP_LOGI(TAG, "Initializing modem...");
  pinMode(PCIE_PWR_PIN, OUTPUT);
  digitalWrite(PCIE_PWR_PIN, HIGH);
  delay(300);
  digitalWrite(PCIE_PWR_PIN, LOW);
  delay(3000);
  SerialAT.begin(115200, SERIAL_8N1, PCIE_RX_PIN, PCIE_TX_PIN);
  while(!modem.init()) {
    ESP_LOGI(TAG, "Failed to restart modem, delaying 3s and retrying");
    delay(3000);
  }
  ESP_LOGI(TAG, "Initialized modem");

  // register network
  String result;
  while(true) {
    Serial.println("Waiting for network...");
    modem.sendAT("+CREG?");
    if (modem.waitResponse(5000, result) == 1) {
      if (result.indexOf("+CREG: 0,1") > 0 || result.indexOf("+CREG: 0,5") > 0) {
        break;
      }
    }
    delay(2000);
  }

  // set to GSM mode
  modem.sendAT("+CNMP=38");
  if (modem.waitResponse(10000) != 1) {
    ESP_LOGI(TAG, "setNetworkMode to GSM failed");
    return;
  }
}

// log messages to SD card
int sdCardLogOutput(const char *format, va_list args) {
	char buf[128];
	int ret = vsnprintf(buf, sizeof(buf), format, args);
  if (log_file) {
    Serial.println(buf);
    log_file.print(buf);
    log_file.flush();
  }
	return ret;
}

// initialize the SD card
void initializeSDCard() {
  SPI.begin(SD_SCLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN)) {
    ESP_LOGE(TAG, "Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    ESP_LOGE(TAG, "No SD card attached");
    return;
  }

  ESP_LOGI(TAG, "Initialized SD Card Type: ");
  if (cardType == CARD_MMC) {
    ESP_LOGI(TAG, "MMC");
  } else if (cardType == CARD_SD) {
    ESP_LOGI(TAG, "SDSC");
  } else if (cardType == CARD_SDHC) {
    ESP_LOGI(TAG, "SDHC");
  } else {
    ESP_LOGI(TAG, "UNKNOWN");
  }

  log_file = SD.open(LOG_FILE_NAME, FILE_APPEND);
  if (!log_file) {
    ESP_LOGE(TAG, "Failed to open log file");
  } else {
    ESP_LOGI(TAG, "Using SD card callback for logging");
    esp_log_set_vprintf(sdCardLogOutput);
  }
}

// check if firmware update is necessary
bool checkForUpdate() {
  modem.sendAT("+HTTPINIT");
  if (modem.waitResponse(10000) != 1) {
    ESP_LOGI(TAG, "Failed to initialize HTTP");
    return false;
  }

  modem.sendAT(String("+HTTPPARA=\"URL\",\"") + OTA_UPDATE_URL + OTA_VERSION_ENDPOINT + "\"");
  if (modem.waitResponse(10000) != 1) {
    ESP_LOGI(TAG, "Failed to set URL");
    return false;
  }

  String response = sendATCommand("+HTTPACTION=0", "+HTTPACTION: 0,200", 20000);

  response = sendATCommand("+HTTPREAD=99", "+HTTPREAD: 0", 20000);
  response = response.substring(22, 28); // splice out the version number

  modem.sendAT("+HTTPTERM");
  if (modem.waitResponse(10000) != 1) {
    ESP_LOGI(TAG, "Failed to disable HTTP service");
  }

  ESP_LOGI(TAG, "Response: %s", response.c_str());

  String currentVersion = preferences.getString("firmwareVersion", "");
  if (response != currentVersion) {
    ESP_LOGI(TAG, "New firmware version available");
    newFirmwareVersion = response;
    return true;
  }

  ESP_LOGI(TAG, "No new firmware version available");
  return false;
}

// download firmware file from remote server to SD card
bool downloadFirmware() {
  const int chunkSize = 1024;

  modem.sendAT("+HTTPINIT");
  if (modem.waitResponse(10000) != 1) {
    ESP_LOGI(TAG, "Failed to initialize HTTP");
    return false;
  }

  modem.sendAT(String("+HTTPPARA=\"URL\",\"") + OTA_UPDATE_URL + OTA_UPDATE_ENDPOINT + "\"");
  if (modem.waitResponse(10000) != 1) {
    ESP_LOGI(TAG, "Failed to set URL");
    return false;
  }

  // GET request
  String response = sendATCommand("+HTTPACTION=0", "+HTTPACTION: 0,200", 20000);

  // int fileLength = response.substring(24, response.length() - 1).toInt(); // parse out the length returned after code 200
  // ESP_LOGI(TAG, "File Length To Download: %d", fileLength);

  File firmware = SD.open(FIRMWARE_FILE_NAME, FILE_WRITE);
  if (!firmware) {
    ESP_LOGI(TAG, "Failed to open file for writing");
    return false;
  }

  // read the firmware file in chunks
  int bytesRead = 0;
  while (true) {
    String readCommand = String("+HTTPREAD=") + bytesRead + "," + chunkSize;
    // String response = sendATCommand(readCommand, "+HTTPREAD: 0", 10000);

    // modified sendATCommand to prevent newline stripping
    modem.sendAT(readCommand);
    String response;
    unsigned long startTime = millis();
    boolean ok = false;
    while (!ok && millis() - startTime < 10000) {
      String s = modem.stream.readStringUntil('\n');
      ok = s.indexOf("+HTTPREAD: 0") >= 0;
      response += s + '\n';
    }
    response.remove(response.length() - 1, 1);

    if (response.indexOf("ERROR") != -1) {
      break;
    }

    if (response.indexOf("+HTTPREAD:") == -1) {
      ESP_LOGI(TAG, "Failed to read HTTP chunk");
      firmware.close();
      return false;
    }

    // extract the data part from the response
    int startIndex = response.indexOf('\r', response.indexOf("+HTTPREAD:")) + 2;
    int endIndex = response.length() - 15; // parse out the +HTTPREAD: 0 at end
    String chunk = response.substring(startIndex, endIndex);
    firmware.print(chunk);
    firmware.flush();
    bytesRead += chunk.length();

    // // if returned data is shorter than chunk size then finished
    // String dataSize = response.substring(response.indexOf("DATA,") + 5);
    // int dataSizeInt = dataSize.substring(0, dataSize.indexOf('\r')).toInt();

    // // Check if we have read all the data
    // if (dataSizeInt < chunkSize) {
    //   break;
    // }
  }

  // Close the file
  firmware.flush();
  firmware.close();

  modem.sendAT("+HTTPTERM");
  if (modem.waitResponse(10000) != 1) {
    ESP_LOGI(TAG, "Failed to disable HTTP service");
  }

  ESP_LOGI(TAG, "Firmware downloaded successfully");
  return true;
}

// flash new firmware from SD card
void applyFirmware() {
  File firmware = SD.open(FIRMWARE_FILE_NAME, FILE_READ);
  if (!firmware) {
    ESP_LOGI(TAG, "Failed to open firmware file");
    return;
  }

  size_t firmwareSize = firmware.size();

  if (!Update.begin(firmwareSize)) {
    ESP_LOGI(TAG, "Failed to start update");
    return;
  }

  size_t written = Update.writeStream(firmware);
  if (written == firmwareSize) {
    ESP_LOGI(TAG, "Written : %d successfully", written);
  } else {
    ESP_LOGI(TAG, "Written only : %d/%d", written, firmwareSize);
  }

  if (Update.end()) {
    ESP_LOGI(TAG, "OTA done!");
    if (Update.isFinished()) {
      ESP_LOGI(TAG, "Update successfully completed. Rebooting.");
      preferences.putString("firmwareVersion", newFirmwareVersion);
      ESP.restart();
    } else {
      ESP_LOGI(TAG, "Update not finished? Something went wrong!");
    }
  } else {
    ESP_LOGI(TAG, "Error #: %d", Update.getError());
  }

  firmware.close();
}

void setup() {
  pinMode(PWR_ON_PIN, OUTPUT);
  digitalWrite(PWR_ON_PIN, HIGH);
  delay(100);
  // Serial2.begin(115200, SERIAL_8N1, PCIE_RX_PIN, PCIE_TX_PIN);
  Serial.begin(115200);
  delay(10);
  esp_log_level_set("*", ESP_LOG_VERBOSE);
  esp_log_level_set(TAG, ESP_LOG_VERBOSE);

  initializeSDCard();

  ESP_LOGI(TAG, "Starting camera sensor %s...", DEVICENAME);

  // disable WiFi and bluetooth for power consumption
  WiFi.mode(WIFI_OFF);
  btStop();

  // initialize NVME
  preferences.begin("image-data", false);

  // initializeConnectionWifi();

  initializeModem();

  getIMEI();

  clearEFS();

  // OTA updates
  if (checkForUpdate()) {
    for (int retries = 0; retries < 5; ++retries) {
      if (downloadFirmware()) {
        applyFirmware();
      }
      delay(10000);
    }
  }

  initializeCamera();

  int year = syncTime();
  while (year < 24) {
    year = syncTime();
  }

  // sendLogFile();
  // takePhoto();
}

void loop() {
  // unsigned long lastReportTime = preferences.getULong("lastReportTime", 0);
  // unsigned long currentTime = getCurrentTime();
  static unsigned long lastReportTime = 0;
  static unsigned long lastOTACheckTime = 0;
  unsigned long currentTime = millis();

  takePhoto();

  if (currentTime - lastReportTime >= 86400000) { // 24 hours = 86400 seconds = 86400000 millis
    lastReportTime = currentTime;
    // preferences.putULong("lastReportTime", lastReportTime);
    // sendLogFile();
  }

  if (currentTime - lastOTACheckTime >= 1800000) {
    lastOTACheckTime = currentTime;
    if (checkForUpdate()) {
      for (int retries = 0; retries < 5; ++retries) {
        if (downloadFirmware()) {
          applyFirmware();
        }
        delay(10000);
      }
    }

    clearEFS();
  }

  ESP_LOGI(TAG, "Heap: %d/%d, PSRAM: %d/%d", (ESP.getHeapSize() - ESP.getFreeHeap()), ESP.getHeapSize(), (ESP.getPsramSize() - ESP.getFreePsram()), ESP.getPsramSize());

  // delay(10000);
  esp_sleep_enable_timer_wakeup(10 * uS_TO_S_FACTOR); //light sleep between photos
  delay(100);
  esp_light_sleep_start();
}
