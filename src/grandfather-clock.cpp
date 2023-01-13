#include <AudioFileSourceSD.h>
#include <AudioOutputSPDIF.h>
#include <AudioGeneratorWAV.h>
#include <AudioOutputI2S.h>
#include <freertos/task.h>
#include "ESPAWSClient.h"
#include "time.h"
#include <TaskScheduler.h>
#include <Arduino.h>
#include "StepUtils.h"
#include "UrlUtils.h"
#include "auth.h"
#include <SimpleFTPServer.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ssl_client.h>
#include <WiFi.h>
// #include <WiFiUDP.h>
WiFiClientSecure client;
HTTPClient http;
// WiFiUDP ntpUDP;
FtpServer ftpSrv;  //set #define FTP_DEBUG in ESP8266FtpServer.h to see ftp verbose on serial

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;

// NTPClient timeClient(ntpUDP);
const char* stateFilename = "/.state";
const char* ssid = "xxxxxxxx";
const char* password = "xxxxxxxxxxxxxx";
const char* hostname = "clock";
ESPAWSClient aws = ESPAWSClient("sqs", ACCESS_KEY, SECRET_KEY, "xx-xxxx-x", "amazonaws.com");
Stepper stepper = Stepper();

// States
int volume = 50;
bool enable = true;
String selectedAudioFile;
// End States

byte direction = 0;
File stateFile;
#define MAX_AUDIO_FILES 256
String audioFileList[MAX_AUDIO_FILES];
int lastHour = -1;
#define SPDIF_OUT_PIN 27
#define SPI_SPEED SD_SCK_MHZ(40)
AudioFileSourceSD* source;
AudioGeneratorWAV* decoder = NULL;
// AudioOutputSPDIF *out;
AudioOutputI2S* out;

// Scheduler

Scheduler runner;
// Callback methods prototypes
void sqsCallback();
void timeCallback();
void getAudioFiles();
void httpReady();
void ftpCallback();
Task sqsTask(21000, TASK_FOREVER, &sqsCallback);
Task timeTask(10000, TASK_FOREVER, &timeCallback);
Task audioFileCheck(30000, TASK_FOREVER, &getAudioFiles);
Task requestRead(50, TASK_FOREVER, &httpReady);
Task ftpTask(0, TASK_FOREVER, &ftpCallback);

// End Scheduler

void writeState() {
  stateFile = SD.open(stateFilename, FILE_WRITE);
  if (stateFile) {
    StaticJsonDocument<256> writeDoc;
    writeDoc["step"] = stepper.getStep();
    writeDoc["volume"] = volume;
    writeDoc["enable"] = enable;
    writeDoc["selectedAudioFile"] = selectedAudioFile;
    serializeJson(writeDoc, stateFile);
    stateFile.close();
  }
}

void readState() {
  if (SD.exists(stateFilename)) {
    DynamicJsonDocument doc(2048);
    Serial.println("Reading Existing State");
    stateFile = SD.open(stateFilename);
    deserializeJson(doc, stateFile);
    stepper.setStep(doc["step"].as<int>());
    volume = doc["volume"].as<int>();
    enable = doc["enable"].as<bool>();
    selectedAudioFile = doc["selectedAudioFile"].as<String>();

    Serial.print("Step set to: ");
    Serial.println(stepper.getStep());
    Serial.print("Volume set to: ");
    Serial.println(volume);
    Serial.print("Enable set to: ");
    Serial.println(enable);
    Serial.print("SelectedAudioFile set to: ");
    Serial.println(selectedAudioFile);

    if(stepper.resetToZeroStep()){
      writeState();
    }
  }else{
    Serial.print(stateFilename);
    Serial.println(" does not exist");
  }
}

void setAudioOutput() {
  out = new AudioOutputI2S(0, 1);
  out->SetGain(0);
  out->SetPinout(26, 25, 22);
}
void setupAudio() {
  Serial.println("Audio Setup");
  // audioLogger = &Serial;
  source = new AudioFileSourceSD();
  // out = new AudioOutputSPDIF(SPDIF_OUT_PIN);
  setAudioOutput();
  decoder = new AudioGeneratorWAV();
}
void getAudioFiles() {
  File dir = SD.open("/");
  File file;
  int i = 0;
  while (file = dir.openNextFile()) {
    String name = String(file.name());
    if (name.endsWith(".wav")) {
      audioFileList[i] = name.substring(1, name.lastIndexOf(".wav"));
      Serial.println("Found file " + name);
      if (selectedAudioFile.isEmpty()) {
        selectedAudioFile = audioFileList[i];
        Serial.println("Selected audio file: " + selectedAudioFile);
      }
      i++;
    }
    file.close();
  }
  Serial.print("Found ");
  Serial.print(i);
  Serial.println(" Audio Files");
  for (; i < MAX_AUDIO_FILES; i++) {
    audioFileList[i].clear();
  }
}
#define MAX_PLAYBACK_HANDLES 5
// TaskHandle_t audioPlaybackHandle[MAX_PLAYBACK_HANDLES];
TaskHandle_t audioPlaybackHandle;
// int audioPlaybackHandleI = 0;
void audioPlaybackCallback(void* params) {
  try {
    if (out == NULL) setAudioOutput();
    // Close old run
    // decoder = new AudioGeneratorWAV();
    // if (decoder != NULL && decoder->isRunning()) decoder->stop();
    // if(source->isOpen()) source->close();
    // Start
    if (!selectedAudioFile.isEmpty()) {
      out->SetGain(((float)volume) / 250);
      if (source->open(("/" + selectedAudioFile + ".wav").c_str())) {
        Serial.println("Playing " + selectedAudioFile + " volume: " + String(volume));
        decoder->begin(source, out);
        while (decoder->isRunning()) {
          if (!decoder->loop()) {
            out->SetGain(0);
            out->flush();
            decoder->stop();
            out->stop();
            out = NULL;
          }
        }
        Serial.println("Audio done");
      } else {
        Serial.println("Audio file not found: " + selectedAudioFile);
      }
    } else {
      Serial.println("No selected audio file");
    }
    if (source->isOpen()) source->close();
    vTaskDelete(NULL);
  } catch (...) {
    Serial.println("Audio Play Crash");
    vTaskDelete(NULL);
  }
}

void playAudioFile() {
  if (decoder->isRunning()) {
    Serial.println("Reset Seek");
    source->seek(0, SEEK_SET);
  } else {
    Serial.println("Create Task");
    xTaskCreatePinnedToCore(
      audioPlaybackCallback, /* Function to implement the task */
      "AudioPlayback",       /* Name of the task */
      10000,                 /* Stack size in words */
      NULL,                  /* Task input parameter */
      5,                     /* Priority of the task */
      &audioPlaybackHandle,  /* Task handle. */
      0);                    /* Core where the task should run */
  }
}
void connectedToAP(WiFiEvent_t wifi_event, WiFiEventInfo_t wifi_info) {
  Serial.println("[+] Connected to the WiFi network");
}

void disconnectedFromAP(WiFiEvent_t wifi_event, WiFiEventInfo_t wifi_info) {
  Serial.println("[-] Disconnected from the WiFi AP");
  WiFi.begin(ssid, password);
}

void gotIPFromAP(WiFiEvent_t wifi_event, WiFiEventInfo_t wifi_info) {
  Serial.print("[+] Local ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.onEvent(connectedToAP, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(disconnectedFromAP, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent(gotIPFromAP, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
}

void setup() {
  Serial.begin(115200);
  // Setup Pins
  stepper.initialize();
  // Audio should be before SD
  setupAudio();


  delay(2000);
  initWiFi();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  // timeClient.begin();

  // Setup SD Card
  while (!SD.begin()) {
    Serial.println("SD failed!");
    delay(1000);
  }
  delay(2000);
  // Load old state
  readState();
  getAudioFiles();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  ftpSrv.begin("grandfather", "clock");  //username, password for ftp.   (default 21, 50009 for PASV)


  runner.init();
  runner.addTask(sqsTask);
  runner.addTask(timeTask);
  runner.addTask(audioFileCheck);
  runner.addTask(requestRead);
  runner.addTask(ftpTask);
  sqsTask.enableDelayed(6000);
  timeTask.enableDelayed(10000);
  audioFileCheck.enableDelayed(10000);
  ftpTask.enable();


  Serial.print("Starting Run Loop ");
  Serial.println(xPortGetCoreID());
}


void runCuckoo() {
  Serial.println("Cuckoo Time");
  stepper.enable();
  delay(100);
  stepper.rotate(450);
  writeState();
  playAudioFile();
  delay(500);
  stepper.rotate(-90);
  writeState();
  delay(500);
  stepper.rotate(90);
  writeState();
  delay(500);
  stepper.rotate(-90);
  writeState();
  delay(500);
  stepper.rotate(90);
  writeState();
  delay(500);
  stepper.rotate(-450);
  writeState();
  delay(100);
  Serial.println("End Cuckoo Time");
  stepper.disable();
}

void (*httpCallback)();
void httpReady() {
  if (aws.receiveReady()) {
    requestRead.disable();
    (*httpCallback)();
  }
}
void receiveMessageCallback() {
  AWSResponse resp = aws.receive();
  Serial.println("AWS Response " + String(resp.status) + " - " + resp.body);
  if (resp.status == 200) {
    int startI;
    int endI;
    startI = resp.body.indexOf("<Body>");
    if (startI >= 0) {
      endI = resp.body.indexOf("</Body>");
      String responseBody = resp.body.substring(startI + 6, endI);
      responseBody.replace("&quot;", "\"");
      Serial.println(responseBody);
      DynamicJsonDocument doc(2048);
      deserializeJson(doc, responseBody);


      // Delete Message
      startI = resp.body.indexOf("<ReceiptHandle>");
      if (startI >= 0) {
        endI = resp.body.indexOf("</ReceiptHandle>");

        String receiptHandle = resp.body.substring(startI + 15, endI);
        Serial.println("Deleting: " + receiptHandle);
        aws.doGet("/xxxxxxxxxxx/ClockQueue.fifo", "Action=DeleteMessage&ReceiptHandle=" + urlEncode(receiptHandle));
        while (!aws.receiveReady())
          ;
        resp = aws.receive();
        Serial.println("AWS Response " + String(resp.status) + " - " + resp.body);
      }

      // Do body

      if (!doc["volume"].isNull()) {
        volume = doc["volume"].as<int>();
        Serial.print("Volume set to: ");
        Serial.println(volume);
      }

      if (!doc["degrees"].isNull()) {
        int degrees = doc["degrees"].as<int>();
        Serial.print("Moving: ");
        Serial.print(degrees);
        Serial.println(" degrees");
        stepper.forceMove(degrees);
        writeState();
      }
      if (!doc["sound"].isNull()) {
        String str = doc["sound"].as<String>();
        str.toLowerCase();
        if (str == "next") {
          int i = 0;
          for (; i < MAX_AUDIO_FILES; i++) {
            if (audioFileList[i] == selectedAudioFile) {
              break;
            }
          }
          if (i == MAX_AUDIO_FILES || audioFileList[i + 1].isEmpty()) {
            selectedAudioFile = audioFileList[0];
          } else {
            selectedAudioFile = audioFileList[i + 1];
          }
          Serial.print("Changed audio to: ");
          Serial.println(selectedAudioFile);

        } else if (!str.isEmpty() && sizeof(str) > 0) {
          int i = 0;
          for (; i < MAX_AUDIO_FILES; i++) {
            if (audioFileList[i] == str) {
              break;
            }
          }
          if (i != MAX_AUDIO_FILES) {
            selectedAudioFile = audioFileList[i];
            Serial.print("Changed audio to: ");
            Serial.println(selectedAudioFile);
          } else {
            Serial.print("Could not find audio file: ");
            Serial.println(str);
          }
        }
      }
      if (doc["run"].as<bool>()) {
        runCuckoo();
      }
      if (!doc["enable"].isNull()) {
        enable = doc["enable"].as<bool>();
        if (enable)
          Serial.println("Enabled");
        else
          Serial.println("Disabled");
      }
      writeState();
    }
    sqsTask.enable();
  } else {
    sqsTask.enableDelayed(10000);
  }
}

void sqsCallback() {
  struct tm timeinfo;
  if (WiFi.status() == WL_CONNECTED && getLocalTime(&timeinfo)) {
    Serial.println("GET /xxxxxxxxxxx/ClockQueue.fifo?Action=ReceiveMessage&MaxNumberOfMessages=1&WaitTimeSeconds=20");
    aws.doGet("/xxxxxxxxxxx/ClockQueue.fifo", "Action=ReceiveMessage&MaxNumberOfMessages=1&WaitTimeSeconds=20");
    httpCallback = &receiveMessageCallback;
    requestRead.enable();
    sqsTask.disable();
  }
}
void timeCallback() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    return;
  }
  // Check if an hour has passed
  if (enable && timeinfo.tm_min == 0 && lastHour != timeinfo.tm_hour) {
    lastHour = timeinfo.tm_hour;
    runCuckoo();
  }
}
void ftpCallback(){
  ftpSrv.handleFTP();
}
void loop() {
  runner.execute();
}
