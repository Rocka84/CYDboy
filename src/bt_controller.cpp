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
    gap_set_page_scan_type(PAGE_SCAN_MODE_INTERLACED);
    gap_set_page_scan_activity(0x0200, 0x0100);
}

static uint32_t start_latch_until = 0;
static uint32_t select_latch_until = 0;
static uint32_t last_start_trigger = 0;
static uint32_t last_select_trigger = 0;
static uint32_t menu_latch_until = 0;

void bt_controller_update() {
    BP32.update();

    if (active_controller && active_controller->isConnected()) {
        uint16_t btns = 0;
        uint32_t now = millis();

        uint8_t d = active_controller->dpad();
        int32_t ax = active_controller->axisX();
        int32_t ay = active_controller->axisY();

        if ((d & DPAD_UP) || (ay < -220)) btns |= GB_BTN_UP;
        if ((d & DPAD_DOWN) || (ay > 220)) btns |= GB_BTN_DOWN;
        if ((d & DPAD_LEFT) || (ax < -220)) btns |= GB_BTN_LEFT;
        if ((d & DPAD_RIGHT) || (ax > 220)) btns |= GB_BTN_RIGHT;

        if (active_controller->a() || active_controller->x()) btns |= GB_BTN_A;
        if (active_controller->b() || active_controller->y()) btns |= GB_BTN_B;

        bool start_triggered = false;
        bool select_triggered = false;

        // Start button handling
        if (active_controller->miscStart()) {
            start_latch_until = now + 250;
            if (now - last_start_trigger > 300) {
                start_triggered = true;
                last_start_trigger = now;
            }
        }
        if (now < start_latch_until) {
            btns |= GB_BTN_START;
        }

        // Select button handling (Esc or Select key)
        if (active_controller->miscSelect() || active_controller->miscCapture()) {
            select_latch_until = now + 250;
            if (now - last_select_trigger > 300) {
                select_triggered = true;
                last_select_trigger = now;
            }
        }
        if (now < select_latch_until) {
            btns |= GB_BTN_SELECT;
        }

        // In-game menu combo:
        // 1. Both buttons overlapping within the latch window
        // 2. Or sequential press within 800ms (since release-only buttons rarely fire at the exact same millisecond)
        if ((start_triggered && (now - last_select_trigger < 800)) ||
            (select_triggered && (now - last_start_trigger < 800))) {
            menu_latch_until = now + 300;
        }

        // 3. Dedicated button shortcuts: L1 + R1, or System button
        if (active_controller->l1() && active_controller->r1()) {
            menu_latch_until = now + 200;
        }
        if (active_controller->miscSystem()) {
            menu_latch_until = now + 200;
        }

        if (now < menu_latch_until) {
            btns |= GB_BTN_MENU;
        }

        bt_buttons = btns;
    } else {
        bt_buttons = 0;
        start_latch_until = 0;
        select_latch_until = 0;
        menu_latch_until = 0;
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

static void draw_badge(int x, int y, int w, int h, const char* label, bool active, uint16_t active_col = TFT_GREEN) {
    uint16_t bg = active ? active_col : 0x2124;
    uint16_t fg = active ? TFT_BLACK : 0x7BEF;
    tft.fillRoundRect(x, y, w, h, 3, bg);
    tft.drawRoundRect(x, y, w, h, 3, active ? TFT_WHITE : 0x4208);
    tft.setTextColor(fg, bg);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(label, x + w / 2, y + h / 2, 1);
}

static void draw_circle_btn(int cx, int cy, int r, const char* label, bool active, uint16_t active_col = TFT_GREEN) {
    uint16_t bg = active ? active_col : 0x2124;
    uint16_t fg = active ? TFT_BLACK : 0x7BEF;
    tft.fillCircle(cx, cy, r, bg);
    tft.drawCircle(cx, cy, r, active ? TFT_WHITE : 0x4208);
    tft.setTextColor(fg, bg);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(label, cx, cy, 1);
}

void bt_controller_ui_show() {
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, SCREEN_W, 36, 0x18C3);
    tft.setTextColor(TFT_WHITE, 0x18C3);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Bluetooth Gamepad", SCREEN_W / 2, 18, 2);

    auto draw_status = [&]() {
        tft.fillRect(8, 42, SCREEN_W - 16, 44, 0x1082);
        tft.drawRoundRect(8, 42, SCREEN_W - 16, 44, 4, 0x528A);
        tft.setTextDatum(MC_DATUM);
        if (bt_controller_is_connected()) {
            tft.setTextColor(TFT_GREEN, 0x1082);
            tft.drawString("Connected!", SCREEN_W / 2, 54, 2);
            tft.setTextColor(TFT_WHITE, 0x1082);
            tft.drawString(controller_name, SCREEN_W / 2, 72, 1);
        } else {
            tft.setTextColor(0xFFE0, 0x1082);
            tft.drawString("Scanning / Pairing...", SCREEN_W / 2, 54, 2);
            tft.setTextColor(0x7BEF, 0x1082);
            tft.drawString("Put gamepad in pairing mode", SCREEN_W / 2, 72, 1);
        }
    };

    char last_btn_name[32] = "None";

    auto draw_tester = [&](bool conn) {
        tft.fillRect(6, 90, SCREEN_W - 12, 178, TFT_BLACK);
        tft.drawRoundRect(6, 90, SCREEN_W - 12, 178, 4, 0x3186);

        if (!conn || !active_controller) {
            tft.setTextColor(0x7BEF, TFT_BLACK);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("Waiting for controller...", SCREEN_W / 2, 175, 2);
            return;
        }

        uint16_t raw_btns = active_controller->buttons();
        uint16_t misc_btns = active_controller->miscButtons();
        uint8_t dpad = active_controller->dpad();
        int32_t ax = active_controller->axisX();
        int32_t ay = active_controller->axisY();

        bool btn_a = active_controller->a();
        bool btn_b = active_controller->b();
        bool btn_x = active_controller->x();
        bool btn_y = active_controller->y();
        bool btn_l1 = active_controller->l1();
        bool btn_r1 = active_controller->r1();
        bool btn_l2 = active_controller->l2();
        bool btn_r2 = active_controller->r2();
        bool btn_th_l = active_controller->thumbL();
        bool btn_th_r = active_controller->thumbR();

        bool m_sys = active_controller->miscSystem();
        bool m_sel = active_controller->miscSelect();
        bool m_sta = active_controller->miscStart();
        bool m_cap = active_controller->miscCapture();

        // Check if any button was pressed to record "Last Pressed"
        if (m_sel) strncpy(last_btn_name, "SELECT (misc 0x02)", sizeof(last_btn_name));
        else if (m_sta) strncpy(last_btn_name, "START (misc 0x04)", sizeof(last_btn_name));
        else if (m_sys) strncpy(last_btn_name, "SYSTEM (misc 0x01)", sizeof(last_btn_name));
        else if (m_cap) strncpy(last_btn_name, "CAPTURE (misc 0x08)", sizeof(last_btn_name));
        else if (btn_a) strncpy(last_btn_name, "A (0x0001)", sizeof(last_btn_name));
        else if (btn_b) strncpy(last_btn_name, "B (0x0002)", sizeof(last_btn_name));
        else if (btn_x) strncpy(last_btn_name, "X (0x0008)", sizeof(last_btn_name));
        else if (btn_y) strncpy(last_btn_name, "Y (0x0004)", sizeof(last_btn_name));
        else if (btn_l1) strncpy(last_btn_name, "L1 (0x0010)", sizeof(last_btn_name));
        else if (btn_r1) strncpy(last_btn_name, "R1 (0x0020)", sizeof(last_btn_name));
        else if (btn_l2) strncpy(last_btn_name, "L2 (0x0040)", sizeof(last_btn_name));
        else if (btn_r2) strncpy(last_btn_name, "R2 (0x0080)", sizeof(last_btn_name));
        else if (btn_th_l) strncpy(last_btn_name, "L3 / ThumbL", sizeof(last_btn_name));
        else if (btn_th_r) strncpy(last_btn_name, "R3 / ThumbR", sizeof(last_btn_name));
        else if (raw_btns != 0) snprintf(last_btn_name, sizeof(last_btn_name), "Raw 0x%04X", raw_btns);
        else if (misc_btns != 0) snprintf(last_btn_name, sizeof(last_btn_name), "Misc 0x%02X", misc_btns);

        tft.setTextColor(0x7BEF, TFT_BLACK);
        tft.setTextDatum(TL_DATUM);
        tft.drawString("GB Mapped", 14, 95, 1);
        tft.drawString("All Detected Inputs", 120, 95, 1);

        // --- Left: GB Controls ---
        int cx = 38, cy = 145;
        bool u = (dpad & DPAD_UP) || (ay < -220);
        bool d = (dpad & DPAD_DOWN) || (ay > 220);
        bool l = (dpad & DPAD_LEFT) || (ax < -220);
        bool r = (dpad & DPAD_RIGHT) || (ax > 220);

        draw_circle_btn(cx, cy - 20, 9, "^", u);
        draw_circle_btn(cx, cy + 20, 9, "v", d);
        draw_circle_btn(cx - 20, cy, 9, "<", l);
        draw_circle_btn(cx + 20, cy, 9, ">", r);

        draw_circle_btn(96, 134, 11, "A", (btn_a || btn_x || btn_r1), 0xF800);
        draw_circle_btn(74, 154, 11, "B", (btn_b || btn_y || btn_l1), 0x07E0);

        draw_badge(14, 178, 38, 16, "SEL", (m_sel || m_cap));
        draw_badge(58, 178, 38, 16, "STA", m_sta);

        // --- Right: Extra Inputs ---
        // Row 1: X, Y, SYS, CAP
        draw_badge(120, 112, 24, 16, "X", btn_x, 0x07FF);
        draw_badge(148, 112, 24, 16, "Y", btn_y, 0xFFE0);
        draw_badge(176, 112, 26, 16, "SYS", m_sys, 0xFD20);
        draw_badge(206, 112, 26, 16, "CAP", m_cap, 0xFD20);

        // Row 2: L1, R1, L2, R2
        draw_badge(120, 132, 24, 16, "L1", btn_l1);
        draw_badge(148, 132, 24, 16, "R1", btn_r1);
        draw_badge(176, 132, 26, 16, "L2", btn_l2);
        draw_badge(206, 132, 26, 16, "R2", btn_r2);

        // Row 3: L3, R3, Stick indicator
        draw_badge(120, 152, 24, 16, "L3", btn_th_l);
        draw_badge(148, 152, 24, 16, "R3", btn_th_r);

        // Mini stick circle
        int sx = 196, sy = 160;
        tft.drawCircle(sx, sy, 12, 0x528A);
        int dot_x = sx + constrain((int)(ax / 45), -10, 10);
        int dot_y = sy + constrain((int)(ay / 45), -10, 10);
        tft.fillCircle(dot_x, dot_y, 3, TFT_CYAN);

        // --- Bottom Readouts ---
        tft.drawLine(12, 202, SCREEN_W - 12, 202, 0x3186);

        char line1[36];
        snprintf(line1, sizeof(line1), "RAW: 0x%04X  MISC: 0x%02X", raw_btns, misc_btns);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(line1, SCREEN_W / 2, 214, 1);

        char line2[36];
        snprintf(line2, sizeof(line2), "DPAD: %d  AXIS: %d, %d", (int)dpad, (int)ax, (int)ay);
        tft.setTextColor(0x7BEF, TFT_BLACK);
        tft.drawString(line2, SCREEN_W / 2, 228, 1);

        char line3[36];
        snprintf(line3, sizeof(line3), "Last: %s", last_btn_name);
        tft.setTextColor(0x07E0, TFT_BLACK);
        tft.drawString(line3, SCREEN_W / 2, 244, 2);
    };

    // Exit button
    tft.fillRoundRect(16, 276, SCREEN_W - 32, 34, 5, 0x18C3);
    tft.drawRoundRect(16, 276, SCREEN_W - 32, 34, 5, 0x528A);
    tft.setTextColor(TFT_WHITE, 0x18C3);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Tap Screen to Exit (or hold Start)", SCREEN_W / 2, 293, 2);

    draw_status();
    draw_tester(bt_controller_is_connected());

    bool last_conn = bt_controller_is_connected();
    uint16_t prev_raw = 0xFFFF;
    uint16_t prev_misc = 0xFFFF;
    uint8_t prev_dpad = 0xFF;
    int32_t prev_ax = 9999, prev_ay = 9999;
    uint32_t t_ui = 0;
    uint32_t start_hold_time = 0;

    while (true) {
        bt_controller_update();
        touch_update();

        bool conn = bt_controller_is_connected();

        if (conn != last_conn || millis() - t_ui > 1200) {
            last_conn = conn;
            t_ui = millis();
            draw_status();
            draw_tester(conn);
        }

        if (conn && active_controller) {
            uint16_t raw = active_controller->buttons();
            uint16_t misc = active_controller->miscButtons();
            uint8_t dp = active_controller->dpad();
            int32_t cur_ax = active_controller->axisX();
            int32_t cur_ay = active_controller->axisY();

            bool ax_changed = (abs(cur_ax - prev_ax) > 80) || (abs(cur_ay - prev_ay) > 80);

            if (raw != prev_raw || misc != prev_misc || dp != prev_dpad || ax_changed) {
                prev_raw = raw;
                prev_misc = misc;
                prev_dpad = dp;
                prev_ax = cur_ax;
                prev_ay = cur_ay;

                draw_tester(true);

                Serial.printf("[BT TEST] raw=0x%04X misc=0x%02X dpad=%d ax=%d ay=%d (Last: %s)\n",
                              raw, misc, dp, (int)cur_ax, (int)cur_ay, last_btn_name);
            }

            // Hold Start for 1.5 seconds to exit without touchscreen
            if (active_controller->miscStart()) {
                if (start_hold_time == 0) start_hold_time = millis();
                else if (millis() - start_hold_time > 1500) {
                    delay(200);
                    return;
                }
            } else {
                start_hold_time = 0;
            }
        }

        if (touch_is_pressed()) {
            int16_t ty = touch_get_y();
            if (ty >= 260 || ty <= 0) {
                delay(200);
                return;
            }
        }

        delay(15);
    }
}
