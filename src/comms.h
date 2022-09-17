#include "includes.h"

#ifndef MQTT_BROKER_PORT
#define MQTT_BROKER_PORT 1883
#endif // MQTT_BROKER_PORT

void callback(char *topic, byte *payload, unsigned int length);

int8_t wifiSignal() { return WiFi.RSSI(); }

WiFiClient net;
PubSubClient client(net);

void ConnectToWifi()
{

    if (WiFi.status() == WL_CONNECTED)
        return;

    while (true)
    {
        unsigned int connectStartTime = millis();
        WiFi.disconnect();
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        Serial.print("Attempting to connect to SSID: ");
        Serial.println(WIFI_SSID);
        while (millis() - connectStartTime < 10000)
        {
            Serial.print(".");
            Core0WDSend(millis());
            delay(1000);
            Core0WDSend(millis());
            if (WiFi.status() == WL_CONNECTED)
            {
                Serial.println("connected to wifi");
                return;
            }
        }
        Serial.println(" could not connect for 10 seconds. retry");
    }
}

void ConnectToMessageBroker()
{

    if (client.connected())
        return;

    client.setServer(MQTT_BROKER_IP, MQTT_BROKER_PORT);
    client.setCallback(callback);
    StaticJsonDocument<128> json_doc;
    json_doc["ThingName"] = THING_NAME;
    json_doc["Alive"] = false;
    char lastWillMsg[128];
    serializeJson(json_doc, lastWillMsg);
    Serial.println("connecting to mqtt");
    if (client.connect(THING_NAME, MONITOR_TOPIC, 1, true, lastWillMsg))
    {
        Serial.print("  connected to message broker");
        Serial.print(". monitor topic:");
        Serial.print(MONITOR_TOPIC);
        Serial.println();

        String objectTopic = "objects-config/" + String(THING_NAME);
        bool ok = client.subscribe(objectTopic.c_str(), 1);
        Serial.printf("  subscribed to topic [%s] %s\n", objectTopic.c_str(), ok ? " ok " : "FAIL");

        String currentSongTopic = "current-song";
        ok = client.subscribe(currentSongTopic.c_str(), 1);
        Serial.printf("  subscribed to topic [%s] %s\n", currentSongTopic.c_str(), ok ? " ok " : "FAIL");

        String animationsTopic = "animations/" + String(THING_NAME) + "/#";
        client.subscribe(animationsTopic.c_str(), 1);
        Serial.printf("  subscribed to topic [%s] %s\n", animationsTopic.c_str(), ok ? " ok " : "FAIL");
    }
    else
    {
        Serial.print("mqtt connect failed. error state:");
        Serial.println(client.state());
    }
}
