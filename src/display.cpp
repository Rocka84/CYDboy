#include "display.h"
#include "hw_config.h"
#include <Arduino.h>

TFT_eSPI tft = TFT_eSPI();
static uint16_t scaled[SCREEN_W * 2];

void display_init() {
    pinMode(TFT_PIN_BL, OUTPUT);
    digitalWrite(TFT_PIN_BL, HIGH);
    tft.init();
    tft.invertDisplay(true);
    // Some ST77xx displays expect swapped byte order (RGB/BGR). Enable
    // swap here to match palette byte-order when needed.
    tft.setSwapBytes(true);
    // Use rotation 2 so the USB connector is at the top in portrait mode.
    tft.setRotation(2);
    tft.fillScreen(TFT_BLACK);
    ledcSetup(0, 40000, 8);
    display_set_backlight(255);
    Serial.printf("[TFT] %dx%d OK\n", tft.width(), tft.height());
}

void display_set_backlight(uint8_t level) {
    if (level == 255) {
        ledcDetachPin(TFT_PIN_BL);
        pinMode(TFT_PIN_BL, OUTPUT);
        digitalWrite(TFT_PIN_BL, HIGH);
    } else if (level == 0) {
        ledcDetachPin(TFT_PIN_BL);
        pinMode(TFT_PIN_BL, OUTPUT);
        digitalWrite(TFT_PIN_BL, LOW);
    } else {
        ledcAttachPin(TFT_PIN_BL, 0);
        ledcWrite(0, level);
    }
}
void display_clear(uint16_t color) { tft.fillScreen(color); }

// Game scanline -> top 216px (1.5x horiz, 1.5x vert)
void display_push_gb_line(uint8_t y, const uint8_t* px, const uint16_t* pal, uint8_t mask) {
    if (y >= GB_SCREEN_H) return;

    for (int x = 0, idx = 0; x < GB_SCREEN_W; x += 2) {
        uint16_t p0 = pal[px[x] & mask];
        uint16_t p1 = pal[px[x + 1] & mask];
        scaled[idx++] = p0;
        scaled[idx++] = p0;
        scaled[idx++] = p1;
    }

    int h = (y & 1) ? 2 : 1;
    if (h == 2) {
        memcpy(scaled + SCREEN_W, scaled, SCREEN_W * sizeof(uint16_t));
    }

    if (y == 0) {
        tft.startWrite();
        tft.setAddrWindow(0, 0, SCREEN_W, GAME_H);
    }
    tft.pushPixels(scaled, SCREEN_W * h);
    if (y == GB_SCREEN_H - 1) {
        tft.endWrite();
    }
}

// ─── Control bar (y=216..320) ───────────────────────────────────────────────
void display_draw_controls() {
    tft.fillRect(0, CTRL_Y, SCREEN_W, CTRL_H, 0x18C3);
    tft.drawFastHLine(0, CTRL_Y, SCREEN_W, 0x528A);

    // D-pad (68x68, 24px wide arms)
    int cx = DPAD_CX, cy = DPAD_CY;
    tft.fillRoundRect(cx - 12, cy - 34, 24, 68, 4, 0x4A69);
    tft.fillRoundRect(cx - 34, cy - 12, 68, 24, 4, 0x4A69);
    tft.fillCircle(cx, cy, 6, 0x2965);
    tft.drawCircle(cx, cy, 6, 0x2104);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, 0x4A69);
    tft.drawString("^", cx, cy - 21, 2);
    tft.drawString("v", cx, cy + 21, 2);
    tft.drawString("<", cx - 21, cy, 2);
    tft.drawString(">", cx + 21, cy, 2);

    // A (red, upper-right)
    tft.fillCircle(BTN_A_X, BTN_A_Y, BTN_A_R, 0xC000);
    tft.drawCircle(BTN_A_X, BTN_A_Y, BTN_A_R, 0xF800);
    tft.setTextColor(TFT_WHITE, 0xC000);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("A", BTN_A_X, BTN_A_Y, 2);

    // B (blue, lower-right)
    tft.fillCircle(BTN_B_X, BTN_B_Y, BTN_B_R, 0x0018);
    tft.drawCircle(BTN_B_X, BTN_B_Y, BTN_B_R, 0x03FF);
    tft.setTextColor(TFT_WHITE, 0x0018);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("B", BTN_B_X, BTN_B_Y, 2);

    // START
    tft.fillRoundRect(BTN_ST_X - BTN_ST_W/2, BTN_ST_Y - BTN_ST_H/2, BTN_ST_W, BTN_ST_H, 4, 0x528A);
    tft.setTextColor(TFT_WHITE, 0x528A);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("STA", BTN_ST_X, BTN_ST_Y, 1);

    // SELECT
    tft.fillRoundRect(BTN_SE_X - BTN_SE_W/2, BTN_SE_Y - BTN_SE_H/2, BTN_SE_W, BTN_SE_H, 4, 0x528A);
    tft.setTextColor(TFT_WHITE, 0x528A);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("SEL", BTN_SE_X, BTN_SE_Y, 1);

    // MENU (top-right overlay on game)
    tft.fillCircle(BTN_M_X, BTN_M_Y, BTN_M_R, 0x7BE0);
    tft.setTextColor(TFT_BLACK, 0x7BE0);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("||", BTN_M_X, BTN_M_Y, 2);
}

void display_clear_controls() {
    tft.fillRect(0, CTRL_Y, SCREEN_W, CTRL_H, TFT_BLACK);
}

