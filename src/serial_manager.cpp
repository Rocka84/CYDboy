#include "serial_manager.h"
#include "hw_config.h"
#include "display.h"
#include "touch_input.h"
#include "button_input.h"
#include "sd_manager.h"
#include <SD.h>

static void draw_usb_ui(const char* status, const char* filename = nullptr, int progress = -1) {
    tft.fillScreen(TFT_BLACK);

    tft.fillRect(0, 0, SCREEN_W, 36, 0x18C3);
    tft.setTextColor(TFT_WHITE, 0x18C3);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("USB ROM Manager", 10, 18, 2);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(0x7BEF, 0x18C3);
    tft.drawString("CYD-GB", SCREEN_W - 10, 18, 1);

    tft.fillRoundRect(12, 50, SCREEN_W - 24, 210, 6, 0x10A2);
    tft.drawRoundRect(12, 50, SCREEN_W - 24, 210, 6, 0x4A69);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x07E0, 0x10A2);
    tft.drawString("WEB SERIAL ACTIVE", SCREEN_W / 2, 80, 2);

    tft.setTextColor(TFT_WHITE, 0x10A2);
    tft.drawString(status, SCREEN_W / 2, 115, 2);

    if (filename) {
        tft.setTextColor(0xFFE0, 0x10A2);
        char fn[32];
        strncpy(fn, filename, sizeof(fn) - 1);
        fn[sizeof(fn) - 1] = 0;
        tft.drawString(fn, SCREEN_W / 2, 145, 2);
    }

    if (progress >= 0) {
        int bx = 26, by = 175, bw = SCREEN_W - 52, bh = 18;
        tft.drawRect(bx, by, bw, bh, 0x7BEF);
        int fill = (bw - 4) * progress / 100;
        if (fill > 0) tft.fillRect(bx + 2, by + 2, fill, bh - 4, 0x07E0);
        char pstr[10];
        snprintf(pstr, sizeof(pstr), "%d%%", progress);
        tft.setTextColor(TFT_WHITE, 0x10A2);
        tft.drawString(pstr, SCREEN_W / 2, by + bh + 14, 2);
    } else {
        uint64_t total = SD.totalBytes() / (1024 * 1024);
        uint64_t used = SD.usedBytes() / (1024 * 1024);
        uint64_t free_mb = (total > used) ? (total - used) : 0;
        char sdbuf[32];
        snprintf(sdbuf, sizeof(sdbuf), "SD: %llu MB free", free_mb);
        tft.setTextColor(0x7BEF, 0x10A2);
        tft.drawString(sdbuf, SCREEN_W / 2, 185, 2);
    }

    tft.fillRect(0, SCREEN_H - 36, SCREEN_W, 36, 0x18C3);
    tft.setTextColor(0xFFE0, 0x18C3);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Tap Screen or Press B to Exit", SCREEN_W / 2, SCREEN_H - 18, 2);
}

static void update_progress_bar(int progress) {
    int bx = 26, by = 175, bw = SCREEN_W - 52, bh = 18;
    int fill = (bw - 4) * progress / 100;
    tft.fillRect(bx + 2, by + 2, fill, bh - 4, 0x07E0);
    tft.fillRect(bx + 2 + fill, by + 2, (bw - 4) - fill, bh - 4, 0x10A2);
    char pstr[10];
    snprintf(pstr, sizeof(pstr), "%d%%", progress);
    tft.fillRect(SCREEN_W / 2 - 30, by + bh + 4, 60, 20, 0x10A2);
    tft.setTextColor(TFT_WHITE, 0x10A2);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(pstr, SCREEN_W / 2, by + bh + 14, 2);
}

void serial_manager_init() {}

static void handle_info() {
    uint64_t total = SD.totalBytes();
    uint64_t used = SD.usedBytes();
    uint64_t free_b = (total > used) ? (total - used) : 0;
    Serial.printf("CYD:INFO:{\"total\":%llu,\"used\":%llu,\"free\":%llu,\"card_type\":%d}\n",
                  total, used, free_b, SD.cardType());
}

static void handle_list() {
    Serial.print("CYD:LIST:[");
    bool first = true;
    const char* dirs[2] = {ROM_PATH_GB, ROM_PATH_GBC};
    for (int d = 0; d < 2; d++) {
        File dir = SD.open(dirs[d]);
        if (!dir || !dir.isDirectory()) continue;
        File f;
        while ((f = dir.openNextFile())) {
            if (f.isDirectory()) { f.close(); continue; }
            String n = f.name();
            String lo = n; lo.toLowerCase();
            if (lo.endsWith(".gb") || lo.endsWith(".gbc")) {
                if (!first) Serial.print(",");
                first = false;
                String esc_name = "";
                for (size_t c = 0; c < n.length(); c++) {
                    if (n[c] == '"') esc_name += "\\\"";
                    else if (n[c] == '\\') esc_name += "\\\\";
                    else esc_name += n[c];
                }
                Serial.printf("{\"name\":\"%s\",\"path\":\"%s/%s\",\"size\":%u,\"is_gbc\":%s}",
                              esc_name.c_str(), dirs[d], esc_name.c_str(), (uint32_t)f.size(),
                              (d == 1) ? "true" : "false");
            }
            f.close();
        }
        dir.close();
    }
    Serial.println("]");
}

static void handle_put(const String& args) {
    int sp = args.lastIndexOf(' ');
    if (sp < 0) {
        Serial.println("CYD:ERR:invalid put args");
        return;
    }
    String path = args.substring(0, sp);
    path.trim();
    uint32_t size = args.substring(sp + 1).toInt();
    if (path.length() == 0 || size == 0) {
        Serial.println("CYD:ERR:invalid size or path");
        return;
    }

    if (path.startsWith("/roms/gb/") && !SD.exists(ROM_PATH_GB)) SD.mkdir(ROM_PATH_GB);
    if (path.startsWith("/roms/gbc/") && !SD.exists(ROM_PATH_GBC)) SD.mkdir(ROM_PATH_GBC);

    if (SD.exists(path.c_str())) SD.remove(path.c_str());

    File f = SD.open(path.c_str(), FILE_WRITE);
    if (!f) {
        Serial.println("CYD:ERR:cannot open file for write");
        return;
    }

    String filename = path.substring(path.lastIndexOf('/') + 1);
    draw_usb_ui("Receiving ROM...", filename.c_str(), 0);

    Serial.println("CYD:READY");

    uint8_t buf[1024];
    uint32_t received = 0;
    uint32_t last_activity = millis();
    int last_pct = 0;

    while (received < size) {
        while (Serial.available() > 0 && received < size) {
            size_t to_read = min((size_t)Serial.available(), sizeof(buf));
            to_read = min(to_read, (size_t)(size - received));
            size_t r = Serial.readBytes(buf, to_read);
            if (r > 0) {
                f.write(buf, r);
                received += r;
                last_activity = millis();

                int pct = (int)((received * 100ULL) / size);
                if (pct != last_pct) {
                    last_pct = pct;
                    update_progress_bar(pct);
                }
            }
        }
        if (millis() - last_activity > 5000) {
            f.close();
            SD.remove(path.c_str());
            Serial.println("CYD:ERR:receive timeout");
            draw_usb_ui("Transfer timed out");
            return;
        }
        yield();
    }

    f.flush();
    f.close();
    Serial.println("CYD:OK");
    draw_usb_ui("ROM Saved Successfully!", filename.c_str(), 100);
}

static void handle_del(const String& path) {
    String p = path;
    p.trim();
    if (p.length() == 0) {
        Serial.println("CYD:ERR:missing path");
        return;
    }
    if (SD.exists(p.c_str())) {
        if (SD.remove(p.c_str())) Serial.println("CYD:OK");
        else Serial.println("CYD:ERR:remove failed");
    } else {
        Serial.println("CYD:ERR:not found");
    }
}

static void handle_baud(const String& baud_str) {
    uint32_t b = baud_str.toInt();
    if (b < 9600 || b > 2000000) {
        Serial.println("CYD:ERR:invalid baud");
        return;
    }
    Serial.println("CYD:OK");
    Serial.flush();
    delay(50);
    Serial.begin(b);
}

static String handshake_buf = "";

bool serial_manager_check_handshake() {
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            handshake_buf.trim();
            if (handshake_buf.startsWith("CYD:PING") || handshake_buf.startsWith("CYD:")) {
                handshake_buf = "";
                return true;
            }
            handshake_buf = "";
        } else {
            if (handshake_buf.length() < 64) {
                handshake_buf += c;
            }
        }
    }
    return false;
}

void serial_manager_run() {
    draw_usb_ui("Connected & Ready");
    Serial.println("CYD:PONG:CYD-GB:v1.0");

    bool running = true;
    String cmd = "";

    while (running) {
        button_update();
        uint16_t btns = button_get_buttons();
        if (btns & (GB_BTN_B | GB_BTN_MENU)) {
            running = false;
            break;
        }
        if (touch_is_pressed()) {
            int16_t ty = touch_get_y();
            if (ty >= SCREEN_H - 40) {
                running = false;
                break;
            }
        }

        while (Serial.available() > 0) {
            char c = (char)Serial.read();
            if (c == '\r') continue;
            if (c == '\n') {
                cmd.trim();
                if (cmd.startsWith("CYD:PING")) {
                    Serial.println("CYD:PONG:CYD-GB:v1.0");
                    draw_usb_ui("Web Tool Connected");
                } else if (cmd.startsWith("CYD:INFO")) {
                    handle_info();
                } else if (cmd.startsWith("CYD:LIST")) {
                    handle_list();
                } else if (cmd.startsWith("CYD:PUT ")) {
                    handle_put(cmd.substring(8));
                } else if (cmd.startsWith("CYD:DEL ")) {
                    handle_del(cmd.substring(8));
                } else if (cmd.startsWith("CYD:BAUD ")) {
                    handle_baud(cmd.substring(9));
                } else if (cmd.startsWith("CYD:EXIT")) {
                    Serial.println("CYD:OK");
                    running = false;
                    break;
                }
                cmd = "";
            } else {
                if (cmd.length() < 256) cmd += c;
            }
        }
        delay(10);
    }

    tft.fillScreen(TFT_BLACK);
}
