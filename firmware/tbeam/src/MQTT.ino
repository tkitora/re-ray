#include <WiFi.h>
#include <PubSubClient.h>

// Wi-Fi設定
const char* ssid = "ECCcomp2";
const char* password = "4Emah5LdS";

// MQTTサーバー設定
const char* mqtt_server = "10.200.4.222";
const int mqtt_port = 1883;

// MQTTの送信先
const char* topic = "guide01/test";

WiFiClient espClient;
PubSubClient mqtt(espClient);

// Wi-FiとMQTTの初期設定
void mqttSetup() {
  WiFi.begin(ssid, password);

  Serial.println("[WiFi] 接続開始");

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < 15000) {
    delay(500);
    Serial.println("[WiFi] 接続中...");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] 接続失敗");
    return;
  }

  Serial.println("[WiFi] 接続成功");
  Serial.println(WiFi.localIP());

  mqtt.setServer(mqtt_server, mqtt_port);
}

// MQTT接続と送信
void mqttConnect() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) return;

  if (mqtt.connect("M5StampS3")) {
    Serial.println("[MQTT] 接続成功");

    if (mqtt.publish(topic, "Hello from M5StampS3")) {
      Serial.println("[MQTT] 送信成功");
    } else {
      Serial.println("[MQTT] 送信失敗");
    }
  } else {
    Serial.print("[MQTT] 接続失敗 state=");
    Serial.println(mqtt.state());
  }
}

// MQTT通信を維持する
void mqttLoop() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (!mqtt.connected()) {
    static unsigned long lastAttempt = 0;

    if (lastAttempt == 0 || millis() - lastAttempt >= 5000) {
      lastAttempt = millis();
      mqttConnect();
    }
  }
}