#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <esp_now.h>

// ================== CLONED LAPTOP MAC ==================
uint8_t clonedMac[6] = { 0x28, 0xDF, 0xEB, 0x06, 0xE6, 0x63 };

// ================== WIFI SETTINGS ==================
const char* apSSID     = "CalhounRepeater";
const char* apPassword = "calhounpass";

const char* staSSID    = "";
const char* staPass    = "";

// ================== GPIO LABELS ==================
// Blind spot ultrasonic sensors (left/right)
#define PIN_ULTRA_LEFT       4
#define PIN_ULTRA_RIGHT      5

// Front radar modules (same module family)
#define PIN_RADAR_LEFT       6
#define PIN_RADAR_CENTER     7
#define PIN_RADAR_RIGHT      8

// Front distance ultrasonic
#define PIN_ULTRA_TRIG       9
#define PIN_ULTRA_ECHO       10

// Buzzer
#define PIN_BUZZER           3

// ================== ESP-NOW (MAIN CALHOUN DASHBOARD / CYD) ==================
uint8_t masterMac[6] = { 0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC }; // <-- PUT CYD ESP MAC HERE

typedef struct {
  uint8_t eventType;
  int32_t value;
} CalhounEvent;

esp_now_peer_info_t peerInfo;

// ================== WEB SERVER ==================
WebServer server(80);

// ================== STATE ==================
int  frontDistanceCm = 0;
bool leftBlindSpot   = false;
bool rightBlindSpot  = false;

bool radarLeft       = false;
bool radarCenter     = false;
bool radarRight      = false;

bool alertsEnabled   = true;

// ================== REAL ESP-NOW CALLBACK ==================
void onEspNowSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("ESP-NOW send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

// ================== ESP-NOW SEND ==================
void sendEvent(uint8_t type, int32_t value) {
  CalhounEvent evt;
  evt.eventType = type;
  evt.value     = value;
  esp_now_send(masterMac, (uint8_t*)&evt, sizeof(evt));
}

// ================== SENSOR READS ==================
bool readDigital(uint8_t pin) {
  return digitalRead(pin) == HIGH;
}

int readDistanceCm() {
  digitalWrite(PIN_ULTRA_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_ULTRA_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_ULTRA_TRIG, LOW);

  long duration = pulseIn(PIN_ULTRA_ECHO, HIGH, 30000);
  if (duration == 0) return -1;

  int distance = duration * 0.034 / 2;
  return distance;
}

// ================== BUZZER CONTROL ==================
void beepPattern(int count, int speed = 80) {
  for (int i = 0; i < count; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(speed);
    digitalWrite(PIN_BUZZER, LOW);
    delay(speed);
  }
}

void progressiveDistanceBeep(int d) {
  if (d <= 0) {
    digitalWrite(PIN_BUZZER, LOW);
    return;
  }

  static unsigned long lastBeep = 0;
  unsigned long now = millis();

  if (d > 150) {
    digitalWrite(PIN_BUZZER, LOW);
  } else if (d > 100) {
    if (now - lastBeep > 800) {
      beepPattern(1, 120);
      lastBeep = now;
    }
  } else if (d > 60) {
    if (now - lastBeep > 500) {
      beepPattern(1, 150);
      lastBeep = now;
    }
  } else if (d > 30) {
    if (now - lastBeep > 250) {
      beepPattern(1, 180);
      lastBeep = now;
    }
  } else {
    digitalWrite(PIN_BUZZER, HIGH);
  }
}

// ================== CONTROL PAGE ==================
String makeControlPage() {
  String html = "<html><body>";
  html += "<h1>Calhoun Repeater Front Module</h1>";

  html += "<p><b>Front Distance:</b> " + String(frontDistanceCm) + " cm</p>";
  html += "<p><b>Left Blind Spot:</b> " + String(leftBlindSpot ? "CAR" : "CLEAR") + "</p>";
  html += "<p><b>Right Blind Spot:</b> " + String(rightBlindSpot ? "CAR" : "CLEAR") + "</p>";

  html += "<p><b>Radar Left:</b> " + String(radarLeft ? "OBJECT" : "CLEAR") + "</p>";
  html += "<p><b>Radar Center:</b> " + String(radarCenter ? "OBJECT" : "CLEAR") + "</p>";
  html += "<p><b>Radar Right:</b> " + String(radarRight ? "OBJECT" : "CLEAR") + "</p>";

  html += "<h2>Alerts</h2>";
  html += "<p><a href='/toggleAlerts'>Toggle Alerts (Currently: " + String(alertsEnabled ? "ON" : "OFF") + ")</a></p>";

  html += "</body></html>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html", makeControlPage());
}

void handleToggleAlerts() {
  alertsEnabled = !alertsEnabled;
  if (!alertsEnabled) digitalWrite(PIN_BUZZER, LOW);
  server.send(200, "text/html", makeControlPage());
}

// ================== WIFI SETUP ==================
void setupWiFi() {
  esp_wifi_set_mac(WIFI_IF_STA, clonedMac);

  WiFi.mode(WIFI_AP_STA);

  if (strlen(staSSID) > 0) {
    WiFi.begin(staSSID, staPass);
  }

  WiFi.softAP(apSSID, apPassword);
}

// ================== ESP-NOW SETUP ==================
void setupEspNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_send_cb(onEspNowSent);

  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, masterMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add ESP-NOW peer");
  }
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_ULTRA_LEFT,  INPUT);
  pinMode(PIN_ULTRA_RIGHT, INPUT);

  pinMode(PIN_RADAR_LEFT,   INPUT);
  pinMode(PIN_RADAR_CENTER, INPUT);
  pinMode(PIN_RADAR_RIGHT,  INPUT);

  pinMode(PIN_ULTRA_TRIG, OUTPUT);
  pinMode(PIN_ULTRA_ECHO, INPUT);

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  setupWiFi();
  setupEspNow();

  server.on("/", handleRoot);
  server.on("/control", handleRoot);
  server.on("/toggleAlerts", handleToggleAlerts);
  server.begin();

  Serial.println("Calhoun Repeater Front Module Ready");
}

// ================== LOOP ==================
unsigned long lastSensorTime = 0;

void loop() {
  server.handleClient();

  unsigned long now = millis();
  if (now - lastSensorTime > 200) {
    lastSensorTime = now;

    // Front distance
    int d = readDistanceCm();
    if (d > 0) {
      frontDistanceCm = d;
      if (alertsEnabled) progressiveDistanceBeep(d);
      sendEvent(1, d);
    }

    // Blind spot ultrasonic
    leftBlindSpot  = readDigital(PIN_ULTRA_LEFT);
    rightBlindSpot = readDigital(PIN_ULTRA_RIGHT);

    if (alertsEnabled) {
      if (leftBlindSpot)  beepPattern(4, 60);  // LEFT = 4 beeps
      if (rightBlindSpot) beepPattern(2, 60);  // RIGHT = 2 beeps
    }

    sendEvent(2, leftBlindSpot  ? 1 : 0);
    sendEvent(3, rightBlindSpot ? 1 : 0);

    // Radar modules (front)
    radarLeft   = readDigital(PIN_RADAR_LEFT);
    radarCenter = readDigital(PIN_RADAR_CENTER);
    radarRight  = readDigital(PIN_RADAR_RIGHT);

    if (alertsEnabled && radarCenter) {
      beepPattern(1, 80);  // FRONT = 1 beep
    }

    sendEvent(4, radarLeft   ? 1 : 0);
    sendEvent(5, radarCenter ? 1 : 0);
    sendEvent(6, radarRight  ? 1 : 0);
  }
}
