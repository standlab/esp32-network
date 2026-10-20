#include <WiFi.h>
#include <PubSubClient.h>
#include <string.h>
#include <stdlib.h>

// ============================================================
// Configuration: change these constants for your device
// ============================================================

// Wi-Fi
const char* WIFI_SSID = "enet";
const char* WIFI_PASSWORD = "mhuw5924?";

// MQTT
const char* DEVICE_ID = "iot-course-node-01";
const char* MQTT_HOST = "broker.emqx.io";
constexpr uint16_t MQTT_PORT = 1883;
const char* MQTT_TOPIC_PREFIX = "iot-course";
const char* MQTT_ONLINE_MESSAGE = "online";
const char* MQTT_OFFLINE_MESSAGE = "offline";
const char* MQTT_RESYNC_COMMAND = "resync";

// Hardware: 3 separate LEDs with individual resistors to GND
constexpr uint8_t RED_LED_PIN = 4;
constexpr uint8_t BLUE_LED_PIN = 2;
constexpr uint8_t GREEN_LED_PIN = 3;
constexpr bool LED_ACTIVE_HIGH = true;

// Intervals and buffers
constexpr unsigned long SERIAL_BAUD_RATE = 115200;
constexpr unsigned long WIFI_RETRY_MS = 7000;
constexpr unsigned long MQTT_RETRY_MS = 3000;
constexpr unsigned long HEARTBEAT_INTERVAL_MS = 5000;
constexpr unsigned long RED_BLINK_HALF_PERIOD_MS = 350;
constexpr unsigned long BLUE_FLASH_MS = 150;
constexpr unsigned long SYNC_ACK_MAX_AGE_MS = 10000;
constexpr size_t MQTT_TOPIC_BUFFER_SIZE = 128;
constexpr size_t MQTT_PAYLOAD_BUFFER_SIZE = 160;
constexpr size_t MQTT_INCOMING_BUFFER_SIZE = 40;

// ============================================================
// Runtime state
// ============================================================

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

char statusTopic[MQTT_TOPIC_BUFFER_SIZE];
char heartbeatTopic[MQTT_TOPIC_BUFFER_SIZE];
char ackTopic[MQTT_TOPIC_BUFFER_SIZE];
char commandTopic[MQTT_TOPIC_BUFFER_SIZE];

unsigned long lastWifiAttemptAt = 0;
unsigned long lastMqttAttemptAt = 0;
unsigned long lastHeartbeatAt = 0;
unsigned long lastBlueFlashAt = 0;
unsigned long lastAckAt = 0;

uint32_t lastSentSeq = 0;
bool blueFlashStarted = false;
bool synced = false;
bool resyncRequested = false;

// ============================================================
// Hardware indicators
// ============================================================

void setLed(uint8_t pin, bool enabled) {
  digitalWrite(pin, (enabled == LED_ACTIVE_HIGH) ? HIGH : LOW);
}

void updateIndicators(unsigned long now) {
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  const bool mqttConnected = wifiConnected && mqttClient.connected();

  // RED: blink if Wi-Fi is missing; steady if MQTT is missing.
  bool redOn = false;
  if (!wifiConnected) {
    redOn = (now / RED_BLINK_HALF_PERIOD_MS) % 2 == 0;
  } else if (!mqttConnected) {
    redOn = true;
  }

  // BLUE: short pulse each time a heartbeat is published.
  const bool blueOn = mqttConnected && blueFlashStarted &&
                      (now - lastBlueFlashAt < BLUE_FLASH_MS);

  // GREEN: current heartbeat was acknowledged by a Python app.
  const bool greenOn = mqttConnected && synced &&
                       (now - lastAckAt < SYNC_ACK_MAX_AGE_MS);

  setLed(RED_LED_PIN, redOn);
  setLed(BLUE_LED_PIN, blueOn);
  setLed(GREEN_LED_PIN, greenOn);
}

// ============================================================
// MQTT application protocol
// ============================================================

void publishHeartbeat(unsigned long now) {
  if (!mqttClient.connected()) return;

  const uint32_t nextSeq = lastSentSeq + 1;
  char payload[MQTT_PAYLOAD_BUFFER_SIZE];
  snprintf(payload, sizeof(payload),
           "{\"device_id\":\"%s\",\"seq\":%lu,\"uptime_s\":%lu}",
           DEVICE_ID,
           static_cast<unsigned long>(nextSeq),
           static_cast<unsigned long>(now / 1000));

  // The telemetry itself is NOT retained (only status is retained).
  if (mqttClient.publish(heartbeatTopic, payload, false)) {
    lastSentSeq = nextSeq;
    lastHeartbeatAt = now;
    lastBlueFlashAt = now;
    blueFlashStarted = true;

    // A NEW message needs a NEW acknowledgment.
    synced = false;
    Serial.print("Heartbeat published: ");
    Serial.println(payload);
  } else {
    synced = false;
    Serial.println("Heartbeat publish failed");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (length == 0 || length >= MQTT_INCOMING_BUFFER_SIZE) return;

  char text[MQTT_INCOMING_BUFFER_SIZE];
  memcpy(text, payload, length);
  text[length] = '\0';

  if (strcmp(topic, ackTopic) == 0) {
    // Application-level ACK is a decimal seq of the last heartbeat.
    char* end = nullptr;
    const unsigned long ackSeq = strtoul(text, &end, 10);
    if (text[0] != '\0' && *end == '\0' && lastSentSeq != 0 &&
        ackSeq == lastSentSeq) {
      synced = true;
      lastAckAt = millis();
      Serial.print("Heartbeat acknowledged, seq=");
      Serial.println(ackSeq);
    } else {
      Serial.print("Ignored stale/invalid ACK: ");
      Serial.println(text);
    }
  } else if (strcmp(topic, commandTopic) == 0) {
    if (strcmp(text, MQTT_RESYNC_COMMAND) == 0) {
      // Process outside the callback, after mqttClient.loop().
      resyncRequested = true;
      Serial.println("Remote resync requested");
    }
  }
}

// ============================================================
// Network connection/reconnection
// ============================================================

void tryReconnectWiFi(unsigned long now) {
  if (now - lastWifiAttemptAt < WIFI_RETRY_MS) return;
  lastWifiAttemptAt = now;
  Serial.println("Wi-Fi disconnected; trying to reconnect...");
  WiFi.reconnect();
}

void tryConnectMQTT(unsigned long now) {
  if (now - lastMqttAttemptAt < MQTT_RETRY_MS) return;
  lastMqttAttemptAt = now;

  Serial.print("Connecting to MQTT... ");
  const bool connected = mqttClient.connect(
      DEVICE_ID,
      statusTopic,
      0,                   // Last Will QoS
      true,                // Retain Last Will
      MQTT_OFFLINE_MESSAGE // Last Will content
  );

  if (!connected) {
    Serial.print("failed, state=");
    Serial.println(mqttClient.state());
    return;
  }

  Serial.println("connected");
  mqttClient.subscribe(ackTopic);
  mqttClient.subscribe(commandTopic);
  mqttClient.publish(statusTopic, MQTT_ONLINE_MESSAGE, true);

  synced = false;
  resyncRequested = false;
  publishHeartbeat(millis());
}

// ============================================================
// Arduino entry points
// ============================================================

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);

  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(BLUE_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  setLed(RED_LED_PIN, false);
  setLed(BLUE_LED_PIN, false);
  setLed(GREEN_LED_PIN, false);

  snprintf(statusTopic, sizeof(statusTopic), "%s/%s/status", MQTT_TOPIC_PREFIX, DEVICE_ID);
  snprintf(heartbeatTopic, sizeof(heartbeatTopic), "%s/%s/heartbeat", MQTT_TOPIC_PREFIX, DEVICE_ID);
  snprintf(ackTopic, sizeof(ackTopic), "%s/%s/sync/ack", MQTT_TOPIC_PREFIX, DEVICE_ID);
  snprintf(commandTopic, sizeof(commandTopic), "%s/%s/command", MQTT_TOPIC_PREFIX, DEVICE_ID);

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttemptAt = millis();
  lastMqttAttemptAt = millis() - MQTT_RETRY_MS;

  Serial.println("IoT course, stage 2: MQTT status LEDs + application ACK");
  Serial.print("Device: ");
  Serial.println(DEVICE_ID);
}

void loop() {
  const unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    synced = false;
    tryReconnectWiFi(now);

  } else if (!mqttClient.connected()) {
    synced = false;
    tryConnectMQTT(now);

  } else {
    mqttClient.loop();

    const unsigned long current = millis();

    if (resyncRequested) {
      resyncRequested = false;
      publishHeartbeat(current);

    } else if (current - lastHeartbeatAt >= HEARTBEAT_INTERVAL_MS) {
      publishHeartbeat(current);
    }
  }

  // Update hardware indicators once per iteration.
  updateIndicators(millis());
}