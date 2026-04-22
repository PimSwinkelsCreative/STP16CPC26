#include "LED1642GW.h"
#include "driver/gpio.h"
#include "driver/i2s.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_struct.h"

LED1642GW::LED1642GW(uint16_t* _ledData, uint16_t _nLedDots, uint8_t _clkPin,
    uint8_t _dataPin, uint8_t _latchPin, int8_t _pwmClockPin)
    : leds(_ledData)
    , nLedDots(_nLedDots)
    , clkPin(_clkPin)
    , dataPin(_dataPin)
    , latchPin(_latchPin)
    , pwmClockPin(_pwmClockPin)
{
    init();
}

LED1642GW::LED1642GW(RGBColor16* _rgbLedData, uint16_t _nRGBLeds, uint8_t _clkPin,
    uint8_t _dataPin, uint8_t _latchPin, int8_t _pwmClockPin)
    : leds((uint16_t*)_rgbLedData)
    , nLedDots(_nRGBLeds * (sizeof(_rgbLedData[0]) / sizeof(_rgbLedData[0].r)))
    , clkPin(_clkPin)
    , dataPin(_dataPin)
    , latchPin(_latchPin)
    , pwmClockPin(_pwmClockPin)
{
    init();
}

LED1642GW::LED1642GW(RGBWColor16* _rgbwData, uint16_t _nRGBWLeds, uint8_t _clkPin,
    uint8_t _dataPin, uint8_t _latchPin, int8_t _pwmClockPin)
    : leds((uint16_t*)_rgbwData)
    , nLedDots(_nRGBWLeds * (sizeof(_rgbwData[0]) / sizeof(_rgbwData[0].r)))
    , clkPin(_clkPin)
    , dataPin(_dataPin)
    , latchPin(_latchPin)
    , pwmClockPin(_pwmClockPin)
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

    clkPinBitmap = 1 << (clkPin % 32);
    dataPinBitmap = 1 << (dataPin % 32);
    latchPinBitmap = 1 << (latchPin % 32);

    lastSettingsUpdate = 0;
    settingUpdateInterval = 1000;

    setConfigRegister();
    enableOutputs();

    if (pwmClockPin >= 0) {
        startPWMClock();
    }
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
            setDataPin((cfg & 0x01 << i) >> i);

            if (driver == 0) {
                if (i == 6) {
                    setLatchPin();
                }
            }
            pulseClock();
        }
        clearLatchPin();
    }
    setDataPin(false);
}

void LED1642GW::enableOutputs()
{
    for (int driver = nLedDrivers - 1; driver >= 0; driver--) {
        for (int i = 15; i >= 0; i--) {
            setDataPin(true); // all pins need to be enabled, so data is always "1"

            if (driver == 0) {
                if (i == 1) {
                    digitalWrite(latchPin, HIGH);
                }
            }
            pulseClock();
        }
        clearLatchPin();
    }
    setDataPin(false);
}

void LED1642GW::startPWMClock()
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
    // periodically re-send the config data to allow for delayed power on of the interconnect boards:
    if (millis() - lastSettingsUpdate > settingUpdateInterval) {
        lastSettingsUpdate = millis();
        setConfigRegister();
        enableOutputs();
    }

    // send the LED data:
    for (int channel = 15; channel >= 0; channel--) {
        for (int driver = nLedDrivers - 1; driver >= 0; driver--) {
            uint16_t nodeIndex = driver * LEDDOTSPERDRIVER + channel;
            for (int i = 15; i >= 0; i--) {
                setDataPin((leds[nodeIndex] & 0x01 << i) >> i);

                if (driver == 0) {
                    if (channel > 0) {
                        if (i == 3) {
                            setLatchPin();
                        }
                    } else {
                        if (i == 5) {
                            setLatchPin();
                        }
                    }
                }
                pulseClock();
            }
            clearLatchPin();
        }
    }
    setDataPin(false);
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

void LED1642GW::pulseClock()
{
    if (clkPin < 31) {
        GPIO.out_w1ts = clkPinBitmap; // set pin
        GPIO.out_w1tc = clkPinBitmap; // clear pin
    } else {
        GPIO.out1_w1ts.val = clkPinBitmap; // set pin
        GPIO.out1_w1tc.val = clkPinBitmap; // clear pin
    }
}
void LED1642GW::setDataPin(bool value)
{
    if (dataPin < 31) {
        if (value) {
            GPIO.out_w1ts = dataPinBitmap;
        } else {
            GPIO.out_w1tc = dataPinBitmap;
        }
    } else {
        if (value) {
            GPIO.out1_w1ts.val = dataPinBitmap;
        } else {
            GPIO.out1_w1tc.val = dataPinBitmap;
        }
    }
}
void LED1642GW::setLatchPin()
{
    if (latchPin < 31) {
        GPIO.out_w1ts = latchPinBitmap;
    } else {
        GPIO.out1_w1ts.val = latchPinBitmap;
    }
}
void LED1642GW::clearLatchPin()
{
    if (latchPin < 31) {
        GPIO.out_w1tc = latchPinBitmap;
    } else {
        GPIO.out1_w1tc.val = latchPinBitmap;
    }
}

void LED1642GW::setConfigUpdateInterval(uint32_t milliseconds)
{
    settingUpdateInterval = milliseconds;
}