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
#include <SD.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ssl_client.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>

#include <SimpleFTPServer.h>

// #include <WiFiUDP.h>
WiFiClientSecure client;
HTTPClient http;
// WiFiUDP ntpUDP;
FtpServer ftpSrv;  //set #define FTP_DEBUG in ESP8266FtpServer.h to see ftp verbose on serial
AsyncWebServer server(80);

const char* ntpServer1 = "0.pool.ntp.org";
const char* ntpServer2 = "1.pool.ntp.org";
const char* ntpServer3 = "2.pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 0;

// NTPClient timeClient(ntpUDP);
const char* stateFilename = "/.state";
const char* configFilename = "/config.json";
const char* hostname = "clock";
ESPAWSClient* aws;
Stepper stepper = Stepper();

// States
int volume = 50;
bool enable = true;
String selectedAudioFile;
// End States

byte rotationDirection = 0;
File stateFile;
File configFile;
#define MAX_AUDIO_FILES 256

String audioFileList[MAX_AUDIO_FILES];
int lastHour = -1;
int timeSkip = 0;
#define LED_PIN 2
#define USE_STOPPER true
#if USE_STOPPER
  #define STOPPER_OUT_PIN 16
  #define STOPPER_PIN 17
#endif
#define SPDIF_OUT_PIN 27
// #define SPI_SPEED SD_SCK_MHZ(40)

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

String awsAccessKey;
String awsSecretKey;
String sqsQueue;
String awsRegion;
String wifiSSID;
String wifiPassword;
String ftpUsername;
String ftpPassword;
	
int gearSpokes = 10;
int slideHoles = 15;
double maxSlidePercentage = 100;
double shuffleSlidePercentage = 100;
double maxVolume = 100;

void writeState() {
  stateFile = SD.open(stateFilename, FILE_WRITE);
  if (stateFile) {
    StaticJsonDocument<256> writeDoc;
#if !USE_STOPPER
    writeDoc["step"] = stepper.getStep();
#endif
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
    stateFile.close();
    volume = doc["volume"].as<int>();
    enable = doc["enable"].as<bool>();
    selectedAudioFile = doc["selectedAudioFile"].as<String>();

#if !USE_STOPPER
    stepper.setStep(doc["step"].as<int>());
    Serial.print("Step set to: ");
    Serial.println(stepper.getStep());
#endif
    Serial.print("Volume set to: ");
    Serial.println(volume);
    Serial.print("Enable set to: ");
    Serial.println(enable);
    Serial.print("SelectedAudioFile set to: ");
    Serial.println(selectedAudioFile);

#if USE_STOPPER
    resetPosition();
#else
    if(stepper.resetToZeroStep()){
      writeState();
    }
#endif
  } else {
    Serial.print(stateFilename);
    Serial.println(" does not exist");
  }
}

void readConfig() {
  if (SD.exists(configFilename)) {
    DynamicJsonDocument doc(2048);
    Serial.println("Reading Config");
    configFile = SD.open(configFilename);
    deserializeJson(doc, configFile);
    configFile.close();
    wifiSSID = doc["wifiSSID"].as<String>();
    wifiPassword = doc["wifiPassword"].as<String>();
    Serial.print("wifiSSID: ");
    Serial.println(wifiSSID);
    Serial.print("wifiPassword: ");
    Serial.println(wifiPassword);

    rotationDirection = doc["rotationDirection"].as<byte>();
    Serial.print("rotationDirection: ");
    Serial.println(rotationDirection);

    awsAccessKey = doc["awsAccessKey"].as<String>();
    Serial.print("awsAccessKey: ");
    Serial.println(awsAccessKey);
    awsSecretKey = doc["awsSecretKey"].as<String>();
    sqsQueue = doc["sqsQueue"].as<String>();
    awsRegion = doc["awsRegion"].as<String>();
    Serial.print("sqsQueue: ");
    Serial.println(sqsQueue);
    Serial.print("awsRegion: ");
    Serial.println(awsRegion);

    ftpUsername = doc["ftpUsername"].as<String>();
    ftpPassword = doc["ftpPassword"].as<String>();
    Serial.print("ftpUsername: ");
    Serial.println(ftpUsername);
    Serial.print("ftpPassword: ");
    Serial.println(ftpPassword);


    
    gearSpokes = doc["gearSpokes"].as<int>();
    slideHoles = doc["slideHoles"].as<int>();
    maxSlidePercentage = doc["maxSlidePercentage"].as<double>();
    shuffleSlidePercentage = doc["shuffleSlidePercentage"].as<double>();
    maxVolume = doc["maxVolume"].as<double>();
  }else{
    Serial.println("Missing config file in the SD card.");
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
void blink(int number, bool longBlink=false){
  for(int i = 0; i < number; i++){
    digitalWrite(LED_PIN, HIGH);
    delay(longBlink?400:200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
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
      out->SetGain(((float)volume) / 250 * ((float)maxVolume) / 100);
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
    blink(2, true);
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
  WiFi.begin((char*)wifiSSID.c_str(), (char*)wifiPassword.c_str());
  blink(5);
}

void gotIPFromAP(WiFiEvent_t wifi_event, WiFiEventInfo_t wifi_info) {
  Serial.print("[+] Local ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

#if USE_STOPPER
  #define STEPS_BEFORE_CHECK 1
  bool isTouching() {
    return digitalRead(STOPPER_PIN) == HIGH;
  }
  void resetPosition(){
    while(!isTouching()){
      stepper.rotateByStep(-STEPS_BEFORE_CHECK*(rotationDirection?1:-1));
    }
  }
#endif

void initWiFi() {
  if(wifiSSID.isEmpty()) {
    Serial.println("Missing wifiSSID from the config file");
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.onEvent(connectedToAP, ARDUINO_EVENT_WIFI_STA_CONNECTED);
    WiFi.onEvent(disconnectedFromAP, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent(gotIPFromAP, ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.begin((char*)wifiSSID.c_str(), (char*)wifiPassword.c_str());
    Serial.print("Connecting to WiFi ..");
  }
}

void setup() {
  Serial.begin(115200);
  // Setup Pins
  stepper.initialize();
  // Audio should be before SD
  setupAudio();

#if USE_STOPPER
  pinMode(STOPPER_OUT_PIN, OUTPUT);
  digitalWrite(STOPPER_OUT_PIN, HIGH);
  pinMode(STOPPER_PIN, INPUT_PULLUP);
#endif



  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Setup SD Card
  while (!SD.begin()) {
    Serial.println("SD failed!");
    delay(1000);
  }
  delay(2000);
  // Load old state
  readState();
  readConfig();


  initWiFi();
  while (WiFi.status() != WL_CONNECTED) {
    blink(1, true);
    Serial.print(".");
  }
  Serial.println("");
  blink(5);

  getAudioFiles();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2, ntpServer3);

  delay(2000);

  if(!ftpUsername.isEmpty() && !ftpPassword.isEmpty())
    ftpSrv.begin(ftpUsername.c_str(), ftpPassword.c_str());  //username, wifiPassword for ftp.   (default 21, 50009 for PASV)
  else
    Serial.println("Missing ftpUsername or ftpPassword, not starting ftp server");
    
  runner.init();
  runner.addTask(timeTask);
  runner.addTask(audioFileCheck);
  runner.addTask(requestRead);
  runner.addTask(ftpTask);

  if(!awsAccessKey.isEmpty() && !awsSecretKey.isEmpty() && !awsRegion.isEmpty()){
    aws = new ESPAWSClient("sqs", awsAccessKey, awsSecretKey, awsRegion, "amazonaws.com");
    runner.addTask(sqsTask);
    sqsTask.enableDelayed(8000);
  }else
    Serial.println("Missing awsAccessKey or awsSecretKey or awsRegion, aws will nt be available.");

  timeTask.enableDelayed(10000);
  audioFileCheck.enableDelayed(10000);
  ftpTask.enable();
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(301, "text/plain", "Redirect");
    response->addHeader("Location", "/update");
    request->send(response);
  });

  AsyncElegantOTA.begin(&server);
  server.begin();

  Serial.print("Starting Run Loop ");
  Serial.println(xPortGetCoreID());
}


void runCuckoo() {
  Serial.println("Cuckoo Time");
  timeTask.disable();
  audioFileCheck.disable();
  ftpTask.disable();
  stepper.enable();
  double maxSlideRotation = (((double)slideHoles-1)/(double)gearSpokes)*360*maxSlidePercentage/100;
  double shuffleRotation = (((double)slideHoles-1)/(double)gearSpokes)*360*shuffleSlidePercentage/100;
  delay(100);
  stepper.rotate(maxSlideRotation*(rotationDirection?1:-1));
#if !USE_STOPPER
  writeState();
#endif
  playAudioFile();
  delay(500);
  stepper.rotate(-shuffleRotation*(rotationDirection?1:-1));
#if !USE_STOPPER
  writeState();
#endif
  delay(500);
  stepper.rotate(shuffleRotation*(rotationDirection?1:-1));
#if !USE_STOPPER
  writeState();
#endif
  delay(500);
  stepper.rotate(-shuffleRotation*(rotationDirection?1:-1));
#if !USE_STOPPER
  writeState();
#endif
  delay(500);
  stepper.rotate(shuffleRotation*(rotationDirection?1:-1));
#if !USE_STOPPER
  writeState();
#endif
  delay(500);
#if USE_STOPPER
  resetPosition();
#else
  stepper.rotate(-maxSlideRotation*(rotationDirection?1:-1));
  writeState();
#endif
  delay(100);
  Serial.println("End Cuckoo Time");
  stepper.disable();
  timeTask.enable();
  audioFileCheck.enable();
  ftpTask.enable();
}

void (*httpCallback)();
void httpReady() {
  if (aws->receiveReady()) {
    requestRead.disable();
    (*httpCallback)();
  }
}
void receiveMessageCallback() {
  AWSResponse resp = aws->receive();
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
        aws->doGet(sqsQueue, "Action=DeleteMessage&ReceiptHandle=" + urlEncode(receiptHandle));
        while (!aws->receiveReady())
          ;
        resp = aws->receive();
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
    Serial.println("GET "+sqsQueue+"?Action=ReceiveMessage&MaxNumberOfMessages=1&WaitTimeSeconds=20");
    aws->doGet(sqsQueue, "Action=ReceiveMessage&MaxNumberOfMessages=1&WaitTimeSeconds=20");
    httpCallback = &receiveMessageCallback;
    requestRead.enable();
    sqsTask.disable();
  }
}
void timeCallback() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    if(timeSkip == 0){
      Serial.println("Failed to obtain time");
      blink(3, true);
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2, ntpServer3);
    }
    timeSkip = (timeSkip + 1) % 6; // Every 60s

    return;
  }
  // Check if an hour has passed
  if (enable && timeinfo.tm_min == 0 && lastHour != timeinfo.tm_hour) {
    lastHour = timeinfo.tm_hour;
    sqsTask.disable();
    runCuckoo();
    sqsTask.enable();
  }
}

void ftpCallback(){
  ftpSrv.handleFTP();
}
void loop() {
  runner.execute();
}
