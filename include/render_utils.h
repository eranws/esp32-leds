#ifndef __RENDER_UTILS_H__
#define __RENDER_UTILS_H__

#include <hsv.h>

#include <NeoPixelBus.h>
#include <secrets.h>

#define DATA_PIN 2
#define COLOR_ORDER NeoRgbFeature // can also be NeoGrbFeature

class RenderUtils
{

public:
    RenderUtils(HSV leds_hsv[], int numberOfLeds) : m_numberOfLeds(numberOfLeds),
                                                    m_leds_hsv(leds_hsv),
                                                    m_leds_rgb(numberOfLeds, DATA_PIN)
    {
    }

public:
    void Setup();
    void Show();
    void Clear();
    void ShowTestPattern();

private:
    int m_numberOfLeds = 0;
    HSV *m_leds_hsv;
    NeoPixelBus<COLOR_ORDER, Neo800KbpsMethod> m_leds_rgb;
};

#endif // __RENDER_UTILS_H__
