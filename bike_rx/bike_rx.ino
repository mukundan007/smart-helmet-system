/*
   Smart Bike Side (Receiver) - Final Production Code
   - I2C LCD Data Streaming Interface (16x2 Display)
   - Dynamic top-line marquee text scrolling logic
   - Direct Ignition and Intermittent Buzzer control (Active-Low Safety Configuration)
   - Emergency Timeout Fallback (Kills engine if helmet signal cuts out)
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <esp_now.h>
#include <WiFi.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);
#define DISPLAY_W 16

// Unified ESP-NOW Message Data Structure
typedef struct struct_message {
  int alcoholValue;
  int drunkStatus;
  int helmetStatus;
  int fallStatus;
} struct_message;

struct_message receivedData;

// Hardware Pin Configuration
#define RELAY_MOTOR_PIN 27   // Relay for Bike Ignition Circuit
#define RELAY_BUZZER_PIN 26  // Relay for Alert Buzzer
#define RELAY_ACTIVE_LOW true

// Safety Timeout Settings
unsigned long lastPacketTime = 0;
const unsigned long helmetTimeout = 3000; // 3-second link cutoff limit

// Marquee Text Variables
String scrollMsgTop = "";
String paddedTop = "";
int scrollPosTop = 0;
unsigned long lastScrollTime = 0;
int scrollDelay = 250;

// State Machines for Accidents & Indicators
bool fallDetected = false;
unsigned long lastBeepTime = 0;
bool buzzerState = false;
bool crashLCDVisible = false;
unsigned long lastCrashLCDTime = 0;
int crashLCDInterval = 500;

// ---------- String Layout Padding Helpers ----------
String padForMarquee(const String &s) {
  String pad = String(' ', DISPLAY_W);
  return s + pad;
}

void setTopMessage(const String &msg) {
  if (msg != scrollMsgTop) {
    scrollMsgTop = msg;
    paddedTop = padForMarquee(scrollMsgTop);
    scrollPosTop = 0;
    Serial.print("Interface Status Update: "); Serial.println(scrollMsgTop);
  }
}

String getMarqueeWindow(const String &padded, int pos) {
  int L = padded.length();
  if (L <= DISPLAY_W) {
    String t = padded;
    while (t.length() < DISPLAY_W) t += ' ';
    return t;
  }
  String out = "";
  for (int i = 0; i < DISPLAY_W; ++i) {
    int idx = (pos + i) % L;
    out += padded.charAt(idx);
  }
  return out;
}

// ---------- Relay State Management ----------
void setRelayState(uint8_t relayPin, bool on) {
  if (RELAY_ACTIVE_LOW)
    digitalWrite(relayPin, on ? LOW : HIGH);
  else
    digitalWrite(relayPin, on ? HIGH : LOW);
}

// ---------- ESP-NOW Packet Inbound Callback ----------
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int data_len) {
  if (!data || data_len != sizeof(struct_message)) return;
  memcpy(&receivedData, data, sizeof(receivedData));
  lastPacketTime = millis();

  Serial.printf("Data Received -> Alc:%d | Drunk:%d | Helmet:%d | Crash:%d\n",
                receivedData.alcoholValue, receivedData.drunkStatus, 
                receivedData.helmetStatus, receivedData.fallStatus);

  // Ignition Interlock Evaluation Logic
  if (!fallDetected) {
    if (receivedData.helmetStatus == 0) {
      setRelayState(RELAY_MOTOR_PIN, false); // Kill Engine
      setTopMessage("Helmet NOT worn");
    } 
    else if (receivedData.drunkStatus == 1 || receivedData.alcoholValue == -1) {
      setRelayState(RELAY_MOTOR_PIN, false); // Kill Engine
      setTopMessage("Helmet WORN - Alcohol HIGH");
    } 
    else {
      setRelayState(RELAY_MOTOR_PIN, true);  // Enable Engine
      setTopMessage("Helmet WORN - Alcohol SAFE");
    }
  }

  // Crash Processing State Machine
  if (receivedData.fallStatus == 1 && !fallDetected) {
    Serial.println("CRITICAL: Crash detected! Disabling engine, activating alarm.");
    fallDetected = true;
    crashLCDVisible = true;
    lastCrashLCDTime = millis();
    setRelayState(RELAY_MOTOR_PIN, false);
    buzzerState = true;
    setRelayState(RELAY_BUZZER_PIN, buzzerState);
  } 
  else if (receivedData.fallStatus == 0 && fallDetected) {
    Serial.println("System Alert Reset: Crash cleared.");
    fallDetected = false;
    crashLCDVisible = false;
    setRelayState(RELAY_BUZZER_PIN, false);
    buzzerState = false;
    lcd.clear();
  }
}

// ---------- Initialization Setup ----------
void setup() {
  Serial.begin(115200);
  delay(50);

  pinMode(RELAY_MOTOR_PIN, OUTPUT);
  pinMode(RELAY_BUZZER_PIN, OUTPUT);

  // Safe initial default state (Relays off)
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(RELAY_MOTOR_PIN, HIGH);
    digitalWrite(RELAY_BUZZER_PIN, HIGH);
  } else {
    digitalWrite(RELAY_MOTOR_PIN, LOW);
    digitalWrite(RELAY_BUZZER_PIN, LOW);
  }

  // UI Setup
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Bike System Boot");
  lcd.setCursor(0,1);
  lcd.print("Awaiting Helmet");

  lastPacketTime = millis();

  // Initialize Wireless Stack
  WiFi.mode(WIFI_STA);
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(OnDataRecv);
    Serial.println("ESP-NOW Stack Bound Successfully.");
  } else {
    Serial.println("Fatal: ESP-NOW initialization failed.");
  }

  setTopMessage("Waiting for helmet data");
  lastScrollTime = millis();
  Serial.println("Bike Control System Operational.");
}

// ---------- Main Processing Loop ----------
void loop() {
  unsigned long currentMillis = millis();

  // Link Supervision: Check if transmitter timed out
  if (currentMillis - lastPacketTime > helmetTimeout) {
    setRelayState(RELAY_MOTOR_PIN, false);
    setTopMessage("Helmet Not Connected");
  }

  // UI Loop Split: Emergency Render vs Normal Telemetry Render
  if (fallDetected) {
    // Intermittent alarm beeps
    if (currentMillis - lastBeepTime >= 300) {
      lastBeepTime = currentMillis;
      buzzerState = !buzzerState;
      setRelayState(RELAY_BUZZER_PIN, buzzerState);
    }

    // High-visibility screen toggle animation
    if (currentMillis - lastCrashLCDTime >= (unsigned long)crashLCDInterval) {
      lastCrashLCDTime = currentMillis;
      crashLCDVisible = !crashLCDVisible;
      lcd.clear();
      if (crashLCDVisible) {
        lcd.setCursor(0,0);
        lcd.print("CRASH DETECTED");
        lcd.setCursor(0,1);
        lcd.print("ENGINE SHUT DOWN");
      }
    }
  } 
  else {
    // Normal Runtime Telemetry HUD
    bool relayOn = (RELAY_ACTIVE_LOW ? digitalRead(RELAY_MOTOR_PIN) == LOW : digitalRead(RELAY_MOTOR_PIN) == HIGH);
    String bottom = "";

    bottom += (receivedData.helmetStatus == 1 ? "H:ON " : "H:OFF ");
    if (receivedData.alcoholValue >= 0)
      bottom += "A:" + String(receivedData.alcoholValue);
    else
      bottom += "A:NA";
    
    bottom += relayOn ? " RUN" : " STOP";
    while (bottom.length() < DISPLAY_W) bottom += ' ';

    // Shift text window down the string array sequentially
    if (currentMillis - lastScrollTime >= (unsigned long)scrollDelay) {
      lastScrollTime = currentMillis;
      if (paddedTop.length() < DISPLAY_W)
        paddedTop = padForMarquee(scrollMsgTop);
        
      String window = getMarqueeWindow(paddedTop, scrollPosTop);
      lcd.setCursor(0,0);
      lcd.print(window);
      lcd.setCursor(0,1);
      lcd.print(bottom);
      
      scrollPosTop++;
      if (scrollPosTop >= paddedTop.length()) scrollPosTop = 0;
    }
  }

  delay(10); // System pacing delay
}