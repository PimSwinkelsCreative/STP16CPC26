#include <Arduino.h>
#include <LED1642GW.h>

// pinout:
#define LATCH_PIN 34
#define BLANK_PIN 35
#define DATA_PIN 37
#define CLK_PIN 36
#define UNUSED_GPIO 1   //pick a GPIO that is not connected to anything

// hardware setup:
#define N_LEDS 2000

// led data:
uint16_t nodes[N_LEDS];

// create the TLC5947 object:
// The led array needs to be initialized prior to this object creation
LED1642GW* ledDriver;

// fade settings
const uint16_t minBrightness = 0;
const uint16_t maxBrightness = 0x1000;
const uint32_t fadeTime = 1000; // fadetime in milliseconds
const uint16_t FPS = 100; // frames per second

// fadeVariables
uint16_t frameInterval = 1000 / FPS;
uint32_t lastFrameUpdate = 0;
uint32_t lastFrameStart = 0;

// running light animation:
uint16_t currentLed = 0;

void setup()
{
    Serial.begin(115200);
    // ledDriver.setBrightness(0x3F);
    delay(2000);

    ledDriver = new LED1642GW(nodes, N_LEDS, CLK_PIN, DATA_PIN, LATCH_PIN, UNUSED_GPIO, BLANK_PIN);
}

void loop()
{
    uint32_t now = millis();
    if (now - lastFrameUpdate >= frameInterval) {
        lastFrameUpdate = now;

        // brightness fade:
        uint16_t brightness = 0;
        uint32_t timeSinceFadeStart = now - lastFrameStart;
        if (timeSinceFadeStart < fadeTime / 2) {
            brightness = map(timeSinceFadeStart, 0, fadeTime / 2, minBrightness, maxBrightness);

        } else if (timeSinceFadeStart < fadeTime) {
            brightness = map(timeSinceFadeStart, fadeTime / 2, fadeTime, maxBrightness, minBrightness);

        } else {
            // fade complete, reset the fade:
            brightness = 0;
            lastFrameStart = now;
        }

        for (int i = 0; i < N_LEDS; i++) {
            ledDriver->setLedTo(i, brightness);
        }
        ledDriver->update();
    }
}
