#include <WiFi.h>
#include <ESP32_FTPClient.h>
#include <esp_camera.h>
#include "config.h"
#include <esp_sntp.h>
#include <HardwareSerial.h>
#include <StreamDebugger.h>
#include <TinyGsmClient.h>
#include <SPI.h>
#include <SD.h>

// configurations defined in config.h
ESP32_FTPClient ftp (FTP_SERVER, FTP_USER, FTP_PASS, 20000);
#ifdef DUMP_AT_COMMANDS
  StreamDebugger debugger(SerialAT, Serial);
#endif
#ifdef DUMP_AT_COMMANDS
  TinyGsm modem(debugger);
#else
  TinyGsm modem(SerialAT);
#endif
String IMEI = "";
String GPSPosition = "";
String LogContent = "";
int totalPictures = 0;
int sendTimes = 0;

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
void syncTime();
void initializeModem();
void initializeSDCard();
void LogMessage(String msg);

// datetime as string of numbers
String getCurrentDateTime() {
  time_t now = time(nullptr);
  struct tm * timeinfo = localtime(&now);
  char buffer[30];
  strftime(buffer, 30, "%d%m%Y%H%M%S", timeinfo);
  return String(buffer);
}

// datetime as human readable string
String getFormattedDateTime() {
  time_t now = time(nullptr);
  struct tm * timeinfo = localtime(&now);
  char buffer[30];
  strftime(buffer, 30, "%d/%m/%Y %H:%M:%S", timeinfo);
  return String(buffer);
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

// take photo, process it, and send to server if needed
void takePhoto() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    LogMessage("Camera capture failed");
    return;
  }
  else {
    totalPictures += 1;
  }

  // send to FTP server
  ftp.OpenConnection();
  ftp.InitFile("Type I");
  ftp.ChangeWorkDir("/");
  String filename = getFormattedImageName();
  ftp.NewFile(filename.c_str());
  ftp.WriteData(fb->buf, fb->len);
  ftp.CloseFile();
  ftp.CloseConnection();

  // return the frame buffer back to the driver for reuse
  esp_camera_fb_return(fb);

  LogMessage("Time: " + String(esp_timer_get_time()));
  LogMessage("Photo taken and uploaded successfully");
  sendTimes += 1;
}

// send formatted logfile with sensor information
void sendLogFile() {
  String formattedDateTime = getFormattedDateTime();
  getGPSPosition(); // update GPS position data

  LogContent = "IMEI:" + IMEI + "\n";
  LogContent += "CSQ:12\n";
  LogContent += "CamID:" + String(DEVICENAME) + "\n";
  LogContent += "Temp:9C\n";
  LogContent += "Date:" + formattedDateTime + "\n";
  LogContent += "Bat:100%\n";
  LogContent += getSDCardInfo() + "\n";
  LogContent += "Total:" + String(totalPictures) + "\n";
  LogContent += "Send:" + String(sendTimes) + "\n";
  LogContent += "GPS:" + GPSPosition + "\n";

  LogMessage("Log Content:\n" + LogContent);

  ftp.OpenConnection();
  ftp.InitFile("Type A");
  ftp.ChangeWorkDir("/");
  String fileName = getFormattedReportName();
  LogMessage("Log filename: " + fileName);
  ftp.NewFile(fileName.c_str());
  ftp.Write(LogContent.c_str());
  ftp.CloseFile();
  ftp.CloseConnection();

  LogMessage("Time: " + String(esp_timer_get_time()));
  LogMessage("Daily report generated and uploaded successfully");
}

// connect to WiFi
void initializeConnectionWifi() {
  LogMessage("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    LogMessage(".");
  }
  LogMessage("");
  LogMessage("Connected to WiFi");
}

// initialize the camera
void initializeCamera() {
  LogMessage("Initializing camera...");

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
      config.frame_size = FRAMESIZE_QHD; // change for better resolution. do not go above QVGA size when not jpeg
      config.jpeg_quality = 10; // 0-63 with lower number is better quality
      config.fb_count = 2;
  } else {
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
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }


// #ifdef CAM_IR_PIN
//   // test IR Filter
//   pinMode(CAM_IR_PIN, OUTPUT);
//   LogMessage("Test IR Filter");
//   int i = 3;
//   while (i--) {
//     digitalWrite(CAM_IR_PIN, 1 - digitalRead(CAM_IR_PIN)); delay(1000);
//   }
// #endif

  LogMessage("IR Filter Off");
  pinMode(CAM_IR_PIN, OUTPUT);
  digitalWrite(CAM_IR_PIN, LOW);

  LogMessage("Camera initialized");
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
    LogMessage("Enabling GPS/GNSS/GLONASS and gathering position data");

    modem.sendAT("+CGPS=1"); // enable GPS
    if (modem.waitResponse(10000) != 1) {
        LogMessage("Failed to enable GPS");
        return;
    }

    String gps_latitude = "";
    String gps_longitude = "";
    while (true) {
        LogMessage("Requesting GPS info");

        modem.sendAT("+CGNSSINFO");
        String response = modem.stream.readStringUntil('\n');
        LogMessage(response);

        if (response.indexOf(",N,") != -1 || response.indexOf(",S,") != -1) {
            int latStart = response.indexOf(",", 22) + 1;
            int latEnd = response.indexOf(",", latStart);
            gps_latitude = response.substring(latStart, latEnd);
            LogMessage(gps_latitude);

            int latDirEnd = response.indexOf(",", latEnd + 1);
            String latDir = response.substring(latEnd + 1, latDirEnd);

            int lonStart = response.indexOf(",", latDirEnd) + 1;
            int lonEnd = response.indexOf(",", lonStart);
            gps_longitude = response.substring(lonStart, lonEnd);
            LogMessage(gps_longitude);

            int lonDirEnd = response.indexOf(",", lonEnd + 1);
            String lonDir = response.substring(lonEnd + 1, lonDirEnd);

            String latitudeDMS = convertToDMS(gps_latitude, latDir);
            String longitudeDMS = convertToDMS(gps_longitude, lonDir);

            GPSPosition = latitudeDMS + " " + longitudeDMS;
            LogMessage("GPS Position: " + GPSPosition);
            break;
        } else {
            LogMessage("Couldn't get GPS info, retrying in 10s.");
            delay(15000);
        }
    }
}

// get IMEI number from GSM module
void getIMEI() {
  LogMessage("Updating IMEI");
  modem.sendAT("+CGSN");
  String IMEI_buf = "";
  if (modem.waitResponse(10000, IMEI_buf) != 1) {
    LogMessage("Failed to get IMEI");
    return;
  }
  IMEI = IMEI_buf.substring(2, 17); // parse AT command response
  LogMessage("IMEI: " + IMEI);
}

// ensure time is set
void syncTime() {
  LogMessage("Syncing Time...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "UTC-2", 1); // UTC+2
  tzset();
  while (time(nullptr) < 8 * 3600 * 2) { // wait for time to be set
    delay(500);
    LogMessage(".");
  }
  LogMessage("");
  LogMessage("Time synced");
}

// initiale the T-PCIE modem
void initializeModem() {
  LogMessage("Initializing modem...");
  pinMode(PCIE_PWR_PIN, OUTPUT);
  digitalWrite(PCIE_PWR_PIN, HIGH);
  delay(300);
  digitalWrite(PCIE_PWR_PIN, LOW);
  delay(3000);
  SerialAT.begin(115200, SERIAL_8N1, PCIE_RX_PIN, PCIE_TX_PIN);
  while(!modem.init()) {
    LogMessage("Failed to restart modem, delaying 3s and retrying");
    delay(3000);
  }
  delay(3000);
  LogMessage("Initialized modem");

  // set to GSM mode
  modem.sendAT("+CNMP=38");
  if (modem.waitResponse(10000) != 1) {
    LogMessage("setNetworkMode to GSM failed");
    return ;
  }
}

// initialize the SD card
void initializeSDCard() {
  SPI.begin(SD_SCLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN)) {
    LogMessage("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) {
    LogMessage("No SD card attached");
    return;
  }

  LogMessage("Initialized SD Card Type: ");
  if (cardType == CARD_MMC) {
    LogMessage("MMC");
  } else if (cardType == CARD_SD) {
    LogMessage("SDSC");
  } else if (cardType == CARD_SDHC) {
    LogMessage("SDHC");
  } else {
    LogMessage("UNKNOWN");
  }

  File log_file = SD.open(LOG_FILE_NAME, FILE_WRITE);
  if (log_file.print("Created log file")) {
    LogMessage("Created log file");
  } else {
    LogMessage("Create log file failed");
  }
  log_file.close();

  File log_file_read = SD.open(LOG_FILE_NAME);
  if (!log_file_read) {
    Serial.println("Failed to open file for reading");
    return;
  }
  Serial.print("Read from file: ");
  while (log_file_read.available()) {
    Serial.write(log_file_read.read());
  }
  log_file_read.close();
}

// log message to SD card
void LogMessage(String msg) {
  Serial.println(msg);
  File log_file = SD.open(LOG_FILE_NAME, FILE_APPEND);
  if(!log_file) {
    Serial.println("Failed to open log file for writing");
    return;
  }
  if(!log_file.print(msg)) {
    Serial.println("Failed to write to log file");
  }
  log_file.close();
}

void setup() {
  pinMode(PWR_ON_PIN, OUTPUT);
  digitalWrite(PWR_ON_PIN, HIGH);
  delay(100);
  Serial.begin(115200);
  delay(10);

  initializeSDCard();

  LogMessage("Starting camera sensor...");

  initializeConnectionWifi();

  initializeModem();

  getIMEI();

  initializeCamera();

  syncTime();

  // sendLogFile();
  // takePhoto();
}

void loop() {
  static unsigned long lastPhotoTime = 0;
  static unsigned long lastReportTime = 0;
  unsigned long currentTime = millis();

 if (currentTime - lastPhotoTime >= 300000) { // 5 minutes
   // takePhoto();
    lastPhotoTime = currentTime;
  }

  // if (currentTime - lastReportTime >= 86400000) { // 24 hours
  //   sendLogFile();
  //   lastReportTime = currentTime;
  // }

  delay(1000);
}
