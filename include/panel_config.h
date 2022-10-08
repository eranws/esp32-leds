#include <NeoPixelBus.h>

// LEDs

// Panel
// typedef ColumnMajorAlternating180Layout MyPanelLayout;
// const uint8_t PanelWidth = 10;  // 10
// const uint8_t PanelHeight = 20; // 20
// const uint8_t TileWidth = 1;    // laid out in 4 panels x 2 panels mosaic
// const uint8_t TileHeight = 1;
// const uint16_t PixelCount = PanelWidth * PanelHeight * TileWidth * TileHeight;

// Trapez
// const uint16_t PixelCount = 20 * 10;       // Panel
// const uint16_t PixelCount = 70 + 100 + 70; // Trapez
const uint16_t PixelCount = 300; // catch-all

const uint8_t PixelPin = 2;

const int32_t timeSize = 4;
const int32_t bufSize = PixelCount * 3;
const int32_t headerSize = bufSize + timeSize;
