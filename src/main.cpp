#define CONFIG_USE_ONLY_LWIP_SELECT 1

#include "includes.h"
#include "song_offset_tracker.h"
// #include "fs_manager.h"

#include "watchdog.h"
#include "comms.h"
#include "pixels.h"
#include "sdcard.h"

SongOffsetTracker songOffsetTracker;
// FsManager fsManager;

TaskHandle_t Task1;

File aniFile;

uint8_t frameBuffer[headerSize];
int lastAnimationTime = -1;

struct NewSongMsg
{
  int32_t songStartTime;
};
const int anListQueueSize = 10;
QueueHandle_t anListQueue = xQueueCreate(anListQueueSize, sizeof(NewSongMsg));

int32_t lastReportedSongStartTime = 0;

void PrintCorePrefix()
{
  Serial.print("[");
  Serial.print(xPortGetCoreID());
  Serial.print("]: ");
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
    msg.songStartTime = 0;
  }

  xQueueSend(anListQueue, &msg, portMAX_DELAY);
}

void SendStartTimeToRenderCore()
{
  if (!songOffsetTracker.IsSongPlaying())
    return;

  NewSongMsg msg;
  msg.songStartTime = songOffsetTracker.GetSongStartTime();
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

  if (strcmp("current-song", topic) == 0)
  {
    songOffsetTracker.HandleCurrentSongMessage((char *)payload);
    SendAnListUpdate();
  }

  Serial.print("done handling mqtt callback: ");
  Serial.println(topic);
}

int32_t global_songStartTime;
int32_t getGlobalTime(int32_t t = (int32_t)millis())
{
  return t - global_songStartTime;
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

  PrintCorePrefix();
  Serial.println("=== setup ===");
  Serial.print("Thing name: ");
  Serial.println(THING_NAME);

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

    global_songStartTime = newMsg.songStartTime;
  }

  int32_t songOffset = getGlobalTime();
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
