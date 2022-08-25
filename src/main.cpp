#define CONFIG_USE_ONLY_LWIP_SELECT 1

#include <Arduino.h>
#include <WiFi.h>
#include <secrets.h>
#include <PubSubClient.h>

#include <animations/i_animation.h>
#include <render_utils.h>
#include <song_offset_tracker.h>
#include <animations_container.h>
#include <fs_manager.h>
#include <animation_factory.h>

#include "SPIFFS.h"

#ifndef NUM_LEDS
#warning NUM_LEDS not definded. using default value of 300
#define NUM_LEDS 300
#endif // NUM_LEDS

#ifndef MQTT_BROKER_PORT
#define MQTT_BROKER_PORT 1883
#endif // MQTT_BROKER_PORT

const unsigned int WD_TIMEOUT_MS = 2000;

HSV leds_hsv[NUM_LEDS];
RenderUtils renderUtils(leds_hsv, NUM_LEDS);
SongOffsetTracker songOffsetTracker;
AnimationsContainer animationsContainer;
FsManager fsManager;

TaskHandle_t Task1;

struct NewSongMsg
{
  bool onlyUpdateTime;
  const AnimationsContainer::AnimationsList *anList;
  int32_t songStartTime;
};
QueueHandle_t anListQueue;
const int anListQueueSize = 10;
int32_t lastReportedSongStartTime = 0;

QueueHandle_t deleteAnListQueue;
const int deleteAnListQueueSize = 10;

QueueHandle_t wdQueue;
const int wdQueueSize = 10;

void PrintCorePrefix()
{
  Serial.print("[");
  Serial.print(xPortGetCoreID());
  Serial.print("]: ");
}

StaticJsonDocument<40000> doc;

void Core0WDSend(unsigned int currMillis)
{
  static unsigned int lastWdSendTime = 0;
  if (currMillis - lastWdSendTime > WD_TIMEOUT_MS)
  {
    // Serial.println("[0] send wd msg from core 0 to core 1");
    int unused = 0;
    xQueueSend(wdQueue, &unused, 5);
    lastWdSendTime = currMillis;
  }
}

void Core0WdReceive(unsigned int currMillis)
{
  static unsigned int lastCore0WdReceiveTime = 0;
  int unused;
  if (xQueueReceive(wdQueue, &unused, 0) == pdTRUE)
  {
    lastCore0WdReceiveTime = currMillis;
  }

  unsigned int timeSinceWdReceive = currMillis - lastCore0WdReceiveTime;
  // Serial.print("[0] timeSinceWdReceive: ");
  // Serial.println(timeSinceWdReceive);
  if (timeSinceWdReceive > (3 * WD_TIMEOUT_MS))
  {
    ESP.restart();
  }
}

void CheckForSongStartTimeChange()
{
  if (!songOffsetTracker.IsSongPlaying())
    return;

  int32_t currStartTime = songOffsetTracker.GetSongStartTime();
  if (currStartTime == lastReportedSongStartTime)
    return;

  lastReportedSongStartTime = currStartTime;

  NewSongMsg msg;
  msg.onlyUpdateTime = true;
  msg.songStartTime = currStartTime;
  msg.anList = nullptr;
  Serial.println("updateing time of current song start");
  xQueueSend(anListQueue, &msg, portMAX_DELAY);
}

void SendAnListUpdate()
{
  NewSongMsg msg;
  if (songOffsetTracker.IsSongPlaying())
  {
    String currFileName = songOffsetTracker.GetCurrentFile();
    PrintCorePrefix();
    Serial.print("currFileName: ");
    Serial.println(currFileName);
    lastReportedSongStartTime = songOffsetTracker.GetSongStartTime();
    msg.songStartTime = lastReportedSongStartTime;
    msg.onlyUpdateTime = false;
    if (msg.songStartTime != 0)
    {
      msg.anList = animationsContainer.SetFromJsonFile(currFileName, doc);
    }
    else
    {
      PrintCorePrefix();
      Serial.println("ignoring an list update since song start time is not valid yet");
      msg.anList = nullptr;
    }
  }
  else
  {
    PrintCorePrefix();
    Serial.println("no song is playing");
    lastReportedSongStartTime = 0;
    msg.anList = nullptr;
    msg.songStartTime = 0;
    msg.onlyUpdateTime = false;
  }

  xQueueSend(anListQueue, &msg, portMAX_DELAY);
}

void SendStartTimeToRenderCore()
{
  if (!songOffsetTracker.IsSongPlaying())
    return;

  NewSongMsg msg;
  msg.onlyUpdateTime = true;
  msg.songStartTime = songOffsetTracker.GetSongStartTime();
  msg.anList = nullptr;
  PrintCorePrefix();
  Serial.println("updateing time of current song start");
  xQueueSend(anListQueue, &msg, portMAX_DELAY);
}

void callback(char *topic, byte *payload, unsigned int length)
{

  Serial.print("{{ MQTT Message on topic ");
  Serial.print("[");
  Serial.print(topic);
  Serial.print("] ");
  Serial.print("size: ");
  Serial.print(length);
  Serial.println("");
  Serial.println("payload:");
  Serial.print("  ");
  Serial.print((char*)payload);
  Serial.println();

  if (strncmp("animations/", topic, 11) == 0)
  {
    int songNameStartIndex = 11 + strlen(THING_NAME) + 1;
    String songName = String(topic + songNameStartIndex);
    fsManager.SaveToFs((String("/music/") + songName).c_str(), payload, length);

    if (songOffsetTracker.GetCurrentFile() == songName)
    {
      SendAnListUpdate();
    }
  }
  else if (strcmp("current-song", topic) == 0)
  {
    songOffsetTracker.HandleCurrentSongMessage((char *)payload);
    SendAnListUpdate();
  }
  else if (strncmp("objects-config", topic, 14) == 0)
  {
    fsManager.SaveToFs("/objects-config", payload, length);
    ESP.restart();
  }

  Serial.print("}} [0] done handling mqtt callback: ");
  Serial.println(topic);
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
    WiFi.begin(SSID, WIFI_PASSWORD);
    Serial.printf("Attempting to connect to SSID: ");
    Serial.printf(SSID);
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

WiFiClient net;
PubSubClient client(net);
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
    Serial.println("connected to message broker");
    client.subscribe((String("objects-config/") + String(THING_NAME)).c_str(), 1);
    client.subscribe("current-song", 1);
    client.subscribe((String("animations/") + String(THING_NAME) + String("/#")).c_str(), 1);
  }
  else
  {
    Serial.print("mqtt connect failed. error state:");
    Serial.println(client.state());
  }
}

void HandleObjectsConfig(File &f)
{

  int totalPixels = AnimationFactory::InitObjectsConfig(leds_hsv, doc, f);
  if (AnimationFactory::objectsMapErrorString == NULL)
  {
    Serial.print("total pixels: ");
    Serial.println(totalPixels);
  }
  else
  {
    Serial.print("objects map encountered an error while initializing: ");
    Serial.println(AnimationFactory::objectsMapErrorString);
  }
}

void DeleteAnListPtr()
{
  const AnimationsContainer::AnimationsList *ptrToDelete;
  if (xQueueReceive(deleteAnListQueue, &ptrToDelete, 0) == pdTRUE)
  {
    for (IAnimation *an : (*ptrToDelete))
    {
      delete an;
    }
    delete ptrToDelete;
  }
}

void SendMonitorMsg(char *buffer, size_t bufferSize)
{

  StaticJsonDocument<128> json_doc;
  json_doc["ThingName"] = THING_NAME;
  json_doc["Alive"] = true;
  json_doc["WifiSignal"] = WiFi.RSSI();
  json_doc["millis"] = millis();
  serializeJson(json_doc, buffer, bufferSize);
  // report to monitor what song is running, animations, etc.
}

void MonitorLoop(void *parameter)
{

  ConnectToWifi();
  songOffsetTracker.setup();
  unsigned int lastReportTime = millis();
  unsigned int lastMonitorTime = millis();
  for (;;)
  {
    DeleteAnListPtr();
    ConnectToWifi();
    ConnectToMessageBroker();
    unsigned int currTime = millis();
    Core0WDSend(currTime);
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

      Serial.print("[0] status millis: ");
      Serial.print(millis());
      Serial.print(" wifi:");
      Serial.print(WiFi.status() == WL_CONNECTED);
      Serial.print(" mqtt: ");
      Serial.println(client.connected());
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

NewSongMsg global_msg;

void setup()
{
  Serial.begin(115200);
  disableCore0WDT();

  global_msg.anList = nullptr;
  global_msg.songStartTime = 0;

  anListQueue = xQueueCreate(anListQueueSize, sizeof(NewSongMsg));
  deleteAnListQueue = xQueueCreate(deleteAnListQueueSize, sizeof(const AnimationsContainer::AnimationsList *));
  wdQueue = xQueueCreate(wdQueueSize, sizeof(int));

  Serial.print("Thing name: ");
  Serial.println(THING_NAME);

  fsManager.setup();
  renderUtils.Setup();

  File file = SPIFFS.open("/objects-config");
  if (file)
  {
    HandleObjectsConfig(file);
    file.close();
  }
  else
  {
    Serial.println("Failed to open objects config file for reading");
  }

  xTaskCreatePinnedToCore(
      MonitorLoop,   /* Function to implement the task */
      "MonitorTask", /* Name of the task */
      16384,         /* Stack size in words */
      NULL,          /* Task input parameter */
      0,             /* Priority of the task */
      &Task1,        /* Task handle. */
      0);            /* Core where the task should run */
}

// we keep this object once so we don't need to create the string on every loop
CurrentSongDetails songDetails;

unsigned int lastPrint1Time = millis();

void loop()
{

  unsigned long currentMillis = millis();
  Core0WdReceive(currentMillis);

  if (currentMillis - lastPrint1Time >= 5000)
  {
    // Serial.println("[1] core 1 alive");
    lastPrint1Time = currentMillis;
  }

  NewSongMsg newMsg;
  if (xQueueReceive(anListQueue, &newMsg, 0) == pdTRUE)
  {
    Serial.println("[1] received message on queue");
    Serial.print("[1] songStartTime: ");
    Serial.println(newMsg.songStartTime);
    Serial.print("[1] an list valid: ");
    Serial.println(newMsg.anList != nullptr);
    Serial.println(newMsg.onlyUpdateTime);

    if (newMsg.onlyUpdateTime)
    {
      Serial.print("[1] only update time: ");
      global_msg.songStartTime = newMsg.songStartTime;
    }
    else
    {
      if (global_msg.anList != nullptr)
      {
        Serial.println("sending animation ptr for deleteing to core 0");
        xQueueSend(deleteAnListQueue, &global_msg.anList, portMAX_DELAY);
      }
      global_msg = newMsg;
    }
  }

  renderUtils.Clear();

  bool hasValidSong = global_msg.anList != nullptr;
  if (hasValidSong)
  {
    int32_t songOffset = ((int32_t)(currentMillis)) - global_msg.songStartTime;
    const AnimationsContainer::AnimationsList *currList = global_msg.anList;
    // Serial.print("number of animations: ");
    // Serial.println(currList->size());
    // Serial.print("song offset: ");
    // Serial.println(songOffset);
    for (AnimationsContainer::AnimationsList::const_iterator it = currList->begin(); it != currList->end(); it++)
    {
      IAnimation *animation = *it;
      if (animation != nullptr && animation->IsActive(songOffset))
      {
        animation->Render((unsigned long)songOffset);
      }
    }
  }
  // unsigned long renderLoopTime = millis() - currentMillis;
  // Serial.print("loop time ms: ");
  // Serial.println(renderLoopTime);

  // if 'EN' pressed:
  const int BOOT_PIN = 0;
  if (digitalRead(BOOT_PIN) == LOW)
  {
    renderUtils.ShowTestPattern();
  }
  else
  {
    renderUtils.Show();
  }

  vTaskDelay(5);
}
