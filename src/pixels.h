// pixels.h
#include "includes.h"

NeoPixelBus<NeoRgbFeature, Neo800KbpsMethod> strip(PixelCount, PixelPin);
NeoGamma<NeoGammaTableMethod> colorGamma;

template <typename T_COLOR_FEATURE, typename T_METHOD>
void renderFrame(uint8_t *buffer, NeoPixelBus<T_COLOR_FEATURE, T_METHOD> &strip)
{
    for (int i = 0; i < strip.PixelCount(); i++)
    {
        int r = buffer[timeSize + 3 * i + 0];
        int g = buffer[timeSize + 3 * i + 1];
        int b = buffer[timeSize + 3 * i + 2];

        RgbColor color(r, g, b);
        color = colorGamma.Correct(color);
        strip.SetPixelColor(i, color);
    }
}