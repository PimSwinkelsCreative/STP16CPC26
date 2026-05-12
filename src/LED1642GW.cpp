#include "LED1642GW.h"
#include "driver/gpio.h" //required for direct output register manipulation
#include "driver/i2s.h" //required for 10MHz PWM clock
#include "soc/gpio_reg.h" //required for direct output register manipulation
#include "soc/gpio_struct.h" //required for direct output register manipulation

LED1642GW::LED1642GW(uint16_t* _ledData, uint16_t _nLedDots, uint8_t _clkPin,
    uint8_t _dataPin, uint8_t _latchPin, uint8_t _dummyPin, int8_t _pwmClockPin)
    : leds(_ledData)
    , nLedDots(_nLedDots)
    , clkPin(_clkPin)
    , dataPin(_dataPin)
    , latchPin(_latchPin)
    , dummyPin(_dummyPin)
    , pwmClockPin(_pwmClockPin)
{
    init();
}

LED1642GW::LED1642GW(RGBColor16* _rgbLedData, uint16_t _nRGBLeds, uint8_t _clkPin,
    uint8_t _dataPin, uint8_t _latchPin, uint8_t _dummyPin, int8_t _pwmClockPin)
    : leds((uint16_t*)_rgbLedData)
    , nLedDots(_nRGBLeds * (sizeof(_rgbLedData[0]) / sizeof(_rgbLedData[0].r)))
    , clkPin(_clkPin)
    , dataPin(_dataPin)
    , latchPin(_latchPin)
    , dummyPin(_dummyPin)
    , pwmClockPin(_pwmClockPin)
{
    init();
}

LED1642GW::LED1642GW(RGBWColor16* _rgbwData, uint16_t _nRGBWLeds, uint8_t _clkPin,
    uint8_t _dataPin, uint8_t _latchPin, uint8_t _dummyPin, int8_t _pwmClockPin)
    : leds((uint16_t*)_rgbwData)
    , nLedDots(_nRGBWLeds * (sizeof(_rgbwData[0]) / sizeof(_rgbwData[0].r)))
    , clkPin(_clkPin)
    , dataPin(_dataPin)
    , latchPin(_latchPin)
    , dummyPin(_dummyPin)
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

    // pinMode(clkPin, OUTPUT);
    // pinMode(dataPin, OUTPUT);
    // pinMode(latchPin, OUTPUT);
    // digitalWrite(latchPin, LOW);

    // clkPinBitmap = 1 << (clkPin % 32);
    // dataPinBitmap = 1 << (dataPin % 32);
    // latchPinBitmap = 1 << (latchPin % 32);

    if (!setupDMA(DEFAULT_DMA_CLK_FREQUENCY)) // TODO make clk frequency updateable
    {
        Serial.println("DMA init failed!");
        while (true)
            ;
    }
    Serial.println("DMA streamer ready");

    lastSettingsUpdate = 0;
    settingUpdateInterval = 1000;

    brightness = MAXBRIGHTNESS;

    setConfigRegister();
    enableOutputs();

    if (pwmClockPin >= 0) {
        startPWMClock();
    }
}

bool LED1642GW::setupDMA(uint32_t clockHz)
{
    // ----------------------------------------------------
    // Allocate DMA buffers
    // ----------------------------------------------------

    for (int i = 0; i < DMA_QUEUE_DEPTH; i++) {

        dmaBuffers[i] = (uint8_t*)heap_caps_malloc(
            DMA_BLOCK_SIZE,
            MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

        if (!dmaBuffers[i]) {

            Serial.println("DMA allocation failed");
            return false;
        }

        memset(dmaBuffers[i], 0, DMA_BLOCK_SIZE);
    }

    // ----------------------------------------------------
    // Create semaphores
    // ----------------------------------------------------

    freeBlocks = xSemaphoreCreateCounting(
        DMA_QUEUE_DEPTH,
        DMA_QUEUE_DEPTH);

    queuedBlocks = xSemaphoreCreateCounting(
        DMA_QUEUE_DEPTH,
        0);
    // ----------------------------------------------------
    // LCD BUS CONFIG
    // ----------------------------------------------------

    esp_lcd_i80_bus_config_t bus_config = {

        .dc_gpio_num = dummyPin,

        .wr_gpio_num = clkPin,

        .clk_src = LCD_CLK_SRC_PLL160M,

        .data_gpio_nums = {
            dataPin,
            latchPin,
            dummyPin,
            dummyPin,
            dummyPin,
            dummyPin,
            dummyPin,
            dummyPin },

        .bus_width = 8,

        .max_transfer_bytes = DMA_BLOCK_SIZE,
    };

    ESP_ERROR_CHECK(
        esp_lcd_new_i80_bus(
            &bus_config,
            &i80_bus));

    // ----------------------------------------------------
    // PANEL IO CONFIG
    // ----------------------------------------------------

    esp_lcd_panel_io_i80_config_t io_config = {

        .cs_gpio_num = -1,

        .pclk_hz = clockHz,

        .trans_queue_depth = DMA_QUEUE_DEPTH,

        .on_color_trans_done = dmaDoneISR,

        .user_ctx = this,

        .lcd_cmd_bits = 0,
        .lcd_param_bits = 0,
    };

    ESP_ERROR_CHECK(
        esp_lcd_new_panel_io_i80(
            i80_bus,
            &io_config,
            &io_handle));

    // ----------------------------------------------------
    // Bitmap Mask preparations
    // ----------------------------------------------------

    for (int i = 0; i < 16; i++) {
        if (i > 0) {
            latchMasks[i] = 0x000F;
        } else {
            latchMasks[i] = 0x003F;
        }
    }

    return true;
}

void LED1642GW::setConfigRegister()
{
    // build the config register:
    uint16_t cfg = 0;
    cfg |= brightness; // CFG0..5 = max gain
    cfg |= (1 << 6); // CFG6 = high current range
    cfg |= (1 << 7); // CFG7 = normal mode
    cfg |= (1 << 13); // CFG13 = SDO delay enable

    startMessage();

    for (int driver = nLedDrivers - 1; driver >= 0; driver--) {
        for (int i = 15; i >= 0; i--) {
            uint8_t bitmap = 0;
            // setDataPin((cfg & 0x01 << i) >> i);
            if ((cfg & 0x01 << i)) {
                bitmap |= 0b01; // set the dataPin
            }

            if (driver == 0) {
                if (i <= 6) {
                    // setLatchPin();
                    bitmap |= 0b10; // set the latchPin;
                }
            }
            // pulseClock();
            setBits(bitmap);
        }
        // clearLatchPin();
    }
    // setDataPin(false);
    endMessage();
}

void LED1642GW::enableOutputs()
{
    startMessage();
    for (int driver = nLedDrivers - 1; driver >= 0; driver--) {
        for (int i = 15; i >= 0; i--) {

            uint8_t bitmap = 0;

            bitmap |= 0b01; // always set the datapin high, since all pins need to be enabled
            // setDataPin(true); // all pins need to be enabled, so data is always "1"

            if (driver == 0) {
                if (i <= 1) {
                    bitmap |= 0b10; // set the latchPin high
                    // digitalWrite(latchPin, HIGH);
                }
            }
            // pulseClock();
            setBits(bitmap);
        }
        // clearLatchPin();
    }
    // setDataPin(false);
    endMessage();
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
    // Periodic config refresh
    if (millis() - lastSettingsUpdate > settingUpdateInterval) {
        lastSettingsUpdate = millis();
        setConfigRegister();
        enableOutputs();
    }

    // Start DMA message
    startMessage();

    // Direct DMA access
    uint8_t* out = currentBuffer;
    uint8_t* outEnd = currentBuffer + DMA_BLOCK_SIZE;

    // LED data generation
    for (int channel = 15; channel >= 0; channel--) {
        for (int driver = nLedDrivers - 1; driver >= 0; driver--) {
            uint16_t nodeIndex = driver * LEDDOTSPERDRIVER + channel;
            uint16_t value = leds[nodeIndex];
            uint16_t latch = (driver == 0) ? latchMasks[channel] : 0;

            // Shift out 16 bits
            for (int i = 0; i < 16; i++) {
                *out++ = ((value & 0x8000) ? 0x01 : 0x00) | ((latch & 0x8000) ? 0x02 : 0x00);
                value <<= 1;
                latch <<= 1;

                // DMA block full?
                if (out >= outEnd) {
                    currentIndex = DMA_BLOCK_SIZE;
                    nextDMABlock(out);
                    outEnd = currentBuffer + DMA_BLOCK_SIZE;
                }
            }
        }
    }

    // Submit remaining partial block
    currentIndex = out - currentBuffer;
    endMessage();
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

void LED1642GW::setBrightness(uint8_t _brightness)
{
    brightness = constrain(_brightness, 0, MAXBRIGHTNESS);
    setConfigRegister();
}

void LED1642GW::setConfigUpdateInterval(uint32_t milliseconds)
{
    settingUpdateInterval = milliseconds;
}

void LED1642GW::startMessage()
{
    currentIndex = 0;
    acquireBlock();
}

// ========================================================
// WRITE ONE SYMBOL
// bit0 -> DATA
// bit1 -> LATCH
// ========================================================

inline __attribute__((always_inline)) void LED1642GW::setBits(uint8_t bitmap)
{
    currentBuffer[currentIndex++] = bitmap & 0x03; // only use 2 least significant bits

    // Buffer full?
    if (currentIndex >= DMA_BLOCK_SIZE) {
        submitCurrentBlock(DMA_BLOCK_SIZE);
        acquireBlock();
        currentIndex = 0;
    }
}

void LED1642GW::endMessage()
{
    // Send partially filled block
    if (currentIndex > 0) {

        submitCurrentBlock(currentIndex);
    }
    currentBuffer = nullptr;
    currentIndex = 0;
}

void LED1642GW::flush()
{
    // wait for the queued messages to be sent
    while (true) {
        if (uxSemaphoreGetCount(queuedBlocks) == 0 && uxSemaphoreGetCount(freeBlocks) == DMA_QUEUE_DEPTH) {
            break;
        }
        delayMicroseconds(10);
    }
}

void LED1642GW::acquireBlock()
{
    xSemaphoreTake(freeBlocks, portMAX_DELAY);
    currentBuffer = dmaBuffers[writeBlockIndex];
    writeBlockIndex++;
    writeBlockIndex %= DMA_QUEUE_DEPTH;
}

void LED1642GW::submitCurrentBlock(size_t lengthBytes)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(io_handle, 0, currentBuffer, lengthBytes));
    xSemaphoreGive(queuedBlocks);
}

// ========================================================
// DMA COMPLETE ISR
// ========================================================
bool LED1642GW::dmaDoneISR(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t* edata,
    void* user_ctx)
{

    LED1642GW* self = (LED1642GW*)user_ctx;

    BaseType_t highTaskWoken = pdFALSE;

    xSemaphoreGiveFromISR(
        self->freeBlocks,
        &highTaskWoken);

    xSemaphoreTakeFromISR(
        self->queuedBlocks,
        &highTaskWoken);

    return highTaskWoken == pdTRUE;
}

inline __attribute__((always_inline)) uint8_t* LED1642GW::getWritePointer()
{
    return currentBuffer + currentIndex;
}

inline __attribute__((always_inline)) uint8_t* LED1642GW::getBufferEnd()
{
    return currentBuffer + DMA_BLOCK_SIZE;
}

inline __attribute__((always_inline)) void LED1642GW::nextDMABlock(uint8_t*& out)
{
    // submit current full block
    submitCurrentBlock(DMA_BLOCK_SIZE);

    // acquire next block
    acquireBlock();

    currentIndex = 0;

    // update local pointer
    out = currentBuffer;
}
