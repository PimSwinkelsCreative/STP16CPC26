#pragma once
#include <16bitPixelTypes.h>
#include <Arduino.h>

#define LEDDOTSPERDRIVER 16
#define BYTESPERDRIVER (2 * LEDDOTSPERDRIVER)

class LED1642GW {
private:
    uint16_t* leds;
    uint16_t nLedDots;
    uint8_t clkPin;
    uint8_t dataPin;
    uint8_t latchPin;
    int8_t pwmClockPin;
    uint32_t clkFrequency;
    uint8_t nLedDrivers;

    uint32_t clkPinBitmap;
    uint32_t dataPinBitmap;
    uint32_t latchPinBitmap;

    uint64_t lastSettingsUpdate;
    uint32_t settingUpdateInterval;

    void init();
    void setConfigRegister();
    void enableOutputs();

    void pulseClock();
    void setDataPin(bool value);
    void setLatchPin();
    void clearLatchPin();

public:
    // constructors:
    LED1642GW(RGBColor16* _rgbLedData, uint16_t _nRGBLeds, uint8_t _clkPin,
        uint8_t _dataPin, uint8_t _latchPin, int8_t _pwmClockPin = -1);
    LED1642GW(RGBWColor16* _rgbwLedData, uint16_t _nRGBWLeds, uint8_t _clkPin,
        uint8_t _dataPin, uint8_t _latchPin, int8_t _pwmClockPin = -1);
    LED1642GW(uint16_t* _ledData, uint16_t _nLedDots, uint8_t _clkPin,
        uint8_t _dataPin, uint8_t _latchPin, int8_t _pwmClockPin = -1);

    // startup:
    void startPWMClock();

    // setting led:
    void setLedTo(uint16_t ledIndex, struct RGBWColor16 color);
    void setLedTo(uint16_t ledIndex, struct RGBColor16 color);
    void setLedTo(uint16_t ledIndex, uint16_t brightness);

    // setting all leds:
    void setAllLedsTo(struct RGBWColor16 color);
    void setAllLedsTo(struct RGBColor16 color);
    void setAllLedsTo(uint16_t brightness);

    // clear all leds:
    void clearLeds();

    // write the ledData to the led drivers:
    void update();

    // function that sets the interval at which the driver config is being sent
    void setConfigUpdateInterval(uint32_t milliseconds);
};