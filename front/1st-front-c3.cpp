#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <esp_now.h>

// ================== CLONED LAPTOP MAC ==================
uint8_t clonedMac[6] = { 0x28, 0xDF, 0xEB, 0x06, 0xE6, 0x63 };

// ================== WIFI SETTINGS ==================
const char* apSSID     = "CalhounRepeater";
const char* apPassword = "calhounpass";

// STA will connect to any hotspot you configure or scan for
// For now, leave blank or set to your current hotspot
const char* staSSID    = "";
const char* staPass    = "";

// ================== GPIO LABELS ==================
// Side blind spot microwave sensors
#define PIN_MICRO_SIDE_LEFT    4
#define PIN_MICRO_SIDE_RIGHT   5

// Front radar/presence sensors (left, center, right)
#define PIN_RADAR_FRONT_LEFT   6
#define PIN_RADAR_FRONT_CENTER 7
#define PIN_RADAR_FRONT_RIGHT  8

// Front distance sensor (ultrasonic)
#define PIN_ULTRA_TRIG         9
#define PIN_ULTRA_ECHO         10

// Inside buzzer
#define PIN_BUZZER_INSIDE      3

// Status LED (kept OFF)
#define PIN_STATUS_LED         LED_BUILTIN

// ================== ESP-NOW (MAIN CALHOUN DASHBOARD / CYD) ==================
uint8_t masterMac[6] = { 0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC }; // <-- PUT CYD ESP MAC HERE

typedef struct {
  uint8_t eventType;   // 1=front distance, 2=side left, 3=side right, 4=front left, 5=front center, 6=front right
  int32_t value;       // distance cm or 0/1 presence
} CalhounEvent;

esp_now_peer_info_t peerInfo;

// ================== WEB SERVER (PHONE CONTROL) ==================
WebServer server(80);

// ================== STATE ==================
int  frontDistanceCm      = 0;
bool sideLeftPresence     = false;
bool sideRightPresence    = false;
bool frontLeftPresence    = false;
bool frontCenterPresence  = false;
bool frontRightPresence   = false;

bool alertsEnabled        = true;

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
void setBuzzer(bool on) {
  digitalWrite(PIN_BUZZER_INSIDE, on ? HIGH : LOW);
}

// Progressive beep based on distance
void handleDistanceBeep(int d) {
  if (d <= 0) {
    setBuzzer(false);
    return;
  }

  // Example thresholds:
  // >150cm: no beep
  // 150-100: slow beep
  // 100-60: medium beep
  // 60-30: fast beep
  // <30: solid tone
  static unsigned long lastBeep = 0;
  unsigned long now = millis();

  if (d > 150) {
    setBuzzer(false);
  } else if (d > 100) {
    if (now - lastBeep > 800) {
      setBuzzer(true);
      delay(100);
      setBuzzer(false);
      lastBeep = now;
    }
  } else if (d > 60) {
    if (now - lastBeep > 500) {
      setBuzzer(true);
      delay(120);
      setBuzzer(false);
      lastBeep = now;
    }
  } else if (d > 30) {
    if (now - lastBeep > 250) {
      setBuzzer(true);
      delay(150);
      setBuzzer(false);
      lastBeep = now;
    }
  } else {
    // Very close: solid tone
    setBuzzer(true);
  }
}

// ================== CONTROL PAGE ==================
String makeControlPage() {
  String html = "<html><body>";
  html += "<h1>Calhoun Repeater Front Module</h1>";

  html += "<p><b>Front Distance:</b> " + String(frontDistanceCm) + " cm</p>";
  html += "<p><b>Side Left:</b> " + String(sideLeftPresence ? "CAR" : "CLEAR") + "</p>";
  html += "<p><b>Side Right:</b> " + String(sideRightPresence ? "CAR" : "CLEAR") + "</p>";
  html += "<p><b>Front Left:</b> " + String(frontLeftPresence ? "OBJECT" : "CLEAR") + "</p>";
  html += "<p><b>Front Center:</b> " + String(frontCenterPresence ? "OBJECT" : "CLEAR") + "</p>";
  html += "<p><b>Front Right:</b> " + String(frontRightPresence ? "OBJECT" : "CLEAR") + "</p>";

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
  if (!alertsEnabled) setBuzzer(false);
  server.send(200, "text/html", makeControlPage());
}

// ================== ESP-NOW CALLBACK ==================
void onEspNowSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // optional debug
}

// ================== WIFI SETUP (MAC CLONE + AP+STA) ==================
void setupWiFi() {
  // Clone MAC for STA
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

  pinMode(PIN_MICRO_SIDE_LEFT,    INPUT);
  pinMode(PIN_MICRO_SIDE_RIGHT,   INPUT);
  pinMode(PIN_RADAR_FRONT_LEFT,   INPUT);
  pinMode(PIN_RADAR_FRONT_CENTER, INPUT);
  pinMode(PIN_RADAR_FRONT_RIGHT,  INPUT);
  pinMode(PIN_ULTRA_TRIG,         OUTPUT);
  pinMode(PIN_ULTRA_ECHO,         INPUT);
  pinMode(PIN_BUZZER_INSIDE,      OUTPUT);
  pinMode(PIN_STATUS_LED,         OUTPUT);

  digitalWrite(PIN_STATUS_LED, LOW);
  setBuzzer(false);

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
      if (alertsEnabled) handleDistanceBeep(d);
      sendEvent(1, d);
    }

    // Side blind spots
    sideLeftPresence  = readDigital(PIN_MICRO_SIDE_LEFT);
    sideRightPresence = readDigital(PIN_MICRO_SIDE_RIGHT);

    if (alertsEnabled) {
      if (sideLeftPresence || sideRightPresence) {
        // brief beep for blind spot
        setBuzzer(true);
        delay(80);
        setBuzzer(false);
      }
    }

    sendEvent(2, sideLeftPresence  ? 1 : 0);
    sendEvent(3, sideRightPresence ? 1 : 0);

    // Front radar presence
    frontLeftPresence   = readDigital(PIN_RADAR_FRONT_LEFT);
    frontCenterPresence = readDigital(PIN_RADAR_FRONT_CENTER);
    frontRightPresence  = readDigital(PIN_RADAR_FRONT_RIGHT);

    sendEvent(4, frontLeftPresence   ? 1 : 0);
    sendEvent(5, frontCenterPresence ? 1 : 0);
    sendEvent(6, frontRightPresence  ? 1 : 0);
  }
}
