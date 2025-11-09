import paho.mqtt.client as mqtt

PORT = 1883
TOPIC = "plants/test"
PI = "raspberrypi"

def on_connect(client, userdata, flags, rc, properties=None):
    print("Connected:", rc)
    client.subscribe(TOPIC, qos=0)

def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode("utf-8", errors="replace")
    except Exception:
        payload = repr(msg.payload)
    print(f"[{msg.topic}] {payload}")

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)  # Paho v2 callback style
client.on_connect = on_connect
client.on_message = on_message

# If your broker requires auth:
# client.username_pw_set("user", "pass")

client.connect(PI, PORT, keepalive=60)
client.loop_forever()

