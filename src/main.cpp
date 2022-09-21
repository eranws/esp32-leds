#define CONFIG_USE_ONLY_LWIP_SELECT 1

#include "includes.h"

#include "song_offset_tracker.h"
#include "comms.h"
#include "pixels.h"
#include "sdcard.h"

TaskHandle_t Task1;
File aniFile;
uint8_t frameBuffer[headerSize];
int lastAnimationTime = -1;

void PrintCorePrefix()
{
  Serial.printf("[%d]", xPortGetCoreID());
}

void setup()
{
  Serial.begin(115200);
  disableCore0WDT();

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

unsigned long int frame = -1;
unsigned long loopCount = -1;
void loop()
{
  loopCount++;

  int32_t songStartTime;
  if (xQueueReceive(anListQueue, &songStartTime, 0) == pdTRUE)
  {
    PrintCorePrefix();
    Serial.println("received message on NewSongMsg queue");

    PrintCorePrefix();
    Serial.print("songStartTime: ");
    Serial.println(songStartTime);

    global_songStartTime = songStartTime;
  }

  int32_t songOffset = getGlobalTime();
  songOffset %= 10000; // loop 10 sec
  const int32_t fileSampleRateMillis = 10;
  int32_t currentFrame = songOffset / fileSampleRateMillis;

  if (frame != currentFrame)
  {
    if (currentFrame * headerSize != aniFile.position())
    {
      aniFile.seek(currentFrame * headerSize);
    }
    frame = currentFrame;
    if (aniFile.available() && aniFile.read(frameBuffer, headerSize) == headerSize)
    {
      renderFrame(frameBuffer, strip);
    }
  }

  // fps
  static float fps = 0.0f;
  static auto last = micros();
  auto now = micros();
  auto diff = now - last;
  float cfps = 1e6 / diff;
  fps = 0.9 * fps + 0.1 * cfps;
  last += diff;
  if (loopCount % 100 == 0)
  {
    Serial.printf("fps: %f  diff: %d \n", fps, diff);
  }

  strip.Show();

  vTaskDelay(5);
}
