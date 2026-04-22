#include "LED1642GW.h"
#include "driver/i2s.h"

#include <SPI.h>

// declare the SPI interface for the LEDs
SPIClass* ledSPI = NULL;

LED1642GW::LED1642GW(uint16_t* _ledData, uint16_t _nLedDots, uint8_t _clkPin,
    uint8_t _dataPin, uint8_t _latchPin, int8_t _pwmClockPin,
    uint32_t _clkFrequency)
    : leds(_ledData)
    , nLedDots(_nLedDots)
    , clkPin(_clkPin)
    , dataPin(_dataPin)
    , latchPin(_latchPin)
    , pwmClockPin(_pwmClockPin)
    , clkFrequency(_clkFrequency)
{
    init();
}

LED1642GW::LED1642GW(RGBColor16* _rgbLedData, uint16_t _nRGBLeds, uint8_t _clkPin,
    uint8_t _dataPin, uint8_t _latchPin, int8_t _pwmClockPin,
    uint32_t _clkFrequency)
    : leds((uint16_t*)_rgbLedData)
    , nLedDots(_nRGBLeds * (sizeof(_rgbLedData[0]) / sizeof(_rgbLedData[0].r)))
    , clkPin(_clkPin)
    , dataPin(_dataPin)
    , latchPin(_latchPin)
    , pwmClockPin(_pwmClockPin)
    , clkFrequency(_clkFrequency)
{
    init();
}

LED1642GW::LED1642GW(RGBWColor16* _rgbwData, uint16_t _nRGBWLeds, uint8_t _clkPin,
    uint8_t _dataPin, uint8_t _latchPin, int8_t _pwmClockPin,
    uint32_t _clkFrequency)
    : leds((uint16_t*)_rgbwData)
    , nLedDots(_nRGBWLeds * (sizeof(_rgbwData[0]) / sizeof(_rgbwData[0].r)))
    , clkPin(_clkPin)
    , dataPin(_dataPin)
    , latchPin(_latchPin)
    , pwmClockPin(_pwmClockPin)
    , clkFrequency(_clkFrequency)
{
    init();
}

void LED1642GW::init()
{
    nLedDrivers = nLedDots / LEDDOTSPERDRIVER;
    if (nLedDots % LEDDOTSPERDRIVER > 0) {
        nLedDrivers++;
    }

    pinMode(clkPin, OUTPUT);
    pinMode(dataPin, OUTPUT);
    pinMode(latchPin, OUTPUT);
    digitalWrite(latchPin, LOW);

    setConfigRegister();
    enableOutputs();

    start();
}

void LED1642GW::setConfigRegister()
{
    // build the config register:
    uint16_t cfg = 0;
    cfg |= 0x003F; // CFG0..5 = max gain
    cfg |= (1 << 6); // CFG6 = high current range
    cfg |= (1 << 7); // CFG7 = normal mode
    cfg |= (1 << 13); // CFG13 = SDO delay enable

    for (int driver = nLedDrivers - 1; driver >= 0; driver--) {
        for (int i = 15; i >= 0; i--) {
            digitalWrite(dataPin, (cfg & 0x01 << i) >> i);

            if (driver == 0) {
                if (i < 7) {
                    digitalWrite(latchPin, HIGH);
                } else {
                    digitalWrite(latchPin, LOW);
                }
            }

            digitalWrite(clkPin, HIGH);
            digitalWrite(clkPin, LOW);
        }
    }
    digitalWrite(dataPin, LOW);
    digitalWrite(latchPin, LOW);
}

void LED1642GW::enableOutputs()
{
    for (int driver = nLedDrivers - 1; driver >= 0; driver--) {
        for (int i = 15; i >= 0; i--) {
            digitalWrite(dataPin, HIGH);

            if (driver == 0) {
                if (i < 2) {
                    digitalWrite(latchPin, HIGH);
                } else {
                    digitalWrite(latchPin, LOW);
                }
            }

            digitalWrite(clkPin, HIGH);
            digitalWrite(clkPin, LOW);
        }
    }
    digitalWrite(dataPin, LOW);
    digitalWrite(latchPin, LOW);
}

void LED1642GW::start()
{
    // use the I2S clock as the 10MHz PWMclock:
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 625000, // clock is 16 times this value, so 10MHz
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 2,
        .dma_buf_len = 8,
        .use_apll = true,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = pwmClockPin,
        .ws_io_num = I2S_PIN_NO_CHANGE,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
}

void LED1642GW::update()
{
    for (int channel = 15; channel >= 0; channel--) {
        for (int driver = nLedDrivers - 1; driver >= 0; driver--) {
            int nodeIndex = driver * LEDDOTSPERDRIVER + channel;
            for (int i = 15; i >= 0; i--) {
                digitalWrite(dataPin, (leds[nodeIndex] & 0x01 << i) >> i);
                if (driver == 0) {
                    if (channel > 0) {
                        if (i == 3) {
                            digitalWrite(latchPin, HIGH);
                        }
                    } else {
                        if (i == 5) {
                            digitalWrite(latchPin, HIGH);
                        }
                    }
                }
                digitalWrite(clkPin, HIGH);
                digitalWrite(clkPin, LOW);
            }
            digitalWrite(latchPin, LOW);
        }
    }
    digitalWrite(dataPin, LOW);
}

void LED1642GW::setLedTo(uint16_t ledIndex, struct RGBWColor16 color)
{
    ledIndex = ledIndex * sizeof(color) / sizeof(color.r);
    if (ledIndex >= nLedDots)
        return;
    memcpy(&leds[ledIndex], &color, sizeof(color));
}

void LED1642GW::setLedTo(uint16_t ledIndex, struct RGBColor16 color)
{
    if (ledIndex >= nLedDots * sizeof(color) / sizeof(color.r))
        return;
    memcpy(&leds[ledIndex], &color, sizeof(color));
}

void LED1642GW::setLedTo(uint16_t ledIndex, uint16_t brightness)
{
    if (ledIndex >= nLedDots)
        return; // catch out of bounds index
    leds[ledIndex] = brightness;
}

void LED1642GW::setAllLedsTo(struct RGBWColor16 color)
{
    for (int i = 0; i < nLedDots; i++) {
        switch (i % 4) {
        case 0:
            leds[i] = color.r;
            break;
        case 1:
            leds[i] = color.g;
            break;
        case 2:
            leds[i] = color.b;
            break;
        case 3:
            leds[i] = color.w;
            break;
        default:
            leds[i] = 0; // should not occur
            break;
        }
    }
}
void LED1642GW::setAllLedsTo(struct RGBColor16 color)
{
    for (int i = 0; i < nLedDots; i++) {
        switch (i % 3) {
        case 0:
            leds[i] = color.r;
            break;
        case 1:
            leds[i] = color.g;
            break;
        case 2:
            leds[i] = color.b;
            break;
        default:
            leds[i] = 0; // should not occur
            break;
        }
    }
}
void LED1642GW::setAllLedsTo(uint16_t brightness)
{
    for (int i = 0; i < nLedDots; i++) {
        leds[i] = brightness;
    }
}

void LED1642GW::clearLeds()
{
    for (int i = 0; i < nLedDots; i++) {
        leds[i] = 0;
    }
}