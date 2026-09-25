#include "wifi_upload.h"
#include "hw_config.h"
#include "display.h"
#include "touch_input.h"
#include "button_input.h"
#include "sd_manager.h"
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>

static WebServer server(80);
static File upload_file;
static char cur_upload_name[48] = {0};
static int cur_upload_pct = 0;

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CYD-GB WiFi Upload</title>
<style>
body{background:#0d1117;color:#e6edf3;font-family:-apple-system,BlinkMacSystemFont,sans-serif;margin:0;padding:20px;text-align:center}
.card{background:#161b22;border:1px solid #30363d;border-radius:8px;max-width:500px;margin:20px auto;padding:24px}
h1{color:#00e599;font-size:22px;margin-bottom:8px}
p{color:#8b949e;font-size:14px;margin-bottom:20px}
.drop{border:2px dashed #30363d;border-radius:8px;padding:30px;cursor:pointer;background:#090d12}
.drop:hover{border-color:#00e599;background:rgba(0,229,153,0.08)}
.btn{background:#00e599;color:#04100c;font-weight:700;border:none;padding:10px 20px;border-radius:6px;cursor:pointer;margin-top:14px}
#log{margin-top:16px;font-size:13px;color:#58a6ff;font-family:monospace}
</style>
</head>
<body>
<div class="card">
<h1>CYD-GB ROM Uploader</h1>
<p>Drag and drop .gb or .gbc ROMs directly to the SD card</p>
<div class="drop" onclick="document.getElementById('fileInput').click()">
<h3>Select or Drop ROMs Here</h3>
<input type="file" id="fileInput" accept=".gb,.gbc" multiple style="display:none" onchange="uploadFiles(this.files)">
</div>
<div id="log">Ready</div>
</div>
<script>
function uploadFiles(files) {
  if(!files.length) return;
  let idx = 0;
  function next() {
    if(idx >= files.length) { document.getElementById('log').textContent = 'All ROMs uploaded successfully!'; return; }
    let f = files[idx++];
    document.getElementById('log').textContent = 'Uploading ' + f.name + '...';
    let fd = new FormData();
    fd.append('file', f, f.name);
    let xhr = new XMLHttpRequest();
    xhr.open('POST', '/upload', true);
    xhr.upload.onprogress = (e) => {
      if(e.lengthComputable) {
        let pct = Math.round((e.loaded / e.total) * 100);
        document.getElementById('log').textContent = 'Uploading ' + f.name + ' (' + pct + '%)';
      }
    };
    xhr.onload = () => {
      if(xhr.status === 200) next();
      else document.getElementById('log').textContent = 'Upload failed for ' + f.name;
    };
    xhr.send(fd);
  }
  next();
}
</script>
</body>
</html>
)rawliteral";

static void draw_wifi_screen(const char* status = nullptr, int pct = -1) {
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, SCREEN_W, 36, 0x18C3);
    tft.setTextColor(TFT_WHITE, 0x18C3);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("WiFi ROM Manager", 10, 18, 2);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(0x7BEF, 0x18C3);
    tft.drawString("CYD-GB", SCREEN_W - 10, 18, 1);

    tft.fillRoundRect(12, 50, SCREEN_W - 24, 210, 6, 0x10A2);
    tft.drawRoundRect(12, 50, SCREEN_W - 24, 210, 6, 0x4A69);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x07E0, 0x10A2);
    tft.drawString("HOTSPOT ACTIVE", SCREEN_W / 2, 75, 2);

    tft.setTextColor(TFT_WHITE, 0x10A2);
    tft.drawString("WiFi: CYD-GB-WiFi", SCREEN_W / 2, 105, 2);
    tft.setTextColor(0xFFE0, 0x10A2);
    tft.drawString("http://192.168.4.1", SCREEN_W / 2, 130, 2);

    if (status) {
        tft.setTextColor(0x7BEF, 0x10A2);
        tft.drawString(status, SCREEN_W / 2, 160, 2);
    } else {
        tft.setTextColor(0x7BEF, 0x10A2);
        tft.drawString("Connect & Open Browser", SCREEN_W / 2, 160, 2);
    }

    if (pct >= 0) {
        int bx = 26, by = 185, bw = SCREEN_W - 52, bh = 18;
        tft.drawRect(bx, by, bw, bh, 0x7BEF);
        int fill = (bw - 4) * pct / 100;
        if (fill > 0) tft.fillRect(bx + 2, by + 2, fill, bh - 4, 0x07E0);
    } else {
        uint64_t total = SD.totalBytes() / (1024 * 1024);
        uint64_t used = SD.usedBytes() / (1024 * 1024);
        uint64_t free_mb = (total > used) ? (total - used) : 0;
        char sdbuf[32];
        snprintf(sdbuf, sizeof(sdbuf), "SD: %llu MB free", free_mb);
        tft.setTextColor(0x7BEF, 0x10A2);
        tft.drawString(sdbuf, SCREEN_W / 2, 195, 2);
    }

    tft.fillRect(0, SCREEN_H - 36, SCREEN_W, 36, 0x18C3);
    tft.setTextColor(0xFFE0, 0x18C3);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Tap Screen or Press B to Exit", SCREEN_W / 2, SCREEN_H - 18, 2);
}

static void handle_upload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        String filename = upload.filename;
        strncpy(cur_upload_name, filename.c_str(), sizeof(cur_upload_name) - 1);
        cur_upload_name[sizeof(cur_upload_name) - 1] = 0;

        String path = filename.endsWith(".gbc") ? "/roms/gbc/" : "/roms/gb/";
        path += filename;

        if (SD.exists(path.c_str())) SD.remove(path.c_str());
        upload_file = SD.open(path.c_str(), FILE_WRITE);
        draw_wifi_screen("Receiving ROM...", 0);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (upload_file) {
            upload_file.write(upload.buf, upload.currentSize);
            cur_upload_pct = (int)((upload.totalSize > 0) ? (upload.currentSize * 100 / upload.totalSize) : 50);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (upload_file) {
            upload_file.flush();
            upload_file.close();
            draw_wifi_screen("Saved successfully!", 100);
        }
    }
}

void wifi_upload_run() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("CYD-GB-WiFi");
    delay(200);

    server.on("/", HTTP_GET, []() {
        server.send_P(200, "text/html", INDEX_HTML);
    });
    server.on("/upload", HTTP_POST, []() {
        server.send(200, "text/plain", "OK");
    }, handle_upload);

    server.begin();
    draw_wifi_screen();

    bool running = true;
    while (running) {
        server.handleClient();
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
        delay(10);
    }

    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    tft.fillScreen(TFT_BLACK);
}
