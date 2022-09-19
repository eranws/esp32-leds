#include "includes.h"

#ifndef MQTT_BROKER_PORT
#define MQTT_BROKER_PORT 1883
#endif // MQTT_BROKER_PORT

void callback(char *topic, byte *payload, unsigned int length);

void ConnectToMessageBroker();
void ConnectToWifi();
void SendStartTimeToRenderCore();
void SendAnListUpdate();
void SendMonitorMsg(char *buffer, size_t bufferSize);

WiFiClient net;
PubSubClient client(net);
SongOffsetTracker songOffsetTracker;

int8_t wifiSignal() { return WiFi.RSSI(); }

void MonitorLoop(void *parameter)
{

    ConnectToWifi();
    songOffsetTracker.setup();
    unsigned int lastReportTime = millis();
    unsigned int lastMonitorTime = millis();
    for (;;)
    {
        ConnectToWifi();
        ConnectToMessageBroker();
        unsigned int currTime = millis();
        if (currTime - lastMonitorTime >= 1000)
        {
            char monitorMsg[128];
            SendMonitorMsg(monitorMsg, 128);
            client.publish(MONITOR_TOPIC, monitorMsg, true);
            lastMonitorTime = currTime;
        }
        if (currTime - lastReportTime >= 5000)
        {
            lastReportTime = currTime;
            Serial.printf("[%d]", xPortGetCoreID());

            Serial.print("status: millis: "), Serial.print(millis());
            Serial.print(" wifi:"), Serial.print(WiFi.status() == WL_CONNECTED);
            Serial.print(" mqtt:"), Serial.print(client.connected());
            Serial.print(" songOffset:"), Serial.print(getGlobalTime());
            Serial.println();
        }
        client.loop();
        bool clockChanged, clockFirstValid;
        songOffsetTracker.loop(&clockChanged, &clockFirstValid);
        if (clockChanged)
        {
            if (clockFirstValid)
            {
                SendAnListUpdate();
            }
            else
            {
                SendStartTimeToRenderCore();
            }
        }

        vTaskDelay(5);
    }
}

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
            delay(1000);
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

void callback(char *topic, byte *payload, unsigned int length)
{
    Serial.printf("[%d]", xPortGetCoreID());

    Serial.print("MQTT message on topic ");
    Serial.print(topic);
    Serial.print(" size: ");
    Serial.print(length);
    Serial.println("");

    if (strcmp("current-song", topic) == 0)
    {
        songOffsetTracker.HandleCurrentSongMessage((char *)payload);
        SendAnListUpdate();
    }

    Serial.print("done handling mqtt callback: ");
    Serial.println(topic);
}

void SendStartTimeToRenderCore()
{
    if (!songOffsetTracker.IsSongPlaying())
        return;

    int32_t songStartTime = songOffsetTracker.GetSongStartTime();
    Serial.printf("[%d]", xPortGetCoreID());

    Serial.println("updating time of current song start");
    xQueueSend(anListQueue, &songStartTime, portMAX_DELAY);
}

void SendAnListUpdate()
{
    Serial.println("SendAnListUpdate");

    int32_t songStartTime = 0;
    if (songOffsetTracker.IsSongPlaying())
    {
        Serial.println("IsSongPlaying");
        String currFileName = songOffsetTracker.GetCurrentFile();
        Serial.printf("[%d]", xPortGetCoreID());
        Serial.print("currFileName: ");
        Serial.println(currFileName);
        songStartTime = songOffsetTracker.GetSongStartTime();
        if (songStartTime != 0)
        {
            Serial.println("todo: update animation file");
        }
        else
        {
            Serial.printf("[%d]", xPortGetCoreID());
            Serial.println("ignoring an list update since song start time is not valid yet");
        }
    }
    else
    {
        Serial.printf("[%d]", xPortGetCoreID());
        Serial.println("no song is playing");
    }

    xQueueSend(anListQueue, &songStartTime, portMAX_DELAY);
}

void SendMonitorMsg(char *buffer, size_t bufferSize)
{
    StaticJsonDocument<128> json_doc;
    json_doc["ThingName"] = THING_NAME;
    json_doc["Alive"] = true;
    json_doc["WifiSignal"] = wifiSignal();
    json_doc["millis"] = millis();
    json_doc["global time"] = getGlobalTime();
    serializeJson(json_doc, buffer, bufferSize);
    // report to monitor what song is running, animations, etc.
}