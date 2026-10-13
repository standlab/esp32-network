import paho.mqtt.client as mqtt


# ============================================================
# Configuration
# ============================================================

MQTT_HOST = "broker.emqx.io"
MQTT_PORT = 1883

DEVICE_ID = "iot-course-node-irkutsk-teacher"

MQTT_TOPIC = f"{DEVICE_ID}/#"


# ============================================================
# MQTT callbacks
# ============================================================

def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print("Connected to MQTT broker")

        client.subscribe(MQTT_TOPIC)

        print(f"Subscribed to: {MQTT_TOPIC}")

    else:
        print(f"Connection failed: {reason_code}")


def on_message(client, userdata, message):
    payload = message.payload.decode("utf-8")

    print(
        f"topic={message.topic} "
        f"payload={payload}"
    )


# ============================================================
# Main
# ============================================================

client = mqtt.Client(
    mqtt.CallbackAPIVersion.VERSION2
)

client.on_connect = on_connect
client.on_message = on_message

print(f"Connecting to {MQTT_HOST}:{MQTT_PORT}...")

client.connect(
    MQTT_HOST,
    MQTT_PORT,
    keepalive=60
)

client.loop_forever()