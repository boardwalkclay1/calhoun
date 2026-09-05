#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <esp_now.h>

// ================== CLONED LAPTOP MAC ==================
uint8_t clonedMac[6] = { 0x28, 0xDF, 0xEB, 0x06, 0xE6, 0x63 };

// ================== WIFI SETTINGS ==================
const char* apSSID     = "CalhounMeshNode";
const char* apPassword = "calhounpass";

const char* staSSID    = "";
const char* staPass    = "";

// ================== GPIO LABELS ==================
// Blind spot ultrasonic sensors
#define PIN_ULTRA_LEFT       4
#define PIN_ULTRA_RIGHT      5

// Radar modules (front)
#define PIN_RADAR_LEFT       6
#define PIN_RADAR_CENTER     7
#define PIN_RADAR_RIGHT      8

// Front distance ultrasonic
#define PIN_ULTRA_TRIG       9
#define PIN_ULTRA_ECHO       10

// Buzzer
#define PIN_BUZZER           3

// ================== MESH CONFIG ==================
#define MAX_NODES 10

uint8_t meshNodes[MAX_NODES][6] = {
  {0x24,0x6F,0x28,0xAA,0xBB,0xCC}, // CYD MASTER
  {0x28,0xDF,0xEB,0x06,0xE6,0x63}, // Node 1 (your repeater)
  {0x28,0xDF,0xEB,0x06,0xE6,0x64}, // Node 2
  {0x28,0xDF,0xEB,0x06,0xE6,0x65}, // Node 3
  {0x28,0xDF,0xEB,0x06,0xE6,0x66}, // Node 4
};

int nodeCount = 5;

// ================== PACKET STRUCT ==================
typedef struct {
  uint8_t originMac[6];   // who created the packet
  uint8_t eventType;      // sensor type
  int32_t value;          // sensor value
} MeshPacket;

// ================== ESP-NOW CALLBACK ==================
void onEspNowSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("ESP-NOW send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

// ================== RECEIVE CALLBACK ==================
void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != sizeof(MeshPacket)) return;

  MeshPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));

  // Forward packet to all other nodes except origin
  for (int i = 0; i < nodeCount; i++) {
    if (memcmp(meshNodes[i], pkt.originMac, 6) != 0) {
      esp_now_send(meshNodes[i], (uint8_t*)&pkt, sizeof(pkt));
    }
  }

  Serial.println("Mesh packet forwarded.");
}

// ================== SEND PACKET ==================
void meshSend(uint8_t eventType, int32_t value) {
  MeshPacket pkt;

  esp_wifi_get_mac(WIFI_IF_STA, pkt.originMac);
  pkt.eventType = eventType;
  pkt.value     = value;

  for (int i = 0; i < nodeCount; i++) {
    esp_now_send(meshNodes[i], (uint8_t*)&pkt, sizeof(pkt));
  }
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
  esp_now_register_recv_cb(onEspNowRecv);

  for (int i = 0; i < nodeCount; i++) {
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(peer));
    memcpy(peer.peer_addr, meshNodes[i], 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
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

  Serial.println("Calhoun Mesh Node Ready");
}

// ================== LOOP ==================
unsigned long lastSensorTime = 0;

void loop() {
  unsigned long now = millis();
  if (now - lastSensorTime > 200) {
    lastSensorTime = now;

    // Front distance
    int d = readDistanceCm();
    if (d > 0) {
      progressiveDistanceBeep(d);
      meshSend(1, d);
    }

    // Blind spot ultrasonic
    bool leftBlindSpot  = readDigital(PIN_ULTRA_LEFT);
    bool rightBlindSpot = readDigital(PIN_ULTRA_RIGHT);

    if (leftBlindSpot)  beepPattern(4, 60);
    if (rightBlindSpot) beepPattern(2, 60);

    meshSend(2, leftBlindSpot  ? 1 : 0);
    meshSend(3, rightBlindSpot ? 1 : 0);

    // Radar modules
    bool radarLeft   = readDigital(PIN_RADAR_LEFT);
    bool radarCenter = readDigital(PIN_RADAR_CENTER);
    bool radarRight  = readDigital(PIN_RADAR_RIGHT);

    if (radarCenter) beepPattern(1, 80);

    meshSend(4, radarLeft   ? 1 : 0);
    meshSend(5, radarCenter ? 1 : 0);
    meshSend(6, radarRight  ? 1 : 0);
  }
}
