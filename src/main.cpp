#define CONFIG_USE_ONLY_LWIP_SELECT 1

#include "includes.h"
#include "song_offset_tracker.h"
#include "fs_manager.h"

#include "SPIFFS.h"

#include "watchdog.h"
#include "comms.h"
#include "pixels.h"

SongOffsetTracker songOffsetTracker;
// AnimationsContainer animationsContainer;
FsManager fsManager;

TaskHandle_t Task1;

File aniFile;

uint8_t frameBuffer[headerSize];
int lastAnimationTime = -1;

struct NewSongMsg
{
  bool onlyUpdateTime;
  // const AnimationsList *anList;
  int32_t songStartTime;
};
QueueHandle_t anListQueue;
const int anListQueueSize = 10;
int32_t lastReportedSongStartTime = 0;

QueueHandle_t deleteAnListQueue;
const int deleteAnListQueueSize = 10;

void PrintCorePrefix()
{
  Serial.print("[");
  Serial.print(xPortGetCoreID());
  Serial.print("]: ");
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
  //! msg.anList = nullptr;
  Serial.println("updateing time of current song start");
  xQueueSend(anListQueue, &msg, portMAX_DELAY);
}

void SendAnListUpdate()
{
  Serial.println("SendAnListUpdate");
  NewSongMsg msg;
  if (songOffsetTracker.IsSongPlaying())
  {
    Serial.println("IsSongPlaying");
    String currFileName = songOffsetTracker.GetCurrentFile();
    PrintCorePrefix();
    Serial.print("currFileName: ");
    Serial.println(currFileName);
    lastReportedSongStartTime = songOffsetTracker.GetSongStartTime();
    msg.songStartTime = lastReportedSongStartTime;
    msg.onlyUpdateTime = false;
    if (msg.songStartTime != 0)
    {
      //! msg.anList = animationsContainer.SetFromJsonFile(currFileName, doc);
      Serial.println("todo: update animation file");
    }
    else
    {
      PrintCorePrefix();
      Serial.println("ignoring an list update since song start time is not valid yet");
      //! msg.anList = nullptr;
    }
  }
  else
  {
    PrintCorePrefix();
    Serial.println("no song is playing");
    lastReportedSongStartTime = 0;
    // msg.anList = nullptr;
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
  //! msg.anList = nullptr;
  PrintCorePrefix();
  Serial.println("updating time of current song start");
  xQueueSend(anListQueue, &msg, portMAX_DELAY);
}

void callback(char *topic, byte *payload, unsigned int length)
{
  PrintCorePrefix();
  Serial.print("MQTT message on topic ");
  Serial.print(topic);
  Serial.print(" size: ");
  Serial.print(length);
  Serial.println("");
  // Serial.println("payload:");
  // Serial.println((char*)payload);

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

  Serial.print("done handling mqtt callback: ");
  Serial.println(topic);
}

void ReadObjectsConfigFile(String filename)
{
  File file = SPIFFS.open(filename.c_str());
  if (file)
  {
    // int totalPixels = AnimationFactory::InitObjectsConfig(leds_hsv, doc, file);
    // if (AnimationFactory::objectsMapErrorString == NULL)
    if (file.size())
    {
      Serial.print("initialized object map. total pixels: ");
      // Serial.println(totalPixels);
    }
    else
    {
      Serial.print("objects map encountered an error while initializing: ");
      // Serial.println(AnimationFactory::objectsMapErrorString);
    }
  }
  else
  {
    Serial.println("Failed to open objects config file for reading");
  }
  file.close();
}

// const AnimationsList *global_anList;
int32_t global_songStartTime;

void SendMonitorMsg(char *buffer, size_t bufferSize)
{
  StaticJsonDocument<128> json_doc;
  json_doc["ThingName"] = THING_NAME;
  json_doc["Alive"] = true;
  json_doc["WifiSignal"] = wifiSignal();
  json_doc["millis"] = millis();
  // json_doc["global song start time"] = global_songStartTime;
  json_doc["global time"] = ((int32_t)(millis())) - global_songStartTime;
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
    // DeleteAnListPtr();
    // if (xQueueReceive(deleteAnListQueue, &ptrToDelete, 0) == pdTRUE)
    //   delete an;

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

      PrintCorePrefix();
      Serial.print("status: millis: ");
      Serial.print(millis());
      Serial.print(" wifi:");
      Serial.print(WiFi.status() == WL_CONNECTED);
      Serial.print(" mqtt:");
      Serial.print(client.connected());
      // Serial.print(" hasValidSong:");
      // Serial.print(global_anList != nullptr);
      Serial.print(" songOffset:");
      Serial.print(((int32_t)millis()) - global_songStartTime);
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

void readBufferFromFile(File &file, uint8_t *buf, uint32_t pos, size_t size)
{
  // Serial.print("Reading file: ");
  // Serial.println(path);

  // File file = fs.open(path);
  // if (!file)
  // {
  //   Serial.println("Failed to open file for reading");
  //   return;
  // }

  Serial.printf("Read from file: %i -> %i\n", pos, size);
  // disableCore1WDT();
  // esp_task_wdt_delete(0);
  uint32_t current_pos = file.position();
  file.seek(0, fs::SeekEnd);
  uint32_t end_of_file = file.position();
  file.seek(current_pos, fs::SeekSet);
  if (pos + size > end_of_file)
  {
    Serial.println("Trying to read beyond file");
    return;
  }
  if (file.seek(pos, fs::SeekSet))
  {
    file.read(buf, size);
  }
  else
  {
    current_pos = file.position();
    Serial.printf("Failed to read file: current %i, desired %i\n", current_pos, pos);
  }
  // enableCore1WDT();
  // esp_task_wdt_add(0);
  // file.close();
}

void beginSDCard()
{
  if (!SD.begin())
  {
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE)
  {
    Serial.println("No SD card attached");
    return;
  }

  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC)
  {
    Serial.println("MMC");
  }
  else if (cardType == CARD_SD)
  {
    Serial.println("SDSC");
  }
  else if (cardType == CARD_SDHC)
  {
    Serial.println("SDHC");
  }
  else
  {
    Serial.println("UNKNOWN");
  }

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
  Serial.printf("Total space: %lluMB\n", SD.totalBytes() / (1024 * 1024));
  Serial.printf("Used space: %lluMB\n", SD.usedBytes() / (1024 * 1024));
}

void setup()
{
  Serial.begin(115200);

  beginSDCard();

  aniFile = SD.open(filename);

  if (!aniFile)
  {
    Serial.println("Opening file failed");
    return;
  }

  delay(500);

  strip.Begin();
  strip.Show();

  disableCore0WDT();

  global_songStartTime = 0;

  anListQueue = xQueueCreate(anListQueueSize, sizeof(NewSongMsg));
  //! deleteAnListQueue = xQueueCreate(deleteAnListQueueSize, sizeof(const AnimationsList *));
  wdQueue = xQueueCreate(wdQueueSize, sizeof(int));

  PrintCorePrefix();
  Serial.println("=== setup ===");
  Serial.print("Thing name: ");
  Serial.println(THING_NAME);

  bool ok = false;
  PrintCorePrefix();
  Serial.print("fsManager.setup() ");
  ok = fsManager.setup();
  Serial.println(ok ? " ok " : "FAIL");

  // PrintCorePrefix();
  // Serial.println("ReadObjectsConfigFile ");
  // ReadObjectsConfigFile("/objects-config");

  xTaskCreatePinnedToCore(
      MonitorLoop,   /* Functirenon to implement the task */
      "MonitorTask", /* Name of the task */
      16384,         /* Stack size in words */
      NULL,          /* Task input parameter */
      0,             /* Priority of the task */
      &Task1,        /* Task handle. */
      0);            /* Core where the task should run */
}

unsigned int lastPrint1Time = millis();
unsigned int lastSecond = 0;


unsigned long int frame = 0;
void loop()
{
  unsigned long currentMillis = millis();
  Core0WdReceive(currentMillis);

  if (currentMillis - lastPrint1Time >= 5000)
  {
    PrintCorePrefix();
    Serial.println("core 1 alive");
    lastPrint1Time = currentMillis;
  }

  NewSongMsg newMsg;
  if (xQueueReceive(anListQueue, &newMsg, 0) == pdTRUE)
  {
    PrintCorePrefix();
    Serial.println("received message on NewSongMsg queue");

    PrintCorePrefix();
    Serial.print("songStartTime: ");
    Serial.println(newMsg.songStartTime);

    PrintCorePrefix();
    Serial.print("onlyUpdateTime: ");
    Serial.println(newMsg.onlyUpdateTime);

    if (newMsg.onlyUpdateTime)
    {
      PrintCorePrefix();
      Serial.print("only update time: ");
      global_songStartTime = newMsg.songStartTime;
    }
    else
    {
      // if (global_anList != nullptr)
      // {
      //   Serial.println("sending animation ptr for deleteing to core 0");
      //   xQueueSend(deleteAnListQueue, &global_anList, portMAX_DELAY);
      // }
      // global_anList = newMsg.anList;
      global_songStartTime = newMsg.songStartTime;
    }
  }

  int32_t songOffset = ((int32_t)(currentMillis)) - global_songStartTime;
  int32_t currentFrame = songOffset / fileSampleRateMs;
  // int32_t frames[] = {0, 1500000};
  // int32_t currentFrame = frame;
  // Serial.println(currentFrame);
  if (frame != currentFrame)
  {
    aniFile.seek(currentFrame * headerSize);
    frame = currentFrame;
    if (aniFile.available() && aniFile.read(frameBuffer, headerSize) == headerSize)
    {
      frame % 1000 ? 0 : Serial.println(frame);
      renderFrame(frameBuffer, strip);
      // delay(fileSampleRateMs);
    }
  }
  strip.Show();

  int animationTime = frameBuffer[0];

  // if (currentFrame % 1000 == 0)
  // {
  //   Serial.print("Animation time: ");
  //   Serial.println(animationTime);
  // }

  // if (animationTime != lastAnimationTime) {
  //   renderFrame(frameBuffer, strip);
  //   lastAnimationTime = animationTime;
  // }

  vTaskDelay(5);
}
