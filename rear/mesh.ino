#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>

// ================== CLONED LAPTOP MAC ==================
uint8_t clonedMac[6] = { 0x28, 0xDF, 0xEB, 0x06, 0xE6, 0x63 };

// ================== WIFI SETTINGS ==================
const char* apSSID     = "CalhounMeshRear";
const char* apPassword = "calhounpass";

// ================== GPIO ==================
// Rear radar center
#define PIN_RADAR_REAR_CENTER  4

// Rear ultrasonic backup
#define PIN_ULTRA_REAR_TRIG    7
#define PIN_ULTRA_REAR_ECHO    8

// Outputs (motor driver inputs)
#define PIN_ALARM_OUT          11
#define PIN_MIST_OUT           12
#define PIN_WATER_OUT          13
#define PIN_LED1_OUT           14
#define PIN_LED2_OUT           15

// Buzzer
#define PIN_BUZZER             3

// ================== MESH CONFIG ==================
#define MAX_NODES 10

uint8_t meshNodes[MAX_NODES][6] = {
  {0x24,0x6F,0x28,0xAA,0xBB,0xCC}, // CYD MASTER
  {0x28,0xDF,0xEB,0x06,0xE6,0x63}, // Front node / main repeater
  {0x28,0xDF,0xEB,0x06,0xE6,0x64}, // Extra node 2
  {0x28,0xDF,0xEB,0x06,0xE6,0x65}, // Extra node 3
};

int nodeCount = 4;

// ================== PACKET STRUCT ==================
typedef struct {
  uint8_t originMac[6];
  uint8_t eventType;
  int32_t value;
} MeshPacket;

// ================== ESP-NOW CALLBACKS ==================
void onEspNowSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Sent OK" : "Send FAIL");
}

void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != sizeof(MeshPacket)) return;

  MeshPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));

  // Forward to all other nodes except origin
  for (int i = 0; i < nodeCount; i++) {
    if (memcmp(meshNodes[i], pkt.originMac, 6) != 0) {
      esp_now_send(meshNodes[i], (uint8_t*)&pkt, sizeof(pkt));
    }
  }

  // Handle commands from CYD (actuators)
  if (pkt.eventType == 20) digitalWrite(PIN_ALARM_OUT, pkt.value); // exterior alarm
  if (pkt.eventType == 21) digitalWrite(PIN_MIST_OUT,  pkt.value); // misters
  if (pkt.eventType == 22) digitalWrite(PIN_WATER_OUT, pkt.value); // water pump
  if (pkt.eventType == 23) digitalWrite(PIN_LED1_OUT,  pkt.value); // LED1
  if (pkt.eventType == 24) digitalWrite(PIN_LED2_OUT,  pkt.value); // LED2
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

int readRearDistance() {
  digitalWrite(PIN_ULTRA_REAR_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_ULTRA_REAR_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_ULTRA_REAR_TRIG, LOW);

  long duration = pulseIn(PIN_ULTRA_REAR_ECHO, HIGH, 30000);
  if (duration == 0) return -1;

  return duration * 0.034 / 2;
}

// ================== BUZZER ==================
void beepPattern(int count, int speed = 80) {
  for (int i = 0; i < count; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(speed);
    digitalWrite(PIN_BUZZER, LOW);
    delay(speed);
  }
}

void progressiveRearBeep(int d) {
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

// ================== WIFI + ESP-NOW ==================
void setupWiFi() {
  esp_wifi_set_mac(WIFI_IF_STA, clonedMac);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSSID, apPassword);
}

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

  pinMode(PIN_RADAR_REAR_CENTER, INPUT);

  pinMode(PIN_ULTRA_REAR_TRIG, OUTPUT);
  pinMode(PIN_ULTRA_REAR_ECHO, INPUT);

  pinMode(PIN_ALARM_OUT, OUTPUT);
  pinMode(PIN_MIST_OUT,  OUTPUT);
  pinMode(PIN_WATER_OUT, OUTPUT);
  pinMode(PIN_LED1_OUT,  OUTPUT);
  pinMode(PIN_LED2_OUT,  OUTPUT);

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  setupWiFi();
  setupEspNow();

  Serial.println("Calhoun Rear Mesh Node Ready");
}

// ================== LOOP ==================
unsigned long lastSensorTime = 0;

void loop() {
  unsigned long now = millis();
  if (now - lastSensorTime > 200) {
    lastSensorTime = now;

    // Rear distance (backup ultrasonic)
    int d = readRearDistance();
    if (d > 0) {
      progressiveRearBeep(d);
      meshSend(10, d);  // eventType 10 = rear distance
    }

    // Rear radar center
    bool rCenter = readDigital(PIN_RADAR_REAR_CENTER);
    if (rCenter) {
      beepPattern(1, 80);  // 1 beep for rear center radar
    }
    meshSend(12, rCenter ? 1 : 0); // eventType 12 = rear radar center
  }
}
