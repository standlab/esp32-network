"""IoT course, stage 2: read telemetry and acknowledge it using paho-mqtt 2.x.

Usage:
    pip install 'paho-mqtt>=2,<3'
    python mqtt_sync_client.py
    python mqtt_sync_client.py --no-ack  # Observe green LED staying off
    python mqtt_sync_client.py --resync  # Ask device to publish now
"""

import argparse
import json
import uuid

import paho.mqtt.client as mqtt


# ============================================================
# Configuration (must match ESP32 sketch)
# ============================================================

MQTT_HOST = "broker.emqx.io"
MQTT_PORT = 1883
DEVICE_ID = "iot-course-node-01"
MQTT_TOPIC_PREFIX = "iot-course"

TOPIC_BASE = f"{MQTT_TOPIC_PREFIX}/{DEVICE_ID}"
STATUS_TOPIC = f"{TOPIC_BASE}/status"
HEARTBEAT_TOPIC = f"{TOPIC_BASE}/heartbeat"
ACK_TOPIC = f"{TOPIC_BASE}/sync/ack"
COMMAND_TOPIC = f"{TOPIC_BASE}/command"


def main() -> None:
    parser = argparse.ArgumentParser(description="IoT MQTT monitoring and ACK client")
    parser.add_argument("--no-ack", action="store_true", help="Do not confirm received telemetry")
    parser.add_argument("--resync", action="store_true", help="Request an immediate heartbeat after connect")
    args = parser.parse_args()

    # Unique client ID: multiple student monitors should not disconnect each other.
    client_id = f"iot-monitor-{uuid.uuid4().hex[:10]}"
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)

    def on_connect(client, userdata, flags, reason_code, properties):
        if reason_code != 0:
            print(f"MQTT connect failed: {reason_code}")
            return
        print(f"Connected to {MQTT_HOST}:{MQTT_PORT}")
        client.subscribe([(STATUS_TOPIC, 0), (HEARTBEAT_TOPIC, 0)])
        print(f"Subscribed to: {TOPIC_BASE}/{{status,heartbeat}}")
        if args.resync:
            client.publish(COMMAND_TOPIC, "resync", qos=0, retain=False)
            print("Sent command: resync")

    def on_message(client, userdata, message):
        text = message.payload.decode("utf-8", errors="replace")
        print(f"{message.topic}: {text}")
        if message.topic != HEARTBEAT_TOPIC or args.no_ack:
            return
        try:
            heartbeat = json.loads(text)
            if heartbeat["device_id"] != DEVICE_ID:
                print("  Ignored heartbeat with another device_id")
                return
            seq = int(heartbeat["seq"])
        except (ValueError, TypeError, KeyError, json.JSONDecodeError) as exc:
            print(f"  Cannot ACK malformed heartbeat: {exc}")
            return
        client.publish(ACK_TOPIC, str(seq), qos=0, retain=False)
        print(f"  ACK sent: seq={seq}")

    client.on_connect = on_connect
    client.on_message = on_message

    print("Press Ctrl+C to stop")
    try:
        client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
        client.loop_forever()
    except KeyboardInterrupt:
        print("\nStopped")
    finally:
        client.disconnect()


if __name__ == "__main__":
    main()