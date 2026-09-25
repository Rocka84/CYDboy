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
    ledcSetup(0, 5000, 8);
    ledcAttachPin(TFT_PIN_BL, 0);
    ledcWrite(0, 255);
    Serial.printf("[TFT] %dx%d OK\n", tft.width(), tft.height());
}

void display_set_backlight(uint8_t level) { ledcWrite(0, level); }
void display_clear(uint16_t color) { tft.fillScreen(color); }

// Game scanline -> top 192px (2x horiz, ~1.33x vert)
void display_push_gb_line(uint8_t y, uint16_t* buf) {
    if (y >= GB_SCREEN_H) return;
    int idx = 0;
    for (int x = 0; x < GB_SCREEN_W && idx < SCREEN_W; x++) {
        scaled[idx++] = buf[x];
        if ((x & 1) == 0 && idx < SCREEN_W) scaled[idx++] = buf[x];
    }
    while (idx < SCREEN_W) scaled[idx++] = buf[GB_SCREEN_W-1];

    int y0 = y * GAME_H / GB_SCREEN_H;
    int y1 = (y+1) * GAME_H / GB_SCREEN_H;
    if (y1 <= y0) y1 = y0 + 1;
    int h = y1 - y0;
    if (y0 + h > GAME_H) h = GAME_H - y0;
    if (h <= 0) return;

    if (h == 2) {
        memcpy(scaled + SCREEN_W, scaled, SCREEN_W * sizeof(uint16_t));
    }
    tft.pushImage(0, y0, SCREEN_W, h, scaled);
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

