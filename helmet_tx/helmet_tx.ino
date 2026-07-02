#include <Wire.h>
#include <MPU6050.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <math.h>


uint8_t bikeAddress[6] = {0x00, 0x4B, 0x12, 0x34, 0xF9, 0x34};

#define MQ_SENSOR_PIN 34   
#define TRIG_PIN 5        
#define ECHO_PIN 18        

const int alcoholThreshold = 1000;   
const int helmetDistanceThreshold = 12;
const unsigned long sendInterval = 500; 

const int helmetSetConfirm = 1;
const int helmetClearConfirm = 1;
const int mqSmoothWindow = 4;           


MPU6050 mpu;
int16_t ax, ay, az;
float accMagnitude;
const float IMPACT_THRESHOLD = 1.8;
const float FALL_THRESHOLD = 0.7;
unsigned long lastFallTime = 0;

unsigned long previousMillis = 0;


int mqBuf[mqSmoothWindow > 1 ? mqSmoothWindow : 1];
int mqBufIdx = 0;
long mqBufSum = 0;
bool mqBufInited = false;

// Helmet confirmation state variables
int helmetSetCounter = 0;
int helmetClearCounter = 0;
bool helmetConfirmed = false;
unsigned long helmetDetectedAt = 0;


typedef struct struct_message {
  int alcoholValue;    
  int drunkStatus;    
  int helmetStatus;    
  int fallStatus;    
} struct_message;

struct_message dataToSend;


void OnDataSent(const esp_now_send_info_t *info, esp_now_send_status_t status) {
  Serial.print("Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}


long singleDuration() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 20000); 
  return duration;
}

long readDistanceCm_instant() {
  long dur = singleDuration();
  if (dur <= 0) return -1;
  return dur * 0.0343 / 2.0; 


int smoothMQRead() 
  int v = analogRead(MQ_SENSOR_PIN);
  if (mqSmoothWindow <= 1) return v;
  if (!mqBufInited) {
    mqBufSum = 0;
    for (int i = 0; i < mqSmoothWindow; ++i) {
      mqBuf[i] = v;
      mqBufSum += mqBuf[i];
    }
    mqBufIdx = 0;
    mqBufInited = true;
    return v;
  } else {
    mqBufSum -= mqBuf[mqBufIdx];
    mqBuf[mqBufIdx] = v;
    mqBufSum += v;
    mqBufIdx = (mqBufIdx + 1) % mqSmoothWindow;
    return (int)(mqBufSum / mqSmoothWindow);
  }
}


void setup() {
  Serial.begin(115200);
  delay(50);

  
  Wire.begin(21, 22);
  mpu.initialize();
  if (mpu.testConnection()) Serial.println("MPU6050 connected!");
  else Serial.println("MPU6050 NOT detected!");

  
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT_PULLDOWN);
  pinMode(MQ_SENSOR_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  
  WiFi.mode(WIFI_STA);
  delay(50);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
  } else {
    esp_now_register_send_cb(OnDataSent);
    Serial.println("ESP-NOW initialized");
  }

  
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, bikeAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;

  if (!esp_now_is_peer_exist(bikeAddress)) {
    if (esp_now_add_peer(&peerInfo) == ESP_OK)
      Serial.println("Bike peer added successfully");
    else
      Serial.println("Failed to add bike peer");
  }

  
  if (mqSmoothWindow > 1) {
    for (int i = 0; i < mqSmoothWindow; ++i) mqBuf[i] = analogRead(MQ_SENSOR_PIN);
    mqBufSum = 0;
    for (int i = 0; i < mqSmoothWindow; ++i) mqBufSum += mqBuf[i];
    mqBufInited = true;
  }

  Serial.println("Helmet System Initialization Complete.");
}


void loop() {
  unsigned long now = millis();
  if (now - previousMillis < sendInterval) return;
  previousMillis = now;

  
  long dist = readDistanceCm_instant();
  bool immediateWorn = (dist >= 0 && dist < helmetDistanceThreshold);

  if (immediateWorn) {
    helmetSetCounter++;
    helmetClearCounter = 0;
    if (helmetSetCounter >= helmetSetConfirm && !helmetConfirmed) {
      helmetConfirmed = true;
      helmetDetectedAt = millis();
      Serial.println("Helmet confirmed: WORN");
    }
    helmetSetCounter = min(helmetSetCounter, helmetSetConfirm);
  } else {
    helmetClearCounter++;
    helmetSetCounter = 0;
    if (helmetClearCounter >= helmetClearConfirm && helmetConfirmed) {
      helmetConfirmed = false;
      Serial.println("Helmet cleared: NOT WORN");
    }
    helmetClearCounter = min(helmetClearCounter, helmetClearConfirm);
  }

  
  int alcoholValueToSend = -1;
  int drunkFlag = 0;
  if (helmetConfirmed) {
    const unsigned long mqEnableDelayMs = 500;
    if (millis() - helmetDetectedAt >= mqEnableDelayMs) {
      int breathRaw = smoothMQRead();
      alcoholValueToSend = breathRaw;
      drunkFlag = (alcoholValueToSend > alcoholThreshold) ? 1 : 0;
      Serial.printf("MQ3 Sensor: %d | Status: %s\n", breathRaw, drunkFlag ? "DRUNK" : "SAFE");
    }
  } else {
    if (mqSmoothWindow > 1) smoothMQRead(); 
  }

  
  int fallDetected = 0;
  if (helmetConfirmed) {
    mpu.getAcceleration(&ax, &ay, &az);
    float accX = ax / 16384.0;
    float accY = ay / 16384.0;
    float accZ = az / 16384.0;
    accMagnitude = sqrt(accX * accX + accY * accY + accZ * accZ);

    if ((accMagnitude > IMPACT_THRESHOLD || accMagnitude < FALL_THRESHOLD) &&
        (millis() - lastFallTime > 2000)) {
      fallDetected = 1;
      lastFallTime = millis();
      Serial.printf("ALERT: Fall/Impact detected! | Vector Force: %.2f g\n", accMagnitude);
    }
  }

 
  dataToSend.alcoholValue = alcoholValueToSend;
  dataToSend.drunkStatus = drunkFlag;
  dataToSend.helmetStatus = helmetConfirmed ? 1 : 0;
  dataToSend.fallStatus = fallDetected;

  esp_err_t res = esp_now_send(bikeAddress, (uint8_t *)&dataToSend, sizeof(dataToSend));

  // Diagnostic Logs
  Serial.printf("Dist:%ld cm | Helmet:%s | Alcohol:%d | Crash:%d | Transmit:%s\n",
                dist, helmetConfirmed ? "WORN" : "NOT", alcoholValueToSend,
                fallDetected, (res == ESP_OK ? "OK" : "ERR"));
}