#include <Arduino.h>
#include "hw_config.h"
#include "display.h"
#include "touch_input.h"
#include "button_input.h"
#include "sd_manager.h"
#include "ui_launcher.h"
#include "emulator_bridge.h"
#include "bt_controller.h"
#include "serial_manager.h"
#include "audio_output.h"
#include "bgm_player.h"
//#include "wifi_upload.h"

static RomEntry* roms = nullptr;
static int rcnt = 0;
static char cur_path[MAX_PATHLEN] = {0};
static volatile bool emu_on = false, menu_req = false;
static bool show_fps_overlay = false;
static bool show_sd_save_overlay = false;
static bool has_saved_settings = false;

static void save_ram() {
    if(!cur_path[0]) return;
    uint32_t sz=0; uint8_t* r=emu_get_cart_ram(&sz);
    if(sz>0) {
        bool ok = sd_save_state(cur_path,r,sz);
        Serial.printf("[SAVE] %u bytes (%s)\n",sz, ok ? "ok" : "fail");
    }
}

static void load_ram() {
    if(!cur_path[0]) return;
    uint32_t sz=0;
    uint8_t* cart_ram = emu_get_cart_ram(&sz);
    if (sz == 0) {
        Serial.println("[SAVE] Load skipped: cart RAM size is 0");
        return;
    }

    if (!cart_ram) {
        Serial.println("[SAVE] Load failed: cart RAM pointer is null");
        return;
    }

    if (sd_load_state(cur_path, cart_ram, sz)) {
        Serial.printf("[SAVE] Loaded %u bytes\n", sz);
    } else {
        Serial.printf("[SAVE] No load for %s\n", cur_path);
    }
}

// ─── Emulation loop ─────────────────────────────────────────────────────────
void run_emu() {
    emu_on = true; menu_req = false;
    bool prev_menu_combo = false;
    display_clear(TFT_BLACK);
    bool controls_visible = !bt_controller_is_connected();
    if (controls_visible) display_draw_controls();
    else display_clear_controls();

    while(emu_on) {
        button_update();
        bool bt_connected = bt_controller_is_connected();
        if (bt_connected == controls_visible) {
            controls_visible = !bt_connected;
            if (controls_visible) display_draw_controls();
            else display_clear_controls();
        }

        uint16_t b = button_get_buttons();
        bool menu_combo = ((b & (GB_BTN_START | GB_BTN_SELECT)) == (GB_BTN_START | GB_BTN_SELECT)) || (b & GB_BTN_MENU);
        if (menu_combo && !prev_menu_combo) {
            menu_req = true;
        }
        prev_menu_combo = menu_combo;
        if (menu_combo) {
            emu_set_joypad(b & ~(GB_BTN_START | GB_BTN_SELECT | GB_BTN_MENU));
        } else {
            emu_set_joypad(b & 0xFF);
        }

        emu_run_frame();

        if (menu_req) {
            menu_req = false;
            audio_enable_output(false);
            int c = launcher_ingame_menu();
            switch(c) {
                case 0: break;  // resume
                case 1: { // save state
                    bool ok = emu_save_state(cur_path);
                    if (ok) save_ram();
                    tft.fillRect(30,80,SCREEN_W-60,40,TFT_BLACK);
                    tft.drawRoundRect(30,80,SCREEN_W-60,40,4, ok ? 0x07E0 : 0xF800);
                    tft.setTextDatum(MC_DATUM); tft.setTextColor(ok ? 0x07E0 : 0xF800);
                    tft.drawString(ok ? "STATE SAVED!" : "SAVE FAILED!",SCREEN_W/2,100,2);
                    delay(700);
                    break;
                }
                case 2: { // load state
                    bool ok = emu_load_state(cur_path);
                    tft.fillRect(30,80,SCREEN_W-60,40,TFT_BLACK);
                    tft.drawRoundRect(30,80,SCREEN_W-60,40,4, ok ? 0x07FF : 0xF800);
                    tft.setTextDatum(MC_DATUM); tft.setTextColor(ok ? 0x07FF : 0xF800);
                    tft.drawString(ok ? "STATE LOADED!" : "NO SAVE STATE!",SCREEN_W/2,100,2);
                    delay(700);
                    break;
                }
                case 3:  // quit
                    emu_on=false; save_ram(); return;
                case 5:  // settings
                    launcher_settings_menu(&show_fps_overlay, &show_sd_save_overlay); break;
            }
            display_clear(TFT_BLACK);
            audio_reset();
            if (audio_get_volume() != AUDIO_VOL_MUTE) {
                audio_enable_output(true);
            } else {
                audio_enable_output(false);
            }
            controls_visible = !bt_controller_is_connected();
            if (controls_visible) display_draw_controls();
            else display_clear_controls();
        }

        taskYIELD();
    }
}

static void run_bt_scanner() {
    bt_controller_ui_show();
    display_clear(TFT_BLACK);
}

// ─── Setup ──────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200); delay(200);
    Serial.println("\n=== CYD-GB ===");
    pinMode(LED_R_PIN, OUTPUT);
    if (LED_G_PIN >= 0) pinMode(LED_G_PIN, OUTPUT);
    if (LED_B_PIN >= 0) pinMode(LED_B_PIN, OUTPUT);
    digitalWrite(LED_R_PIN, HIGH);
    if (LED_G_PIN >= 0) digitalWrite(LED_G_PIN, HIGH);
    if (LED_B_PIN >= 0) digitalWrite(LED_B_PIN, HIGH);

    display_init();
    touch_init();
    button_init();
    audio_init();
    bt_controller_init();

    if(!sd_init()) {
        tft.fillScreen(TFT_BLACK); tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_RED); tft.drawString("SD Card Error!",SCREEN_W/2,100,4);
        tft.setTextColor(0x7BEF); tft.drawString("Insert FAT32 SD & reset",SCREEN_W/2,140,2);
        while(true) delay(1000);
    }

    bgm_init();

    // Splash
    tft.fillScreen(TFT_BLACK); tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x07E0); tft.drawString("CYDboy",SCREEN_W/2,70,4);
    tft.setTextColor(0x7BEF); tft.drawString("Game Boy Emulator",SCREEN_W/2,110,2);
    uint32_t splash_start = millis();
    while (millis() - splash_start < 1200) {
        bt_controller_update();
        delay(20);
    }

    // Auto-run calibration on boot if no calibration data is present
    if (!touch_has_calibration()) {
        Serial.println("[INIT] No calibration found. Launching calibration...");
        touch_run_calibration();
    }

    // Load saved settings from NVS
    uint8_t s_pal, s_fs, s_bl;
    if (touch_load_settings(&s_pal, &s_fs, &s_bl, &show_fps_overlay, &show_sd_save_overlay)) {
        has_saved_settings = true;
        emu_set_palette(s_pal);
        emu_set_frame_skip(s_fs);
        display_set_backlight(s_bl);
        Serial.printf("[INIT] Loaded settings: pal=%d fs=%d bl=%d fps_ov=%d save_ov=%d\n",
                      s_pal, s_fs, s_bl, (int)show_fps_overlay, (int)show_sd_save_overlay);
    }

    Serial.printf("[INIT] FreeHeap: %u, MaxAlloc: %u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

// ─── Loop ───────────────────────────────────────────────────────────────────
void loop() {
    if (!roms) roms = (RomEntry*)malloc(sizeof(RomEntry) * MAX_ROMS);
    if (!roms) {
        Serial.println("[MAIN] Failed to alloc roms list");
        delay(1000);
        return;
    }
    rcnt = sd_scan_roms(roms, MAX_ROMS);
    bgm_start();
    int sel = launcher_show(roms, rcnt);
    bgm_stop();
    if (sel == LAUNCHER_SEL_BT_SCANNER) {
        if (roms) { free(roms); roms = nullptr; }
        run_bt_scanner();
        return;
    }
    if (sel == LAUNCHER_SEL_USB_MANAGER) {
        if (roms) { free(roms); roms = nullptr; }
        serial_manager_run();
        return;
    }
    if (sel == LAUNCHER_SEL_SETTINGS) {
        if (roms) { free(roms); roms = nullptr; }
        launcher_settings_menu(&show_fps_overlay, &show_sd_save_overlay);
        return;
    }
    // if (sel == LAUNCHER_SEL_WIFI_UPLOAD) {
    //     wifi_upload_run();
    //     return;
    // }
    if (sel < 0 || sel >= rcnt) {
        if (roms) { free(roms); roms = nullptr; }
        return;
    }

    strncpy(cur_path, roms[sel].full_path, 79);
    cur_path[79] = 0;

    // Loading screen
    tft.fillScreen(TFT_BLACK); tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x07E0); tft.drawString("Loading...", SCREEN_W/2, 90, 4);
    char nm[30]; strncpy(nm, roms[sel].filename, 28); nm[28] = 0;
    char* d = strrchr(nm, '.'); if (d) *d = 0;
    tft.setTextColor(TFT_WHITE); tft.drawString(nm, SCREEN_W/2, 130, 2);

    if (roms) { free(roms); roms = nullptr; }

    if (!emu_open_rom(cur_path)) {
        tft.setTextColor(TFT_RED); tft.drawString("Open failed!",SCREEN_W/2,170,2); delay(2000); return;
    }
    if(!emu_init(0,0)){
        tft.setTextColor(TFT_RED);
        char errbuf[64];
        snprintf(errbuf, sizeof(errbuf), "Init: %s", emu_get_error());
        tft.drawString(errbuf, SCREEN_W/2, 170, 2);
        delay(3000);
        emu_close_rom();
        return;
    }

    load_ram();
    if (!has_saved_settings) emu_set_frame_skip(1);
    if (LED_G_PIN >= 0) digitalWrite(LED_G_PIN, LOW);
    run_emu();
    if (LED_G_PIN >= 0) digitalWrite(LED_G_PIN, HIGH);
    emu_close_rom();
}
