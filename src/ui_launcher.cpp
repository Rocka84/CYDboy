#include "ui_launcher.h"
#include "display.h"
#include "touch_input.h"
#include "button_input.h"
#include "emulator_bridge.h"
#include "serial_manager.h"
#include "bt_controller.h"
#include "hw_config.h"
#include "audio_output.h"
#include "bgm_player.h"
#include <Arduino.h>

#define ITEMS_PP 5
#define ITEM_H   46
#define ITEM_Y0  42
#define ITEM_X   7
#define CARD_W   (SCREEN_W - 14)
#define CARD_H   40

// ─── Modern Dark Mode Theme Colors (Blue / Light Blue / Turquoise) ───────────
#define COLOR_SCREEN_BG        0x0000 // Pure black (TFT_BLACK)
#define COLOR_HEADER_BG        0x08A4 // Sleek midnight navy (#0E1726)
#define COLOR_CYAN_GLOW        0x07FF // Glowing Electric Cyan (#00FFFF)
#define COLOR_TURQUOISE        0x0698 // Turquoise / Teal (#00D2C4)
#define COLOR_ROYAL_BLUE       0x24BD // Royal Electric Blue (#2563EB)
#define COLOR_LIGHT_BLUE       0x3DFE // Light Sky Blue (#38BDF8)
#define COLOR_CARD_BG          0x10E5 // Dark slate card body (#111C2E)
#define COLOR_CARD_SEL_BG      0x1969 // Elevated rich electric navy (#1A2B4C)
#define COLOR_CARD_BORDER      0x21AA // Steel navy border (#233554)
#define COLOR_TEXT_WHITE       0xFFFF // Crisp white
#define COLOR_TEXT_ICE         0xDEFB // Soft ice white (#DCE3EC)
#define COLOR_SLASH_RED        0xF9A6 // Vivid red for exit (#FF3344)
#define COLOR_STATUS_GREEN     0x262B // Emerald Green for active connection (#22C55E)

// ─── Helpers ────────────────────────────────────────────────────────────────
static void wait_release() {
    button_update();
    while (button_get_buttons() || touch_is_pressed()) {
        button_update();
        delay(10);
    }
    delay(50);
}

struct CleanTitleInfo {
    char title[64];
    char badge[8];
};

static bool match_region_token(const char* tag, char* out, size_t out_max) {
    if (!tag || !out || out_max < 4) return false;
    if (strcasecmp(tag, "USA") == 0 || strcasecmp(tag, "U") == 0 || strcasecmp(tag, "US") == 0) {
        strncpy(out, "USA", out_max); return true;
    }
    if (strcasecmp(tag, "Europe") == 0 || strcasecmp(tag, "EUR") == 0 || strcasecmp(tag, "E") == 0 || strcasecmp(tag, "EU") == 0) {
        strncpy(out, "EUR", out_max); return true;
    }
    if (strcasecmp(tag, "Japan") == 0 || strcasecmp(tag, "JPN") == 0 || strcasecmp(tag, "J") == 0) {
        strncpy(out, "JPN", out_max); return true;
    }
    if (strcasecmp(tag, "Germany") == 0 || strcasecmp(tag, "GER") == 0 || strcasecmp(tag, "G") == 0 || strcasecmp(tag, "DE") == 0) {
        strncpy(out, "GER", out_max); return true;
    }
    if (strcasecmp(tag, "France") == 0 || strcasecmp(tag, "FRA") == 0 || strcasecmp(tag, "F") == 0 || strcasecmp(tag, "FR") == 0) {
        strncpy(out, "FRA", out_max); return true;
    }
    if (strcasecmp(tag, "Spain") == 0 || strcasecmp(tag, "SPA") == 0 || strcasecmp(tag, "ES") == 0) {
        strncpy(out, "SPA", out_max); return true;
    }
    if (strcasecmp(tag, "Italy") == 0 || strcasecmp(tag, "ITA") == 0 || strcasecmp(tag, "IT") == 0) {
        strncpy(out, "ITA", out_max); return true;
    }
    if (strcasecmp(tag, "World") == 0 || strcasecmp(tag, "W") == 0) {
        strncpy(out, "WLD", out_max); return true;
    }
    if (strcasecmp(tag, "Australia") == 0 || strcasecmp(tag, "AUS") == 0) {
        strncpy(out, "AUS", out_max); return true;
    }
    if (strstr(tag, ",") != nullptr || strcasecmp(tag, "En,Fr,De") == 0) {
        strncpy(out, "MULTI", out_max); return true;
    }
    return false;
}

static void parse_rom_display_info(const char* filename, CleanTitleInfo* out) {
    char raw[64];
    strncpy(raw, filename, 63);
    raw[63] = 0;

    char* dot = strrchr(raw, '.');
    if (dot) *dot = 0;

    out->badge[0] = 0;

    char* cur = raw;
    while (*cur) {
        if (*cur == '(' || *cur == '[') {
            char close_ch = (*cur == '(') ? ')' : ']';
            char* tag_start = cur + 1;
            char* tag_end = strchr(tag_start, close_ch);
            if (tag_end) {
                int len = tag_end - tag_start;
                if (len > 0 && len < 14 && out->badge[0] == 0) {
                    char temp[16];
                    strncpy(temp, tag_start, min(len, 15));
                    temp[min(len, 15)] = 0;
                    match_region_token(temp, out->badge, sizeof(out->badge));
                }
                memset(cur, ' ', (tag_end - cur) + 1);
                cur = tag_end + 1;
                continue;
            }
        }
        cur++;
    }

    char clean[64];
    int clean_len = 0;
    bool last_space = true;
    for (int i = 0; raw[i] && clean_len < 60; i++) {
        char c = (raw[i] == '_') ? ' ' : raw[i];
        if (c == ' ') {
            if (!last_space && clean_len > 0) clean[clean_len++] = ' ';
            last_space = true;
        } else {
            clean[clean_len++] = c;
            last_space = false;
        }
    }
    while (clean_len > 0 && clean[clean_len - 1] == ' ') clean_len--;
    clean[clean_len] = 0;

    if (clean_len == 0) {
        strncpy(out->title, filename, 60);
        out->title[60] = 0;
        char* d = strrchr(out->title, '.');
        if (d) *d = 0;
    } else {
        strncpy(out->title, clean, 60);
        out->title[60] = 0;
    }
}

static void draw_bluetooth_icon(int x, int y, bool connected) {
    tft.fillRect(SCREEN_W - 28, 2, 26, 32, COLOR_HEADER_BG);

    // Oval badge matching Bluetooth branding
    uint16_t pill_bg = 0x198B; // Authentic dark navy blue (#1E3050)
    uint16_t pill_border = connected ? COLOR_STATUS_GREEN : COLOR_CARD_BORDER;
    tft.fillRoundRect(x, y, 16, 22, 7, pill_bg);
    tft.drawRoundRect(x, y, 16, 22, 7, pill_border);

    // Status indicated strictly by the color of the "B" rune:
    // Connected: Emerald Green | Disconnected: muted slate gray
    uint16_t rune_col = connected ? COLOR_STATUS_GREEN : 0x4A69;

    int cx = x + 8;
    tft.drawFastVLine(cx, y + 3, 16, rune_col);
    tft.drawFastVLine(cx + 1, y + 3, 16, rune_col);

    tft.drawLine(cx, y + 11, cx + 4, y + 7, rune_col);
    tft.drawLine(cx + 4, y + 7, cx, y + 3, rune_col);
    tft.drawLine(cx, y + 11, cx + 4, y + 15, rune_col);
    tft.drawLine(cx + 4, y + 15, cx, y + 19, rune_col);
    tft.drawLine(cx, y + 11, cx - 4, y + 7, rune_col);
    tft.drawLine(cx, y + 11, cx - 4, y + 15, rune_col);
}

static void draw_header(const char* t) {
    tft.fillRect(0, 0, SCREEN_W, 36, COLOR_HEADER_BG);
    tft.drawFastHLine(0, 36, SCREEN_W, COLOR_CYAN_GLOW);

    // 8-bit style pixel font
    tft.setTextFont(1);
    tft.setTextSize(3);
    tft.setTextColor(COLOR_CYAN_GLOW, COLOR_HEADER_BG);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(t, 12, 18);
    tft.setTextSize(1);

    draw_bluetooth_icon(SCREEN_W - 24, 7, bt_controller_is_connected());
}

// ─── ROM List ───────────────────────────────────────────────────────────────
static void draw_list(RomEntry* r, int cnt, int pg, int sel) {
    int total = cnt;
    int s = pg * ITEMS_PP, e = min(s + ITEMS_PP, total);
    tft.fillRect(0, 37, SCREEN_W, SCREEN_H - 37 - 40, COLOR_SCREEN_BG);

    if (total == 0) {
        tft.setTextColor(COLOR_TEXT_ICE, COLOR_SCREEN_BG);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("No ROMs found on SD", SCREEN_W / 2, 130, 2);
        tft.setTextColor(COLOR_LIGHT_BLUE, COLOR_SCREEN_BG);
        tft.drawString("Use Settings -> USB Manager", SCREEN_W / 2, 155, 2);
    }

    for (int i = s; i < e; i++) {
        int y = ITEM_Y0 + (i - s) * ITEM_H;
        bool is_sel = (i == sel);
        uint16_t card_bg = is_sel ? COLOR_CARD_SEL_BG : COLOR_CARD_BG;
        uint16_t card_border = is_sel ? COLOR_CYAN_GLOW : COLOR_CARD_BORDER;

        tft.fillRoundRect(ITEM_X, y, CARD_W, CARD_H, 6, card_bg);
        tft.drawRoundRect(ITEM_X, y, CARD_W, CARD_H, 6, card_border);
        if (is_sel) {
            tft.drawRoundRect(ITEM_X + 1, y + 1, CARD_W - 2, CARD_H - 2, 5, card_border);
        }

        // Selection cursor
        if (is_sel) {
            tft.setTextColor(COLOR_CYAN_GLOW, card_bg);
            tft.setTextDatum(MC_DATUM);
            tft.drawString(">", ITEM_X + 7, y + CARD_H / 2, 2);
        }

        int badge_x = ITEM_X + (is_sel ? 15 : 8);

        CleanTitleInfo info;
        parse_rom_display_info(r[i].filename, &info);

        // Left Cartridge Badge (DMG / GBC)
        uint16_t badge_c = r[i].is_gbc ? COLOR_TURQUOISE : COLOR_ROYAL_BLUE;
        const char* badge_t = r[i].is_gbc ? "GBC" : "DMG";
        tft.fillRoundRect(badge_x, y + 8, 34, 24, 4, badge_c);
        tft.setTextColor(r[i].is_gbc ? TFT_BLACK : COLOR_TEXT_WHITE, badge_c);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(badge_t, badge_x + 17, y + 20, 1);

        // Game Title (clipped so long titles never overflow)
        int title_x = badge_x + 40;
        int title_w = (info.badge[0] != 0) ? (SCREEN_W - ITEM_X - 48 - title_x) : (SCREEN_W - ITEM_X - 10 - title_x);
        tft.setViewport(title_x, y + 6, title_w, CARD_H - 12, false);
        tft.setTextColor(is_sel ? COLOR_TEXT_WHITE : COLOR_TEXT_ICE, card_bg);
        tft.setTextDatum(ML_DATUM);
        tft.drawString(info.title, title_x, y + CARD_H / 2, 2);
        tft.resetViewport();

        // Right Region Badge (only drawn if region tag exists)
        if (info.badge[0] != 0) {
            tft.fillRoundRect(SCREEN_W - ITEM_X - 44, y + 10, 38, 20, 4, 0x08EB);
            tft.drawRoundRect(SCREEN_W - ITEM_X - 44, y + 10, 38, 20, 4, is_sel ? COLOR_CYAN_GLOW : COLOR_CARD_BORDER);
            tft.setTextColor(is_sel ? COLOR_CYAN_GLOW : COLOR_LIGHT_BLUE, 0x08EB);
            tft.setTextDatum(MC_DATUM);
            tft.drawString(info.badge, SCREEN_W - ITEM_X - 25, y + 20, 1);
        }
    }

    // Bottom Navigation Bar
    const int nav_y = SCREEN_H - 40;
    tft.fillRect(0, nav_y, SCREEN_W, 40, COLOR_HEADER_BG);
    tft.drawFastHLine(0, nav_y, SCREEN_W, COLOR_CYAN_GLOW);

    // Left button [SET]
    tft.fillRoundRect(8, nav_y + 6, 60, 28, 5, 0x1949);
    tft.drawRoundRect(8, nav_y + 6, 60, 28, 5, COLOR_CARD_BORDER);
    tft.setTextColor(COLOR_TEXT_ICE, 0x1949);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("SETTINGS", 38, nav_y + 20, 1);

    // Center Page Indicator
    int tp = (total > 0) ? ((total + ITEMS_PP - 1) / ITEMS_PP) : 1;
    tft.fillRoundRect(74, nav_y + 6, SCREEN_W - 148, 28, 5, COLOR_CARD_BG);
    tft.drawRoundRect(74, nav_y + 6, SCREEN_W - 148, 28, 5, COLOR_CARD_BORDER);
    tft.setTextColor(COLOR_CYAN_GLOW, COLOR_CARD_BG);
    char ps[20];
    snprintf(ps, sizeof(ps), "< %d / %d >", pg + 1, tp);
    tft.drawString(ps, SCREEN_W / 2, nav_y + 20, 2);

    // Right button [CAL]
    tft.fillRoundRect(SCREEN_W - 68, nav_y + 6, 60, 28, 5, 0x1949);
    tft.drawRoundRect(SCREEN_W - 68, nav_y + 6, 60, 28, 5, COLOR_CARD_BORDER);
    tft.setTextColor(COLOR_TEXT_ICE, 0x1949);
    tft.drawString("CALIB", SCREEN_W - 38, nav_y + 20, 1);
}

static void update_selected_marquee(RomEntry* r, int cnt, int pg, int sel, uint32_t sel_start_ms, int* last_scroll_x) {
    if (sel >= cnt) return;
    int s = pg * ITEMS_PP, e = min(s + ITEMS_PP, cnt);
    if (sel < s || sel >= e) return;

    CleanTitleInfo info;
    parse_rom_display_info(r[sel].filename, &info);

    int y = ITEM_Y0 + (sel - s) * ITEM_H;
    int badge_x = ITEM_X + 15;
    int title_x = badge_x + 40;
    int title_w = (info.badge[0] != 0) ? (SCREEN_W - ITEM_X - 48 - title_x) : (SCREEN_W - ITEM_X - 10 - title_x);

    tft.setTextFont(1);
    tft.setTextSize(1);
    int text_w = tft.textWidth(info.title, 2);
    if (text_w <= title_w) return;

    int max_scroll = text_w - title_w;
    uint32_t elapsed = millis() - sel_start_ms;
    const uint32_t pause_ms = 1200;
    const uint32_t speed_px_sec = 35;
    uint32_t scroll_duration = (max_scroll * 1000) / speed_px_sec;
    uint32_t cycle_total = pause_ms + scroll_duration + pause_ms;

    uint32_t cycle_pos = elapsed % cycle_total;
    int scroll_x = 0;
    if (cycle_pos < pause_ms) {
        scroll_x = 0;
    } else if (cycle_pos < pause_ms + scroll_duration) {
        scroll_x = (int)((cycle_pos - pause_ms) * speed_px_sec / 1000);
        if (scroll_x > max_scroll) scroll_x = max_scroll;
    } else {
        scroll_x = max_scroll;
    }

    if (scroll_x != *last_scroll_x) {
        *last_scroll_x = scroll_x;
        tft.setViewport(title_x, y + 6, title_w, CARD_H - 12, false);
        tft.fillRect(title_x, y + 6, title_w, CARD_H - 12, COLOR_CARD_SEL_BG);
        tft.setTextColor(COLOR_TEXT_WHITE, COLOR_CARD_SEL_BG);
        tft.setTextDatum(ML_DATUM);
        tft.drawString(info.title, title_x - scroll_x, y + CARD_H / 2, 2);
        tft.resetViewport();
    }
}

int launcher_show(RomEntry* roms, int cnt) {
    int pg = 0, sel = 0;
    tft.fillScreen(COLOR_SCREEN_BG);
    draw_header("CYDboy");

    draw_list(roms, cnt, pg, sel);
    uint16_t prev = 0;
    uint32_t dbg_t = 0;
    int total = cnt;
    int tp = (total > 0) ? ((total + ITEMS_PP - 1) / ITEMS_PP) : 1;
    bool last_conn = bt_controller_is_connected();
    int prev_sel = sel;
    uint32_t sel_start_ms = millis();
    int last_scroll_x = 0;

    while (true) {
        if (serial_manager_check_handshake()) {
            return LAUNCHER_SEL_USB_MANAGER;
        }

        button_update();
        uint16_t b = button_get_buttons();

        bool curr_conn = bt_controller_is_connected();
        if (curr_conn != last_conn) {
            last_conn = curr_conn;
            draw_bluetooth_icon(SCREEN_W - 24, 7, curr_conn);
            int s = pg * ITEMS_PP, e = min(s + ITEMS_PP, total);
            if (cnt >= s && cnt < e) {
                draw_list(roms, cnt, pg, sel);
            }
        }

        if (sel != prev_sel) {
            prev_sel = sel;
            sel_start_ms = millis();
            last_scroll_x = 0;
        }

        update_selected_marquee(roms, cnt, pg, sel, sel_start_ms, &last_scroll_x);

        if (touch_is_pressed()) {
            int16_t tx = touch_get_x(), ty = touch_get_y();

            // Card list touches
            if (ty >= ITEM_Y0 && ty < ITEM_Y0 + ITEMS_PP * ITEM_H) {
                int idx = pg * ITEMS_PP + (ty - ITEM_Y0) / ITEM_H;
                if (idx < total) {
                    if (sel != idx) {
                        sel = idx;
                        draw_list(roms, cnt, pg, sel);
                        delay(180);
                    } else {
                        return sel;
                    }
                }
            }

            // Bottom Nav Bar Touches
            if (ty >= SCREEN_H - 42) {
                if (tx < 72) {
                    return LAUNCHER_SEL_SETTINGS;
                } else if (tx > SCREEN_W - 72) {
                    touch_run_calibration();
                    draw_header("CYDboy");
                    draw_list(roms, cnt, pg, sel);
                    delay(250);
                } else if (tx >= 72 && tx < 120 && pg > 0) {
                    pg--;
                    sel = pg * ITEMS_PP;
                    draw_list(roms, cnt, pg, sel);
                    delay(250);
                } else if (tx >= 120 && tx <= SCREEN_W - 72 && pg < tp - 1) {
                    pg++;
                    sel = pg * ITEMS_PP;
                    draw_list(roms, cnt, pg, sel);
                    delay(250);
                }
            }
        }

        // Select + Start combo opens settings
        if (((b & (GB_BTN_START | GB_BTN_SELECT)) == (GB_BTN_START | GB_BTN_SELECT)) || (b & GB_BTN_MENU)) {
            wait_release();
            return LAUNCHER_SEL_SETTINGS;
        }

        // Up
        if ((b & GB_BTN_UP) && !(prev & GB_BTN_UP)) {
            if (sel > pg * ITEMS_PP) {
                sel--;
                draw_list(roms, cnt, pg, sel);
            } else if (pg > 0) {
                pg--;
                sel = pg * ITEMS_PP + ITEMS_PP - 1;
                draw_list(roms, cnt, pg, sel);
            }
        }
        // Down
        if ((b & GB_BTN_DOWN) && !(prev & GB_BTN_DOWN)) {
            if (sel < total - 1) {
                if (sel < (pg + 1) * ITEMS_PP - 1) {
                    sel++;
                } else if (pg < tp - 1) {
                    pg++;
                    sel = pg * ITEMS_PP;
                }
                draw_list(roms, cnt, pg, sel);
            }
        }
        // Left = prev page
        if ((b & GB_BTN_LEFT) && !(prev & GB_BTN_LEFT)) {
            if (pg > 0) { pg--; sel = pg * ITEMS_PP; draw_list(roms, cnt, pg, sel); }
        }
        // Right = next page
        if ((b & GB_BTN_RIGHT) && !(prev & GB_BTN_RIGHT)) {
            if (pg < tp - 1) { pg++; sel = pg * ITEMS_PP; draw_list(roms, cnt, pg, sel); }
        }
        // A = select
        if ((b & GB_BTN_A) && !(prev & GB_BTN_A)) {
            if (total > 0 && sel < total) {
                return sel;
            }
        }

        prev = b;
        if (millis() - dbg_t > 3000) {
            dbg_t = millis();
            Serial.printf("[LAUNCH] pg=%d sel=%d\n", pg, sel);
        }
        delay(20);
    }
}

// ─── In-game pause menu ─────────────────────────────────────────────────────
static void mbtn(int x, int w, int y, const char* t, uint16_t fg, bool hl) {
    uint16_t bg = hl ? COLOR_CARD_SEL_BG : COLOR_CARD_BG;
    uint16_t border = hl ? COLOR_CYAN_GLOW : COLOR_CARD_BORDER;
    tft.fillRoundRect(x, y, w, 28, 5, bg);
    tft.drawRoundRect(x, y, w, 28, 5, border);
    if (hl) tft.drawRoundRect(x + 1, y + 1, w - 2, 26, 4, border);

    tft.setTextDatum(MC_DATUM);
    if (hl) {
        tft.setTextColor(COLOR_CYAN_GLOW, bg);
        tft.drawString(">", x + 12, y + 14, 2);
    }
    tft.setTextColor(hl ? COLOR_TEXT_WHITE : fg, bg);
    tft.drawString(t, SCREEN_W / 2, y + 14, 2);
}

int launcher_ingame_menu() {
    const int panel_w = SCREEN_W - 24;
    const int panel_x = (SCREEN_W - panel_w) / 2;
    const int panel_y = 12;
    const int panel_h = 224;
    const int btn_w = panel_w - 24;
    const int btn_x = panel_x + 12;

    // Dark sleek Dialog Window
    tft.fillRect(panel_x, panel_y, panel_w, panel_h, COLOR_SCREEN_BG);
    tft.drawRoundRect(panel_x, panel_y, panel_w, panel_h, 6, COLOR_ROYAL_BLUE);
    tft.drawRoundRect(panel_x + 2, panel_y + 2, panel_w - 4, panel_h - 4, 5, COLOR_CYAN_GLOW);

    // PAUSED Banner
    tft.fillRoundRect(panel_x + 10, panel_y + 8, panel_w - 20, 32, 4, COLOR_CARD_BG);
    tft.drawRoundRect(panel_x + 10, panel_y + 8, panel_w - 20, 32, 4, COLOR_CYAN_GLOW);
    tft.setTextColor(COLOR_CYAN_GLOW, COLOR_CARD_BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("PAUSED", SCREEN_W / 2, panel_y + 24, 4);

    #define MI 5
    int yp[MI] = {56, 88, 120, 152, 184};
    const char* lb[MI] = {"Resume", "Save State", "Load State", "Settings", "Quit"};
    uint16_t fc[MI] = {COLOR_TURQUOISE, COLOR_LIGHT_BLUE, COLOR_LIGHT_BLUE, COLOR_TEXT_WHITE, COLOR_SLASH_RED};
    for (int i = 0; i < MI; i++) mbtn(btn_x, btn_w, yp[i], lb[i], fc[i], false);
    wait_release();

    int hl = 0;
    mbtn(btn_x, btn_w, yp[hl], lb[hl], fc[hl], true);
    uint16_t prev = 0;

    while (true) {
        button_update();
        uint16_t b = button_get_buttons();

        if (touch_is_pressed()) {
            int16_t tx = touch_get_x(), ty = touch_get_y();
            if (tx >= btn_x && tx <= btn_x + btn_w) {
                for (int i = 0; i < MI; i++) {
                    if (ty >= yp[i] && ty < yp[i] + 28) {
                        if (hl != i) {
                            mbtn(btn_x, btn_w, yp[hl], lb[hl], fc[hl], false);
                            hl = i;
                            mbtn(btn_x, btn_w, yp[hl], lb[hl], fc[hl], true);
                        }
                        delay(180);
                        switch (hl) {
                            case 0: return 0;
                            case 1: return 1;
                            case 2: return 2;
                            case 3: return 5;
                            case 4: return 3;
                        }
                    }
                }
            }
        }

        if ((b & GB_BTN_UP) && !(prev & GB_BTN_UP)) {
            mbtn(btn_x, btn_w, yp[hl], lb[hl], fc[hl], false);
            hl = (hl == 0) ? MI - 1 : hl - 1;
            mbtn(btn_x, btn_w, yp[hl], lb[hl], fc[hl], true);
        }
        if ((b & GB_BTN_DOWN) && !(prev & GB_BTN_DOWN)) {
            mbtn(btn_x, btn_w, yp[hl], lb[hl], fc[hl], false);
            hl = (hl + 1) % MI;
            mbtn(btn_x, btn_w, yp[hl], lb[hl], fc[hl], true);
        }
        // A = select
        if ((b & GB_BTN_A) && !(prev & GB_BTN_A)) {
            switch (hl) {
                case 0: return 0;
                case 1: return 1;
                case 2: return 2;
                case 3: return 5;
                case 4: return 3;
            }
        }
        // B = cancel -> resume
        if ((b & GB_BTN_B) && !(prev & GB_BTN_B)) {
            return 0;
        }

        prev = b;
        delay(15);
    }
}

// ─── Settings menu ──────────────────────────────────────────────────────────
void launcher_settings_menu(bool* show_fps_overlay, bool* show_save_overlay, bool is_main_menu) {
    uint8_t pal = emu_get_palette();
    uint8_t fs = emu_get_frame_skip();
    uint8_t bl = 255;
    touch_load_settings(&pal, &fs, &bl, show_fps_overlay, show_save_overlay);

    int sel = 0;
    uint16_t prev = 0;

    bool has_bgm = false;
#if ENABLE_SOUND
    has_bgm = bgm_file_exists();
#endif

    int r_idx = 0;
    const int ROW_PALETTE    = r_idx++;
    const int ROW_FRAMESKIP  = r_idx++;
    const int ROW_BRIGHTNESS = r_idx++;
#if ENABLE_SOUND
    const int ROW_VOLUME     = r_idx++;
    const int ROW_BGM        = has_bgm ? (r_idx++) : -1;
#else
    const int ROW_VOLUME     = -1;
    const int ROW_BGM        = -1;
#endif
    const int ROW_BT         = is_main_menu ? (r_idx++) : -1;
    const int ROW_USB        = is_main_menu ? (r_idx++) : -1;
    const int row_done       = r_idx++;
    const int num_rows       = r_idx;

    int row_y[8];
    int row_h[8];

    if (num_rows == 8) {
        row_y[0] = 40;  row_h[0] = 28;
        row_y[1] = 72;  row_h[1] = 28;
        row_y[2] = 104; row_h[2] = 28;
        row_y[3] = 136; row_h[3] = 28;
        row_y[4] = 168; row_h[4] = 28;
        row_y[5] = 200; row_h[5] = 34;
        row_y[6] = 238; row_h[6] = 34;
        row_y[7] = 276; row_h[7] = 32;
    } else if (num_rows == 7) {
        row_y[0] = 40;  row_h[0] = 30;
        row_y[1] = 74;  row_h[1] = 30;
        row_y[2] = 108; row_h[2] = 30;
        row_y[3] = 142; row_h[3] = 30;
        row_y[4] = 176; row_h[4] = 34;
        row_y[5] = 214; row_h[5] = 34;
        row_y[6] = 260; row_h[6] = 32;
    } else if (num_rows == 6) {
        row_y[0] = 44;  row_h[0] = 36;
        row_y[1] = 88;  row_h[1] = 36;
        row_y[2] = 132; row_h[2] = 36;
        row_y[3] = 176; row_h[3] = 36;
        row_y[4] = 220; row_h[4] = 36;
        row_y[5] = 268; row_h[5] = 32;
    } else {
        row_y[0] = 46;  row_h[0] = 38;
        row_y[1] = 92;  row_h[1] = 38;
        row_y[2] = 138; row_h[2] = 38;
        row_y[3] = 184; row_h[3] = 38;
        row_y[4] = 250; row_h[4] = 34;
    }

    auto draw_settings = [&](int selrow) {
        const int row_x = 10;
        const int row_w = SCREEN_W - 20;

        tft.fillScreen(COLOR_SCREEN_BG);
        draw_header("CYDboy");

        // Palette
        if (ROW_PALETTE >= 0) {
            int y = row_y[ROW_PALETTE], h = row_h[ROW_PALETTE];
            uint16_t bg = (selrow == ROW_PALETTE) ? COLOR_CARD_SEL_BG : COLOR_CARD_BG;
            uint16_t border = (selrow == ROW_PALETTE) ? COLOR_CYAN_GLOW : COLOR_CARD_BORDER;
            tft.fillRoundRect(row_x, y, row_w, h, 5, bg);
            tft.drawRoundRect(row_x, y, row_w, h, 5, border);

            tft.setTextColor(COLOR_LIGHT_BLUE, bg);
            tft.setTextDatum(ML_DATUM);
            tft.drawString("Palette:", row_x + 8, y + 8, 1);

            char palstr[32];
            snprintf(palstr, sizeof(palstr), "%s", emu_get_palette_name(pal));
            tft.setTextColor(COLOR_TEXT_WHITE, bg);
            tft.drawString(palstr, row_x + 8, y + 20, is_main_menu ? 1 : 2);

            const uint16_t* c = emu_get_palette_colors(pal);
            int sw_sz = is_main_menu ? 14 : 16;
            int sw_x = SCREEN_W - (is_main_menu ? 86 : 90);
            int sw_y = y + (h - sw_sz) / 2;
            for (int s = 0; s < 4; s++) {
                tft.fillRect(sw_x + s * (sw_sz + 2), sw_y, sw_sz, sw_sz, c[s]);
                tft.drawRect(sw_x + s * (sw_sz + 2), sw_y, sw_sz, sw_sz, COLOR_CYAN_GLOW);
            }
        }

        // Frame skip
        if (ROW_FRAMESKIP >= 0) {
            int y = row_y[ROW_FRAMESKIP], h = row_h[ROW_FRAMESKIP];
            uint16_t bg = (selrow == ROW_FRAMESKIP) ? COLOR_CARD_SEL_BG : COLOR_CARD_BG;
            uint16_t border = (selrow == ROW_FRAMESKIP) ? COLOR_CYAN_GLOW : COLOR_CARD_BORDER;
            tft.fillRoundRect(row_x, y, row_w, h, 5, bg);
            tft.drawRoundRect(row_x, y, row_w, h, 5, border);

            tft.setTextColor(COLOR_LIGHT_BLUE, bg);
            tft.setTextDatum(ML_DATUM);
            tft.drawString("Frame Skip:", row_x + 8, y + 8, 1);

            char fss[32];
            if (fs == 0) snprintf(fss, sizeof(fss), "0 (40 FPS, Accurate)");
            else if (fs == 1) snprintf(fss, sizeof(fss), "1 (60 FPS, Fast)");
            else if (fs == 2) snprintf(fss, sizeof(fss), "2 (60 FPS, Smooth)");
            else snprintf(fss, sizeof(fss), "%d (Skip %d)", fs, fs);

            tft.setTextColor(COLOR_TEXT_WHITE, bg);
            tft.drawString(fss, row_x + 8, y + 20, is_main_menu ? 1 : 2);
        }

        // Brightness
        if (ROW_BRIGHTNESS >= 0) {
            int y = row_y[ROW_BRIGHTNESS], h = row_h[ROW_BRIGHTNESS];
            uint16_t bg = (selrow == ROW_BRIGHTNESS) ? COLOR_CARD_SEL_BG : COLOR_CARD_BG;
            uint16_t border = (selrow == ROW_BRIGHTNESS) ? COLOR_CYAN_GLOW : COLOR_CARD_BORDER;
            tft.fillRoundRect(row_x, y, row_w, h, 5, bg);
            tft.drawRoundRect(row_x, y, row_w, h, 5, border);

            tft.setTextColor(COLOR_LIGHT_BLUE, bg);
            tft.setTextDatum(ML_DATUM);
            tft.drawString("Brightness:", row_x + 8, y + 8, 1);

            int total_steps = 8;
            int active_steps = (bl * total_steps + 127) / 255;
            int bar_x = row_x + 8;
            int bar_y = y + (is_main_menu ? 18 : 20);
            for (int b = 0; b < total_steps; b++) {
                uint16_t bc = (b < active_steps) ? COLOR_CYAN_GLOW : 0x08EB;
                tft.fillRoundRect(bar_x + b * 13, bar_y, 10, 8, 2, bc);
            }

            char bls[16];
            snprintf(bls, sizeof(bls), "%d%%", bl * 100 / 255);
            tft.setTextColor(COLOR_TEXT_WHITE, bg);
            tft.setTextDatum(MR_DATUM);
            tft.drawString(bls, SCREEN_W - row_x - 12, y + h / 2, is_main_menu ? 1 : 2);
        }

#if ENABLE_SOUND
        // Game Sound
        if (ROW_VOLUME >= 0) {
            int y = row_y[ROW_VOLUME], h = row_h[ROW_VOLUME];
            uint16_t bg = (selrow == ROW_VOLUME) ? COLOR_CARD_SEL_BG : COLOR_CARD_BG;
            uint16_t border = (selrow == ROW_VOLUME) ? COLOR_CYAN_GLOW : COLOR_CARD_BORDER;
            tft.fillRoundRect(row_x, y, row_w, h, 5, bg);
            tft.drawRoundRect(row_x, y, row_w, h, 5, border);

            tft.setTextColor(COLOR_LIGHT_BLUE, bg);
            tft.setTextDatum(ML_DATUM);
            tft.drawString("Sound Volume:", row_x + 8, y + 8, 1);

            uint8_t vol = audio_get_volume();
            int bar_x = row_x + 8;
            int bar_y = y + (is_main_menu ? 18 : 20);
            for (int b = 0; b < 3; b++) {
                uint16_t bc = (b < vol) ? COLOR_TURQUOISE : 0x08EB;
                tft.fillRoundRect(bar_x + b * 20, bar_y, 16, 8, 2, bc);
            }

            tft.setTextColor(COLOR_TEXT_WHITE, bg);
            tft.setTextDatum(MR_DATUM);
            tft.drawString(audio_get_volume_str(), SCREEN_W - row_x - 12, y + h / 2, is_main_menu ? 1 : 2);
        }

        // Menu Music (BGM)
        if (ROW_BGM >= 0) {
            int y = row_y[ROW_BGM], h = row_h[ROW_BGM];
            uint16_t bg = (selrow == ROW_BGM) ? COLOR_CARD_SEL_BG : COLOR_CARD_BG;
            uint16_t border = (selrow == ROW_BGM) ? COLOR_CYAN_GLOW : COLOR_CARD_BORDER;
            tft.fillRoundRect(row_x, y, row_w, h, 5, bg);
            tft.drawRoundRect(row_x, y, row_w, h, 5, border);

            tft.setTextColor(COLOR_LIGHT_BLUE, bg);
            tft.setTextDatum(ML_DATUM);
            tft.drawString("Menu Music (BGM):", row_x + 8, y + h / 2, 1);

            bool bgm_on = bgm_is_enabled();
            int btn_h = is_main_menu ? 18 : 24;
            tft.fillRoundRect(SCREEN_W - row_x - 56, y + (h - btn_h) / 2, 48, btn_h, 4, bgm_on ? COLOR_TURQUOISE : 0x21AE);
            tft.setTextColor(bgm_on ? TFT_BLACK : COLOR_TEXT_ICE, bgm_on ? COLOR_TURQUOISE : 0x21AE);
            tft.setTextDatum(MC_DATUM);
            tft.drawString(bgm_on ? "ON" : "OFF", SCREEN_W - row_x - 32, y + h / 2, 1);
        }
#endif

        // Bluetooth Gamepad
        if (ROW_BT >= 0) {
            int y = row_y[ROW_BT], h = row_h[ROW_BT];
            uint16_t bg = (selrow == ROW_BT) ? COLOR_CARD_SEL_BG : COLOR_CARD_BG;
            uint16_t border = (selrow == ROW_BT) ? COLOR_CYAN_GLOW : COLOR_CARD_BORDER;
            tft.fillRoundRect(row_x, y, row_w, h, 5, bg);
            tft.drawRoundRect(row_x, y, row_w, h, 5, border);

            tft.fillRoundRect(row_x + 6, y + 6, 32, 22, 4, COLOR_LIGHT_BLUE);
            tft.setTextColor(TFT_BLACK, COLOR_LIGHT_BLUE);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("BT", row_x + 22, y + 17, 1);

            tft.setTextColor((selrow == ROW_BT) ? COLOR_TEXT_WHITE : COLOR_TEXT_ICE, bg);
            tft.setTextDatum(ML_DATUM);
            tft.drawString("Bluetooth Gamepad", row_x + 46, y + 17, 2);
        }

        // USB ROM Manager
        if (ROW_USB >= 0) {
            int y = row_y[ROW_USB], h = row_h[ROW_USB];
            uint16_t bg = (selrow == ROW_USB) ? COLOR_CARD_SEL_BG : COLOR_CARD_BG;
            uint16_t border = (selrow == ROW_USB) ? COLOR_CYAN_GLOW : COLOR_CARD_BORDER;
            tft.fillRoundRect(row_x, y, row_w, h, 5, bg);
            tft.drawRoundRect(row_x, y, row_w, h, 5, border);

            tft.fillRoundRect(row_x + 6, y + 6, 32, 22, 4, 0x633E);
            tft.setTextColor(COLOR_TEXT_WHITE, 0x633E);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("USB", row_x + 22, y + 17, 1);

            tft.setTextColor((selrow == ROW_USB) ? COLOR_TEXT_WHITE : COLOR_TEXT_ICE, bg);
            tft.setTextDatum(ML_DATUM);
            tft.drawString("USB ROM Manager", row_x + 46, y + 17, 2);
        }

        // Row DONE Button
        {
            int y = row_y[row_done], h = row_h[row_done];
            uint16_t donebg = (selrow == row_done) ? COLOR_CYAN_GLOW : 0x1949;
            uint16_t border = (selrow == row_done) ? COLOR_TEXT_WHITE : COLOR_CYAN_GLOW;
            uint16_t textc = (selrow == row_done) ? TFT_BLACK : COLOR_TEXT_WHITE;
            tft.fillRoundRect(SCREEN_W / 2 - 60, y, 120, h, 5, donebg);
            tft.drawRoundRect(SCREEN_W / 2 - 60, y, 120, h, 5, border);
            tft.setTextColor(textc, donebg);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("DONE", SCREEN_W / 2, y + h / 2, 2);
        }
    };

    draw_settings(sel);
    wait_release();

    while (true) {
        button_update();
        uint16_t b = button_get_buttons();

        if (touch_is_pressed()) {
            int16_t tx = touch_get_x(), ty = touch_get_y();
            for (int i = 0; i < num_rows; i++) {
                if (ty >= row_y[i] && ty < row_y[i] + row_h[i]) {
                    if (i == ROW_PALETTE) {
                        if (tx < 120) pal = (pal + NUM_PALETTES - 1) % NUM_PALETTES;
                        else pal = (pal + 1) % NUM_PALETTES;
                        emu_set_palette(pal);
                        draw_settings(i);
                        delay(200);
                    } else if (i == ROW_FRAMESKIP) {
                        if (tx < 120 && fs > 0) fs--;
                        else if (tx >= 120 && fs < 4) fs++;
                        emu_set_frame_skip(fs);
                        draw_settings(i);
                        delay(200);
                    } else if (i == ROW_BRIGHTNESS) {
                        if (tx < 120 && bl > 30) bl -= 25;
                        else if (tx >= 120 && bl < 255) bl = min(255, bl + 25);
                        display_set_backlight(bl);
                        draw_settings(i);
                        delay(200);
#if ENABLE_SOUND
                    } else if (ROW_VOLUME >= 0 && i == ROW_VOLUME) {
                        if (tx < 120) {
                            uint8_t v = audio_get_volume();
                            if (v > 0) audio_set_volume(v - 1);
                        } else {
                            uint8_t v = audio_get_volume();
                            if (v < 3) audio_set_volume(v + 1);
                        }
                        draw_settings(i);
                        delay(200);
                    } else if (ROW_BGM >= 0 && i == ROW_BGM) {
                        bgm_set_enabled(!bgm_is_enabled());
                        draw_settings(i);
                        delay(200);
#endif
                    } else if (ROW_BT >= 0 && i == ROW_BT) {
                        bt_controller_ui_show();
                        display_clear(COLOR_SCREEN_BG);
                        draw_settings(i);
                        wait_release();
                    } else if (ROW_USB >= 0 && i == ROW_USB) {
                        serial_manager_run();
                        display_clear(COLOR_SCREEN_BG);
                        draw_settings(i);
                        wait_release();
                    } else if (i == row_done) {
                        touch_save_settings(pal, fs, bl, false, false);
                        wait_release();
                        return;
                    }
                    break;
                }
            }
        }

        // Navigation
        if ((b & GB_BTN_UP) && !(prev & GB_BTN_UP)) {
            sel = (sel == 0) ? num_rows - 1 : sel - 1;
            draw_settings(sel);
        }
        if ((b & GB_BTN_DOWN) && !(prev & GB_BTN_DOWN)) {
            sel = (sel + 1) % num_rows;
            draw_settings(sel);
        }

        // Row interactions
        if (sel == ROW_PALETTE) {
            if ((b & GB_BTN_LEFT) && !(prev & GB_BTN_LEFT)) {
                pal = (pal + NUM_PALETTES - 1) % NUM_PALETTES;
                emu_set_palette(pal);
                draw_settings(sel);
            }
            if ((b & GB_BTN_RIGHT) && !(prev & GB_BTN_RIGHT)) {
                pal = (pal + 1) % NUM_PALETTES;
                emu_set_palette(pal);
                draw_settings(sel);
            }
        } else if (sel == ROW_FRAMESKIP) {
            if ((b & GB_BTN_LEFT) && !(prev & GB_BTN_LEFT)) {
                if (fs > 0) { fs--; emu_set_frame_skip(fs); draw_settings(sel); }
            }
            if ((b & GB_BTN_RIGHT) && !(prev & GB_BTN_RIGHT)) {
                if (fs < 4) { fs++; emu_set_frame_skip(fs); draw_settings(sel); }
            }
        } else if (sel == ROW_BRIGHTNESS) {
            if ((b & GB_BTN_LEFT) && !(prev & GB_BTN_LEFT)) {
                if (bl > 30) { bl -= 25; display_set_backlight(bl); draw_settings(sel); }
            }
            if ((b & GB_BTN_RIGHT) && !(prev & GB_BTN_RIGHT)) {
                if (bl < 255) { bl = min(255, bl + 25); display_set_backlight(bl); draw_settings(sel); }
            }
#if ENABLE_SOUND
        } else if (ROW_VOLUME >= 0 && sel == ROW_VOLUME) {
            if ((b & GB_BTN_LEFT) && !(prev & GB_BTN_LEFT)) {
                uint8_t v = audio_get_volume();
                if (v > 0) { audio_set_volume(v - 1); draw_settings(sel); }
            }
            if ((b & GB_BTN_RIGHT) && !(prev & GB_BTN_RIGHT)) {
                uint8_t v = audio_get_volume();
                if (v < 3) { audio_set_volume(v + 1); draw_settings(sel); }
            }
        } else if (ROW_BGM >= 0 && sel == ROW_BGM) {
            if (((b & GB_BTN_LEFT) && !(prev & GB_BTN_LEFT)) || ((b & GB_BTN_RIGHT) && !(prev & GB_BTN_RIGHT))) {
                bgm_set_enabled(!bgm_is_enabled());
                draw_settings(sel);
            }
#endif
        } else if (sel == row_done) {
            if ((b & GB_BTN_A) && !(prev & GB_BTN_A)) {
                touch_save_settings(pal, fs, bl, false, false);
                wait_release();
                return;
            }
        }

        // A button
        if ((b & GB_BTN_A) && !(prev & GB_BTN_A)) {
#if ENABLE_SOUND
            if (ROW_VOLUME >= 0 && sel == ROW_VOLUME) {
                audio_cycle_volume();
                draw_settings(sel);
            } else if (ROW_BGM >= 0 && sel == ROW_BGM) {
                bgm_set_enabled(!bgm_is_enabled());
                draw_settings(sel);
            } else
#endif
            if (ROW_BT >= 0 && sel == ROW_BT) {
                bt_controller_ui_show();
                display_clear(COLOR_SCREEN_BG);
                draw_settings(sel);
                wait_release();
            } else if (ROW_USB >= 0 && sel == ROW_USB) {
                serial_manager_run();
                display_clear(COLOR_SCREEN_BG);
                draw_settings(sel);
                wait_release();
            } else if (sel < row_done) {
                sel = (sel + 1) % num_rows;
                draw_settings(sel);
            }
        }

        // B button cancels / saves & exits
        if ((b & GB_BTN_B) && !(prev & GB_BTN_B)) {
            touch_save_settings(pal, fs, bl, false, false);
            wait_release();
            return;
        }

        prev = b;
        delay(20);
    }
}
