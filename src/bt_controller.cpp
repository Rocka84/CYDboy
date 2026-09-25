#include "bt_controller.h"
#include <Arduino.h>
#include <Bluepad32.h>
#include "hw_config.h"
#include "touch_input.h"
#include "display.h"

static ControllerPtr active_controller = nullptr;
static volatile uint16_t bt_buttons = 0;
static char controller_name[32] = "None";

static void onConnectedController(ControllerPtr ctl) {
    active_controller = ctl;
    String name = ctl->getModelName();
    snprintf(controller_name, sizeof(controller_name), "%s", name.c_str());
    Serial.printf("[BT] Controller connected: %s\n", controller_name);
}

static void onDisconnectedController(ControllerPtr ctl) {
    if (active_controller == ctl) {
        Serial.printf("[BT] Controller disconnected: %s\n", controller_name);
        active_controller = nullptr;
        strncpy(controller_name, "None", sizeof(controller_name));
        bt_buttons = 0;
    }
}

void bt_controller_init() {
    Serial.printf("[BT] Initializing Bluepad32 (fw: %s)...\n", BP32.firmwareVersion());
    BP32.setup(&onConnectedController, &onDisconnectedController);
    BP32.enableNewBluetoothConnections(true);
}

void bt_controller_update() {
    BP32.update();

    if (active_controller && active_controller->isConnected()) {
        uint16_t btns = 0;

        uint8_t d = active_controller->dpad();
        int32_t ax = active_controller->axisX();
        int32_t ay = active_controller->axisY();

        if ((d & DPAD_UP) || (ay < -220)) btns |= GB_BTN_UP;
        if ((d & DPAD_DOWN) || (ay > 220)) btns |= GB_BTN_DOWN;
        if ((d & DPAD_LEFT) || (ax < -220)) btns |= GB_BTN_LEFT;
        if ((d & DPAD_RIGHT) || (ax > 220)) btns |= GB_BTN_RIGHT;

        if (active_controller->a() || active_controller->x() || active_controller->r1()) btns |= GB_BTN_A;
        if (active_controller->b() || active_controller->y() || active_controller->l1()) btns |= GB_BTN_B;

        if (active_controller->miscStart()) btns |= GB_BTN_START;
        if (active_controller->miscSelect()) btns |= GB_BTN_SELECT;
        if (active_controller->miscSystem()) btns |= GB_BTN_MENU;

        bt_buttons = btns;
    } else {
        bt_buttons = 0;
    }
}

uint16_t bt_controller_get_buttons() {
    return bt_buttons;
}

bool bt_controller_is_connected() {
    return (active_controller != nullptr && active_controller->isConnected());
}

const char* bt_controller_get_name() {
    return controller_name;
}

void bt_controller_ui_show() {
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, SCREEN_W, 36, 0x18C3);
    tft.setTextColor(TFT_WHITE, 0x18C3);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Bluetooth Gamepad", SCREEN_W / 2, 18, 2);

    auto draw_status = [&]() {
        tft.fillRect(8, 44, SCREEN_W - 16, 56, 0x1082);
        tft.drawRoundRect(8, 44, SCREEN_W - 16, 56, 4, 0x528A);
        tft.setTextDatum(MC_DATUM);
        if (bt_controller_is_connected()) {
            tft.setTextColor(TFT_GREEN, 0x1082);
            tft.drawString("Connected!", SCREEN_W / 2, 58, 2);
            tft.setTextColor(TFT_WHITE, 0x1082);
            tft.drawString(controller_name, SCREEN_W / 2, 82, 2);
        } else {
            tft.setTextColor(0xFFE0, 0x1082);
            tft.drawString("Scanning / Pairing...", SCREEN_W / 2, 58, 2);
            tft.setTextColor(0x7BEF, 0x1082);
            tft.drawString("Put gamepad in pairing mode", SCREEN_W / 2, 82, 1);
        }
    };

    auto draw_tester = [&](uint16_t b) {
        int cx = 70, cy = 175;
        tft.fillRect(10, 110, SCREEN_W - 20, 140, TFT_BLACK);

        tft.setTextColor(0x7BEF, TFT_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Input Test", SCREEN_W / 2, 120, 1);

        // D-Pad
        tft.fillCircle(cx, cy - 25, 11, (b & GB_BTN_UP) ? TFT_GREEN : 0x2945);
        tft.fillCircle(cx, cy + 25, 11, (b & GB_BTN_DOWN) ? TFT_GREEN : 0x2945);
        tft.fillCircle(cx - 25, cy, 11, (b & GB_BTN_LEFT) ? TFT_GREEN : 0x2945);
        tft.fillCircle(cx + 25, cy, 11, (b & GB_BTN_RIGHT) ? TFT_GREEN : 0x2945);
        tft.setTextColor(TFT_WHITE);
        tft.drawString("^", cx, cy - 25, 1);
        tft.drawString("v", cx, cy + 25, 1);
        tft.drawString("<", cx - 25, cy, 1);
        tft.drawString(">", cx + 25, cy, 1);

        // A / B
        tft.fillCircle(175, cy - 10, 15, (b & GB_BTN_A) ? TFT_GREEN : 0xC000);
        tft.fillCircle(140, cy + 15, 15, (b & GB_BTN_B) ? TFT_GREEN : 0x0018);
        tft.drawString("A", 175, cy - 10, 2);
        tft.drawString("B", 140, cy + 15, 2);

        // Start / Select
        tft.fillRoundRect(80, 222, 34, 18, 3, (b & GB_BTN_SELECT) ? TFT_GREEN : 0x528A);
        tft.fillRoundRect(126, 222, 34, 18, 3, (b & GB_BTN_START) ? TFT_GREEN : 0x528A);
        tft.drawString("SEL", 97, 231, 1);
        tft.drawString("STA", 143, 231, 1);
    };

    // Exit button
    tft.fillRoundRect(16, 272, SCREEN_W - 32, 36, 6, 0x18C3);
    tft.drawRoundRect(16, 272, SCREEN_W - 32, 36, 6, 0x528A);
    tft.setTextColor(TFT_WHITE, 0x18C3);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Back (or press B / Start)", SCREEN_W / 2, 290, 2);

    draw_status();
    draw_tester(0);

    bool last_conn = bt_controller_is_connected();
    uint16_t prev_b = 0;
    uint32_t t_ui = 0;

    while (true) {
        bt_controller_update();
        touch_update();

        uint16_t b = bt_controller_get_buttons();
        bool conn = bt_controller_is_connected();

        if (conn != last_conn || millis() - t_ui > 1000) {
            last_conn = conn;
            t_ui = millis();
            draw_status();
        }

        if (b != prev_b) {
            draw_tester(b);
            // Allow controller B, Start, or Select button to exit back to ROM launcher
            if ((b & (GB_BTN_B | GB_BTN_START | GB_BTN_SELECT | GB_BTN_MENU)) && 
                !(prev_b & (GB_BTN_B | GB_BTN_START | GB_BTN_SELECT | GB_BTN_MENU))) {
                delay(200);
                return;
            }
            prev_b = b;
        }

        if (touch_is_pressed()) {
            int16_t ty = touch_get_y();
            if (ty >= 220 || ty <= 0) {
                delay(200);
                return;
            }
        }

        delay(15);
    }
}
