#include <stdarg.h>
#include <stdio.h>
#include <WiFi.h>
#include <ESP32_FTPClient.h>
#include <esp_camera.h>
#include "config.h"
#include <esp_sntp.h>
#include <esp_log.h>
#include <esp32-hal-log.h>
#include <HardwareSerial.h>
#include <StreamDebugger.h>
#include <TinyGsmClient.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>

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
File log_file;
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
int sdCardLogOutput(const char *format, va_list args);

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
    ESP_LOGI(TAG, "Camera capture failed");
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

  ESP_LOGI(TAG, "Time: %s", String(esp_timer_get_time()));
  ESP_LOGI(TAG, "Photo taken and uploaded successfully");
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

  ESP_LOGI(TAG, "Log Content:\n%s", LogContent.c_str());

  ftp.OpenConnection();
  ftp.InitFile("Type I");
  ftp.ChangeWorkDir("/");
  String fileName = getFormattedReportName();
  ftp.NewFile(fileName.c_str());
  unsigned char *data = (unsigned char *)LogContent.c_str();
  ftp.WriteData(data, LogContent.length());
  ftp.CloseFile();
  ftp.CloseConnection();

  ESP_LOGI(TAG, "Time: %s", String(esp_timer_get_time()).c_str());
  ESP_LOGI(TAG, "Daily report generated and uploaded successfully");
}

// connect to WiFi
void initializeConnectionWifi() {
  ESP_LOGI(TAG, "Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    ESP_LOGI(TAG, "Connecting to WiFi...");
  }
  ESP_LOGI(TAG, "Connected to WiFi");
}

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
    ESP_LOGI(TAG, "Camera init failed with error 0x%x", err);
    return;
  }

// #ifdef CAM_IR_PIN
//   // test IR Filter
//   pinMode(CAM_IR_PIN, OUTPUT);
//   ESP_LOGI(TAG, "Test IR Filter");
//   int i = 3;
//   while (i--) {
//     digitalWrite(CAM_IR_PIN, 1 - digitalRead(CAM_IR_PIN)); delay(1000);
//   }
// #endif

  // TODO: on during the day, off during the night
  ESP_LOGI(TAG, "IR Filter Off");
  pinMode(CAM_IR_PIN, OUTPUT);
  digitalWrite(CAM_IR_PIN, LOW);

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
void syncTime() {
  ESP_LOGI(TAG, "Syncing Time...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "UTC-2", 1); // UTC+2
  tzset();
  while (time(nullptr) < 8 * 3600 * 2) { // wait for time to be set
    delay(500);
    ESP_LOGI(TAG, "Syncing Time...");
  }
  ESP_LOGI(TAG, "Time synced");
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
  delay(3000);
  ESP_LOGI(TAG, "Initialized modem");

  // set to GSM mode
  modem.sendAT("+CNMP=38");
  if (modem.waitResponse(10000) != 1) {
    ESP_LOGI(TAG, "setNetworkMode to GSM failed");
    return ;
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

void setup() {
  pinMode(PWR_ON_PIN, OUTPUT);
  digitalWrite(PWR_ON_PIN, HIGH);
  delay(100);
  Serial.begin(115200);
  delay(10);
  esp_log_level_set("*", ESP_LOG_VERBOSE);
  esp_log_level_set(TAG, ESP_LOG_VERBOSE);

  initializeSDCard();

  ESP_LOGI(TAG, "Starting camera sensor...");

  initializeConnectionWifi();

  initializeModem();

  getIMEI();

  initializeCamera();

  syncTime();

  // sendLogFile();
}

void loop() {
  static unsigned long lastPhotoTime = 0;
  static unsigned long lastReportTime = 0;
  unsigned long currentTime = millis();

 if (currentTime - lastPhotoTime >= 600000) { // 10 minutes
    takePhoto();
    lastPhotoTime = currentTime;
  }

  // if (currentTime - lastReportTime >= 86400000) { // 24 hours
  //   sendLogFile();
  //   lastReportTime = currentTime;
  // }

  delay(1000);
}
