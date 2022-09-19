#include <NeoPixelBus.h>

// LEDs
typedef ColumnMajorAlternating180Layout MyPanelLayout;
const uint8_t PanelWidth = 10;  // 10
const uint8_t PanelHeight = 20; // 20
const uint8_t TileWidth = 1;    // laid out in 4 panels x 2 panels mosaic
const uint8_t TileHeight = 1;
const uint16_t PixelCount = PanelWidth * PanelHeight * TileWidth * TileHeight;
const uint8_t PixelPin = 2;

// const String filename = "/tstst1024.bin";
const String filename = "/tstst.bin";

const int32_t timeSize = 4;
const int32_t bufSize = PixelCount * 3;
const int32_t headerSize = bufSize + timeSize;
const int32_t fileSampleRateMs = 10;
