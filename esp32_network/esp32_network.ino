#include <WiFi.h>
#include <PubSubClient.h>

// ============================================================
// Configuration
// ============================================================

// Wi-Fi
const char* WIFI_SSID = "enet";
const char* WIFI_PASSWORD = "zvnq8637)";

// Device - !!!CHANGE device ID if copy the code
const char* DEVICE_ID = "iot-course-node-irkutsk-teacher";

// MQTT broker
const char* MQTT_HOST = "broker.emqx.io";
const uint16_t MQTT_PORT = 1883;

// MQTT topics
const char* MQTT_STATUS_TOPIC =
    "iot-course-node-irkutsk-teacher/status";

const char* MQTT_HEARTBEAT_TOPIC =
    "iot-course-node-irkutsk-teacher/heartbeat";

// MQTT messages
const char* MQTT_ONLINE_MESSAGE = "online";
const char* MQTT_OFFLINE_MESSAGE = "offline";

// Timing
const unsigned long HEARTBEAT_INTERVAL_MS = 5000;
const unsigned long WIFI_RETRY_DELAY_MS = 1000;
const unsigned long MQTT_RETRY_DELAY_MS = 2000;


// ============================================================
// Global objects
// ============================================================

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

unsigned long lastHeartbeatTime = 0;


// ============================================================
// Wi-Fi
// ============================================================

void connectWiFi()
{
    Serial.print("Connecting to Wi-Fi: ");
    Serial.println(WIFI_SSID);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(WIFI_RETRY_DELAY_MS);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("Wi-Fi connected");

    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}


// ============================================================
// MQTT
// ============================================================

void connectMQTT()
{
    while (!mqttClient.connected())
    {
        Serial.print("Connecting to MQTT broker... ");

        bool connected = mqttClient.connect(
            DEVICE_ID,
            MQTT_STATUS_TOPIC,
            0,
            true,
            MQTT_OFFLINE_MESSAGE
        );

        if (connected)
        {
            Serial.println("connected");

            // Tell everyone that the device is online.
            mqttClient.publish(
                MQTT_STATUS_TOPIC,
                MQTT_ONLINE_MESSAGE,
                true
            );
        }
        else
        {
            Serial.print("failed, state = ");
            Serial.println(mqttClient.state());

            delay(MQTT_RETRY_DELAY_MS);
        }
    }
}


// ============================================================
// Heartbeat
// ============================================================

void publishHeartbeat()
{
    char message[64];

    snprintf(
        message,
        sizeof(message),
        "device=%s uptime=%lu",
        DEVICE_ID,
        millis() / 1000
    );

    mqttClient.publish(
        MQTT_HEARTBEAT_TOPIC,
        message
    );

    Serial.print("Published: ");
    Serial.println(message);
}


// ============================================================
// Arduino setup
// ============================================================

void setup()
{
    Serial.begin(115200);

    delay(1000);

    Serial.println();
    Serial.println("==============================");
    Serial.println(" IoT course - MQTT device");
    Serial.println("==============================");

    connectWiFi();

    mqttClient.setServer(
        MQTT_HOST,
        MQTT_PORT
    );

    connectMQTT();
}


// ============================================================
// Arduino main loop
// ============================================================

void loop()
{
    // Restore Wi-Fi connection if necessary.
    if (WiFi.status() != WL_CONNECTED)
    {
        connectWiFi();
    }

    // Restore MQTT connection if necessary.
    if (!mqttClient.connected())
    {
        connectMQTT();
    }

    // PubSubClient needs loop() to maintain the MQTT connection.
    mqttClient.loop();

    // Periodic heartbeat.
    unsigned long now = millis();

    if (now - lastHeartbeatTime >= HEARTBEAT_INTERVAL_MS)
    {
        lastHeartbeatTime = now;

        publishHeartbeat();
    }
}