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
void IRAM_ATTR display_push_gb_line(uint8_t y, const uint8_t* px, const uint16_t* pal, uint8_t mask) {
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
#define DARK_CTRL_BG       0x08A4
#define DARK_POD_BG        0x10E5
#define DARK_LINE_CYAN     0x07FF
#define DARK_DPAD_BODY     0x1949
#define DARK_DPAD_HI       0x3DFE
#define DARK_DPAD_SHADOW   0x0863
#define DARK_BTN_A         0x0698
#define DARK_BTN_B         0x24BD
#define DARK_BTN_HI        0x07FF
#define DARK_BTN_SHADOW    0x0863
#define DARK_RUBBER_BTN    0x1949
#define DARK_TEXT_CYAN     0x3DFE

void display_draw_controls() {
    tft.fillRect(0, CTRL_Y, SCREEN_W, CTRL_H, DARK_CTRL_BG);
    tft.drawFastHLine(0, CTRL_Y, SCREEN_W, DARK_LINE_CYAN);
    tft.drawFastHLine(0, CTRL_Y + 1, SCREEN_W, 0x10E5);

    // D-pad (68x68, 24px wide arms)
    int cx = DPAD_CX, cy = DPAD_CY;
    // Drop shadow
    tft.fillRoundRect(cx - 12 + 1, cy - 34 + 2, 24, 68, 4, DARK_DPAD_SHADOW);
    tft.fillRoundRect(cx - 34 + 1, cy - 12 + 2, 68, 24, 4, DARK_DPAD_SHADOW);
    // Body
    tft.fillRoundRect(cx - 12, cy - 34, 24, 68, 4, DARK_DPAD_BODY);
    tft.fillRoundRect(cx - 34, cy - 12, 68, 24, 4, DARK_DPAD_BODY);
    tft.drawRoundRect(cx - 12, cy - 34, 24, 68, 4, DARK_DPAD_HI);
    tft.drawRoundRect(cx - 34, cy - 12, 68, 24, 4, DARK_DPAD_HI);

    // Center dimple
    tft.fillCircle(cx, cy, 7, DARK_DPAD_SHADOW);
    tft.drawCircle(cx, cy, 7, DARK_LINE_CYAN);

    // Direction arrows
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(DARK_LINE_CYAN, DARK_DPAD_BODY);
    tft.drawString("^", cx, cy - 20, 2);
    tft.drawString("v", cx, cy + 20, 2);
    tft.drawString("<", cx - 20, cy, 2);
    tft.drawString(">", cx + 20, cy, 2);

    // Angled recessed pod for A/B buttons
    tft.fillRoundRect(BTN_B_X - BTN_B_R - 4, BTN_A_Y - BTN_A_R - 2, 68, 56, 12, DARK_POD_BG);
    tft.drawRoundRect(BTN_B_X - BTN_B_R - 4, BTN_A_Y - BTN_A_R - 2, 68, 56, 12, 0x21AA);

    // B button (Royal Blue)
    tft.fillCircle(BTN_B_X + 1, BTN_B_Y + 2, BTN_B_R, DARK_BTN_SHADOW);
    tft.fillCircle(BTN_B_X, BTN_B_Y, BTN_B_R, DARK_BTN_B);
    tft.drawCircle(BTN_B_X, BTN_B_Y, BTN_B_R, DARK_BTN_HI);
    tft.setTextColor(TFT_WHITE, DARK_BTN_B);
    tft.drawString("B", BTN_B_X, BTN_B_Y, 2);

    // A button (Turquoise)
    tft.fillCircle(BTN_A_X + 1, BTN_A_Y + 2, BTN_A_R, DARK_BTN_SHADOW);
    tft.fillCircle(BTN_A_X, BTN_A_Y, BTN_A_R, DARK_BTN_A);
    tft.drawCircle(BTN_A_X, BTN_A_Y, BTN_A_R, DARK_BTN_HI);
    tft.setTextColor(TFT_BLACK, DARK_BTN_A);
    tft.drawString("A", BTN_A_X, BTN_A_Y, 2);

    // Printed labels below buttons
    tft.setTextColor(DARK_TEXT_CYAN, DARK_CTRL_BG);
    tft.drawString("B", BTN_B_X + 12, BTN_B_Y + 16, 1);
    tft.drawString("A", BTN_A_X + 12, BTN_A_Y + 16, 1);

    // START & SELECT buttons
    tft.fillRoundRect(BTN_SE_X - BTN_SE_W/2, BTN_SE_Y - BTN_SE_H/2, BTN_SE_W, BTN_SE_H, 4, DARK_RUBBER_BTN);
    tft.drawRoundRect(BTN_SE_X - BTN_SE_W/2, BTN_SE_Y - BTN_SE_H/2, BTN_SE_W, BTN_SE_H, 4, DARK_DPAD_HI);
    tft.drawString("SELECT", BTN_SE_X, BTN_SE_Y + 15, 1);

    tft.fillRoundRect(BTN_ST_X - BTN_ST_W/2, BTN_ST_Y - BTN_ST_H/2, BTN_ST_W, BTN_ST_H, 4, DARK_RUBBER_BTN);
    tft.drawRoundRect(BTN_ST_X - BTN_ST_W/2, BTN_ST_Y - BTN_ST_H/2, BTN_ST_W, BTN_ST_H, 4, DARK_DPAD_HI);
    tft.drawString("START", BTN_ST_X, BTN_ST_Y + 15, 1);

    // MENU button (top-right overlay on game screen)
    tft.fillCircle(BTN_M_X, BTN_M_Y, BTN_M_R, 0x08A4);
    tft.drawCircle(BTN_M_X, BTN_M_Y, BTN_M_R, DARK_LINE_CYAN);
    tft.setTextColor(DARK_LINE_CYAN, 0x08A4);
    tft.drawString("||", BTN_M_X, BTN_M_Y, 2);
}

void display_clear_controls() {
    tft.fillRect(0, CTRL_Y, SCREEN_W, CTRL_H, TFT_BLACK);
}

