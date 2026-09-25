#include <Arduino.h>
#include "button_input.h"
#include "touch_input.h"
#include "bt_controller.h"
#include "hw_config.h"
#include <Wire.h>

static volatile uint16_t cur_btns = 0;
static bool pcf_detected = false;

void button_init() {
    Wire.begin(BUTTON_I2C_SDA, BUTTON_I2C_SCL);
    Wire.setClock(100000);
    Wire.beginTransmission((uint8_t)BUTTON_I2C_ADDR);
    pcf_detected = (Wire.endTransmission() == 0);
    if (pcf_detected) {
        Serial.println("[INPUT] PCF8574 I2C button board detected at 0x20");
    } else {
        Serial.println("[INPUT] No I2C button board detected; touch active");
    }
}

static uint16_t read_pcf_buttons() {
    Wire.requestFrom((uint8_t)BUTTON_I2C_ADDR, (uint8_t)1);
    if (Wire.available() < 1) return 0;
    uint8_t raw = Wire.read();
    raw = ~raw;  // PCF8574 inputs are pulled high; pressed is low.

    uint16_t buttons = 0;
    if (raw & (1 << 0)) buttons |= GB_BTN_UP;
    if (raw & (1 << 1)) buttons |= GB_BTN_DOWN;
    if (raw & (1 << 2)) buttons |= GB_BTN_LEFT;
    if (raw & (1 << 3)) buttons |= GB_BTN_RIGHT;
    if (raw & (1 << 4)) buttons |= GB_BTN_A;
    if (raw & (1 << 5)) buttons |= GB_BTN_B;
    if (raw & (1 << 6)) buttons |= GB_BTN_START;
    if (raw & (1 << 7)) buttons |= GB_BTN_SELECT;
    return buttons;
}

void button_update() {
    touch_update();
    bt_controller_update();
    uint16_t btns = touch_get_buttons() | bt_controller_get_buttons();
    if (pcf_detected) {
        btns |= read_pcf_buttons();
    }
    cur_btns = btns;
}

uint16_t button_get_buttons() {
    return cur_btns;
}

