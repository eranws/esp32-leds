// watch - dog.h
#include <Arduino.h>
const unsigned int WD_TIMEOUT_MS = 2000;

QueueHandle_t wdQueue;
const int wdQueueSize = 10;

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
    if (xQueueReceive(wdQueue, 0, 0) == pdTRUE)
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