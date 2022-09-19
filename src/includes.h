#ifndef __INCLUDES_H__
#define __INCLUDES_H__

#include <WiFi.h>
#include <PubSubClient.h>
#include <Arduino.h>
#include <ArduinoJson.h>

#include "secrets.h"

const int anListQueueSize = 10;
static QueueHandle_t anListQueue = xQueueCreate(anListQueueSize, sizeof(int32_t));

static int32_t global_songStartTime = 0;
int32_t getGlobalTime(int32_t t = (int32_t)millis())
{
    return t - global_songStartTime;
}

#endif // __INCLUDES_H__
