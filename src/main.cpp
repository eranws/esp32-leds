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
#include <num_leds.h>
#include <animation_factory.h>

#include "SPIFFS.h"

HSV leds_hsv[NUM_LEDS];
RenderUtils renderUtils(leds_hsv, NUM_LEDS);
SongOffsetTracker songOffsetTracker;
AnimationsContainer animationsContainer;
FsManager fsManager;

TaskHandle_t Task1;

struct NewSoneMsg {
  const AnimationsContainer::AnimationsList *anList;
  int32_t songStartTime;
};
QueueHandle_t anListQueue;
const int anListQueueSize = 5;

QueueHandle_t deleteAnListQueue;
const int deleteAnListQueueSize = 5;


StaticJsonDocument<40000> doc;

void SendAnListUpdate()
{
    NewSoneMsg msg;
    if(songOffsetTracker.IsSongPlaying()) {
      String currFileName = songOffsetTracker.GetCurrentFile();
      Serial.print("[0] currFileName: ");
      Serial.println(currFileName);
      msg.anList = animationsContainer.SetFromJsonFile(currFileName, doc);
      msg.songStartTime = songOffsetTracker.GetSongStartTime();
    }
    else {
      Serial.println("no song is playing");
      msg.anList = nullptr;
      msg.songStartTime = 0;
    }

    Serial.println("before queue send");
    xQueueSend(anListQueue, &msg, portMAX_DELAY);
    Serial.println("after queue send");
}

void callback(char* topic, byte* payload, unsigned int length) {

  Serial.print("Message arrived, ");
  Serial.print(length);
  Serial.print(" [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (strncmp("animations/", topic, 11) == 0) {
    int songNameStartIndex = 11 + strlen(THING_NAME) + 1;
    String songName = String(topic + songNameStartIndex);
    fsManager.SaveToFs((String("/music/") + songName).c_str(), payload, length);

    if(songOffsetTracker.GetCurrentFile() == songName) {
      SendAnListUpdate();
    }
    
  } else if(strcmp("current-song", topic) == 0) {
    songOffsetTracker.HandleCurrentSongMessage((char *)payload);
    SendAnListUpdate();

  } else if(strncmp("objects-config", topic, 14) == 0) {
    fsManager.SaveToFs("/objects-config", payload, length);
    ESP.restart();
  }

  Serial.print("[0] done handling mqtt callback: ");
  Serial.println(topic);
}

void ConnectToWifi() {

  if (WiFi.status() == WL_CONNECTED)
    return;

  while(true) {
    unsigned int connectStartTime = millis();
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.begin(SSID, WIFI_PASSWORD);
    Serial.printf("Attempting to connect to SSID: ");
    Serial.printf(SSID);
    while (millis() - connectStartTime < 10000)
    {
        Serial.print(".");
        delay(1000);
        if(WiFi.status() == WL_CONNECTED) {
          Serial.println("connected to wifi");
          return;
        }
    }
    Serial.println(" could not connect for 10 seconds. retry");
  }
}

WiFiClient net;
PubSubClient client(net);
void ConnectToMessageBroker() {

    if(client.connected())
      return;

    client.setServer(MQTT_HOST, 1883);
    client.setCallback(callback);
    Serial.println("connecting to mqtt");
    if(client.connect(THING_NAME)) {
        Serial.println("connected to message broker");
        client.subscribe((String("objects-config/") + String(THING_NAME)).c_str(), 1);
        client.subscribe("current-song", 1);
        client.subscribe((String("animations/") + String(THING_NAME) + String("/#")).c_str(), 1);
    }
    else {
        Serial.print("mqtt connect failed. error state:");
        Serial.println(client.state());
    }

}

void HandleObjectsConfig(File &f) {

  int totalPixels = AnimationFactory::InitObjectsConfig(leds_hsv, doc, f);
  if(AnimationFactory::objectsMapErrorString == NULL) {
    Serial.print("total pixels: ");
    Serial.println(totalPixels);
  } else {
    Serial.print("objects map encountered an error while initializing: ");
    Serial.println(AnimationFactory::objectsMapErrorString);
  }
}

void DeleteAnListPtr() {
  const AnimationsContainer::AnimationsList *ptrToDelete;
  if(xQueueReceive(deleteAnListQueue, &ptrToDelete, 0) == pdTRUE) {
    Serial.println("[0] deleteing animations list on core 0");
    delete ptrToDelete;
    Serial.println("[0] DONE deleteing animations list on core 0");
  }
}

void MonitorLoop( void * parameter) {
  ConnectToWifi();
  IPAddress timeServerIP(10, 0, 0, 200);
  songOffsetTracker.setup(timeServerIP, TIME_SERVER_PORT);
  unsigned int lastReportTime = millis();
  for(;;) {
    DeleteAnListPtr();
    ConnectToWifi();
    ConnectToMessageBroker();
    unsigned int currTime = millis();
    if(currTime - lastReportTime >= 5000) {
      Serial.print("[0] current millis: ");
      Serial.println(millis());
      Serial.print("[0] wifi client connected: ");
      Serial.println(WiFi.status() == WL_CONNECTED);
      Serial.print("[0] mqtt client connected: ");
      Serial.println(client.connected());
      lastReportTime = currTime;
    }
    client.loop();
    songOffsetTracker.loop();

    vTaskDelay(5);
  }
}

NewSoneMsg msg;

void setup() {
  Serial.begin(115200);
  disableCore0WDT();

  msg.anList = nullptr;
  msg.songStartTime = 0;

  anListQueue = xQueueCreate( anListQueueSize, sizeof(NewSoneMsg) );
  deleteAnListQueue = xQueueCreate( deleteAnListQueueSize, sizeof(const AnimationsContainer::AnimationsList *) );

  Serial.print("Thing name: ");
  Serial.println(THING_NAME);

  fsManager.setup();
  renderUtils.Setup();

  File file = SPIFFS.open("/objects-config");
  if(file){
      HandleObjectsConfig(file);
      file.close();
  }
  else {
      Serial.println("Failed to open objects config file for reading");
  }

  xTaskCreatePinnedToCore(
      MonitorLoop, /* Function to implement the task */
      "MonitorTask", /* Name of the task */
      16384,  /* Stack size in words */
      NULL,  /* Task input parameter */
      0,  /* Priority of the task */
      &Task1,  /* Task handle. */
      0); /* Core where the task should run */

}

// we keep this object once so we don't need to create the string on every loop
CurrentSongDetails songDetails;

unsigned int lastPrint1Time = millis();

void loop() {

  if(millis() - lastPrint1Time >= 5000) {
    Serial.println("[1] core 1 alive");
    lastPrint1Time = millis();
  }

  NewSoneMsg newMsg;
  if(xQueueReceive(anListQueue, &newMsg, 0) == pdTRUE) {
    Serial.println("[1] received message on queue");
    Serial.print("[1] songStartTime: ");
    Serial.println(newMsg.songStartTime);
    Serial.print("[1] an list valid: ");
    Serial.println(newMsg.anList != nullptr);

    if(msg.anList != nullptr) {
      Serial.println("sending animation ptr for deleteing to core 0");
      xQueueSend(deleteAnListQueue, msg.anList, portMAX_DELAY);
    }
    msg = newMsg;
  }

  renderUtils.Clear();

  unsigned long currentMillis = millis();
  bool hasValidSong = msg.anList != nullptr;
  if(hasValidSong) {
    int32_t songOffset = ((int32_t)(millis())) - msg.songStartTime;
    const AnimationsContainer::AnimationsList *currList = msg.anList;
    // Serial.print("number of animations: ");
    // Serial.println(currList->size());
    // Serial.print("song offset: ");
    // Serial.println(songOffset);
    for(AnimationsContainer::AnimationsList::const_iterator it = currList->begin(); it != currList->end(); it++) {
      IAnimation *animation = *it;
      if(animation != nullptr && animation->IsActive(songOffset)) {
        animation->Render((unsigned long)songOffset);
      }
    }
  }
  unsigned long renderLoopTime = millis() - currentMillis;
  // Serial.print("loop time ms: ");
  // Serial.println(renderLoopTime);

  renderUtils.Show();

  vTaskDelay(5);
}
