#include "emulator_bridge.h"
#include "display.h"
#include "hw_config.h"
#include "sd_manager.h"
#include <Arduino.h>
#include <string.h>
#include <SD.h>

#define ENABLE_LCD 1
#define ENABLE_SOUND 0
#define PEANUT_GB_HIGH_LCD_ACCURACY 0
#include "peanut_gb.h"

// ─── Page cache ─────────────────────────────────────────────────────────────
#define PG_SZ 4096
#define PG_N  12
#define PG_MASK (PG_SZ-1)
#define HASH_SZ 32
#define HASH_M (HASH_SZ-1)

struct Pg { uint32_t addr, acc; uint8_t* d; bool v; };
static Pg pg[PG_N];
static int8_t ht[HASH_SZ];
static uint32_t acc = 0;
static int npg = 0;
static File romf;
static uint32_t romlen = 0;

#define B0SZ (32*1024)
static uint8_t* b0 = nullptr;

static uint32_t cmiss = 0;
static uint32_t emu_time_us = 0;
static uint32_t render_time_us = 0;

static inline uint8_t* IRAM_ATTR cget(uint32_t a) {
    uint32_t pb = a & ~PG_MASK;
    int8_t i = ht[(pb>>12)&HASH_M];
    if (i >= 0 && pg[i].v && pg[i].addr == pb) { pg[i].acc = ++acc; return &pg[i].d[a&PG_MASK]; }
    for (int j=0;j<npg;j++) if (pg[j].v && pg[j].addr==pb) {
        pg[j].acc=++acc; ht[(pb>>12)&HASH_M]=j; return &pg[j].d[a&PG_MASK];
    }
    int lru=0; uint32_t old=UINT32_MAX;
    for (int j=0;j<npg;j++) { if (!pg[j].v){lru=j;break;} if(pg[j].acc<old){old=pg[j].acc;lru=j;} }
    if (pg[lru].v) { int8_t oh=(pg[lru].addr>>12)&HASH_M; if(ht[oh]==lru) ht[oh]=-1; }
    cmiss++;
    romf.seek(pb); size_t r=romf.read(pg[lru].d, min((uint32_t)PG_SZ,romlen-pb));
    if (r<PG_SZ) memset(pg[lru].d+r,0xFF,PG_SZ-r);
    pg[lru].addr=pb; pg[lru].acc=++acc; pg[lru].v=true; ht[(pb>>12)&HASH_M]=lru;
    return &pg[lru].d[a&PG_MASK];
}

// ─── State ──────────────────────────────────────────────────────────────────
static struct gb_s* gb = nullptr;
static uint8_t* cram = nullptr;
static uint32_t cram_sz = 0;
static uint16_t lbuf[GB_SCREEN_W];
static uint8_t fskip = 0, fcnt = 0;
static uint32_t fpsc = 0, fpst = 0, cfps = 0;
static uint8_t jpad = 0;

// ─── 20 Palettes (SW is identity for testing; remove pre-swapped values) ──
// Change this to identity to test driver-side byte ordering (use with setSwapBytes).
#define SW(c) (uint16_t)(c)

static const uint16_t pals[NUM_PALETTES][4] = {
    {SW(0x9FE5),SW(0x4F64),SW(0x2542),SW(0x0261)}, //  0 Classic Green
    {SW(0xFFFF),SW(0xAD55),SW(0x52AA),SW(0x0000)}, //  1 Original DMG
    {SW(0xFFFF),SW(0xB596),SW(0x6B4D),SW(0x0000)}, //  2 Pocket Gray
    {SW(0xFFDF),SW(0xD68F),SW(0x7A4B),SW(0x1082)}, //  3 Warm Sepia
    {SW(0xBF5F),SW(0x6CDF),SW(0x339F),SW(0x0019)}, //  4 Cool Blue
    {SW(0xFFF0),SW(0xFC00),SW(0x8800),SW(0x2000)}, //  5 Autumn
    {SW(0xE71C),SW(0x9CD3),SW(0x4228),SW(0x0000)}, //  6 Grayscale
    {SW(0xFFFF),SW(0xFE20),SW(0xC800),SW(0x4000)}, //  7 Lava
    {SW(0xAFFF),SW(0x5F5F),SW(0x2D1F),SW(0x0019)}, //  8 Ocean
    {SW(0xFFF0),SW(0xBDE0),SW(0x5AE0),SW(0x0120)}, //  9 Forest
    {SW(0xFFFF),SW(0xFD20),SW(0xAB00),SW(0x4000)}, // 10 Sunset
    {SW(0xFFDF),SW(0xF71C),SW(0xAA13),SW(0x3808)}, // 11 Cherry
    {SW(0xCFFF),SW(0x867F),SW(0x433F),SW(0x0019)}, // 12 Ice
    {SW(0xFFB6),SW(0xD52A),SW(0x8A08),SW(0x3000)}, // 13 Chocolate
    {SW(0xFFFF),SW(0xBF5F),SW(0x5F1F),SW(0x0019)}, // 14 Mint
    {SW(0xFFF8),SW(0xFCC0),SW(0xC880),SW(0x6000)}, // 15 Peach
    {SW(0xEF3C),SW(0x867F),SW(0x4179),SW(0x0000)}, // 16 Lavender
    {SW(0xFFFF),SW(0x07FF),SW(0x001F),SW(0x0000)}, // 17 Neon
    {SW(0x0000),SW(0x4228),SW(0xAD55),SW(0xFFFF)}, // 18 Inverted
    {SW(0xFE60),SW(0xAB00),SW(0x5000),SW(0x0000)}, // 19 Gold
};
static const char* palnames[NUM_PALETTES] = {
    "Classic Green","Original DMG","Pocket Gray","Warm Sepia","Cool Blue",
    "Autumn","Grayscale","Lava","Ocean","Forest",
    "Sunset","Cherry","Ice","Chocolate","Mint",
    "Peach","Lavender","Neon","Inverted","Gold"
};
static uint8_t curpal = 0;

void emu_set_palette(uint8_t i) { if (i<NUM_PALETTES) curpal=i; }
uint8_t emu_get_palette() { return curpal; }
const char* emu_get_palette_name(uint8_t i) { return (i<NUM_PALETTES)?palnames[i]:"?"; }

// ─── Callbacks ──────────────────────────────────────────────────────────────
static uint8_t IRAM_ATTR gb_rom_read(struct gb_s* g, const uint_fast32_t a) {
    (void)g; if(a>=romlen) return 0xFF; if(a<B0SZ) return b0[a]; return *cget(a);
}
static uint8_t IRAM_ATTR gb_cram_r(struct gb_s* g, const uint_fast32_t a) {
    (void)g; return (cram && a<cram_sz)?cram[a]:0xFF;
}
static void IRAM_ATTR gb_cram_w(struct gb_s* g, const uint_fast32_t a, const uint8_t v) {
    (void)g;
    if(cram && a<cram_sz) cram[a]=v;
}
static void gb_err(struct gb_s* g, const enum gb_error_e e, const uint16_t a) {
    (void)g; Serial.printf("[EMU] Err %d @0x%04X\n",(int)e,a);
}
static void IRAM_ATTR lcd_line(struct gb_s* g, const uint8_t px[160], const uint_fast8_t ln) {
    (void)g;
    if (fskip>0 && (fcnt%(fskip+1))!=0) return;
    uint32_t t0 = micros();
    const uint16_t* p = pals[curpal];
    for (int x=0;x<GB_SCREEN_W;x++) lbuf[x]=p[px[x]&3];
    display_push_gb_line(ln, lbuf);
    render_time_us += (micros() - t0);
}

// ─── API ────────────────────────────────────────────────────────────────────
bool emu_open_rom(const char* path) {
    if (romf) romf.close();
    romf = SD.open(path, FILE_READ);
    if (!romf) {
        Serial.printf("[EMU] Failed to open ROM: %s\n", path);
        return false;
    }
    romlen = romf.size();
    Serial.printf("[EMU] SD ROM: %s (%u KB)\n", path, romlen / 1024);
    return true;
}
void emu_close_rom() {
    if (romf) romf.close();
    romlen = 0;
    for (int i = 0; i < PG_N; i++) {
        if (pg[i].d) { free(pg[i].d); pg[i].d = nullptr; }
        pg[i].v = false;
    }
    npg = 0;
    if (b0) { free(b0); b0 = nullptr; }
    if (cram) { free(cram); cram = nullptr; cram_sz = 0; }
    if (gb) { free(gb); gb = nullptr; }
    Serial.printf("[EMU] Closed. Free heap: %u\n", ESP.getFreeHeap());
}

bool emu_init(uint8_t*, uint32_t) {
    if (!romf || !romlen) {
        Serial.println("[EMU] Init failed: no ROM open");
        return false;
    }

    Serial.printf("[EMU] Init: free heap=%u, max alloc=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    if (!gb) gb = (struct gb_s*)malloc(sizeof(struct gb_s));
    if (!gb) {
        Serial.printf("[EMU] Failed to alloc gb (%u bytes)\n", sizeof(struct gb_s));
        return false;
    }
    memset(gb, 0, sizeof(struct gb_s));

    if (!b0) b0 = (uint8_t*)malloc(B0SZ);
    if (!b0) {
        Serial.printf("[EMU] Failed to alloc b0 (%u bytes)\n", B0SZ);
        return false;
    }
    romf.seek(0);
    romf.read(b0, min(romlen, (uint32_t)B0SZ));

    uint32_t req_ram = 0;
    if (b0[0x0147] == 0x05 || b0[0x0147] == 0x06) {
        req_ram = 512;
    } else {
        uint8_t rc = b0[0x0149];
        if (rc == 1) req_ram = 2048;
        else if (rc == 2) req_ram = 8192;
        else if (rc >= 3) req_ram = 32768;
    }

    if (req_ram > 0) {
        if (!cram || cram_sz != req_ram) {
            if (cram) free(cram);
            cram = (uint8_t*)malloc(req_ram);
        }
        if (!cram) {
            Serial.printf("[EMU] Failed to alloc cram (%u bytes)\n", req_ram);
            return false;
        }
        cram_sz = req_ram;
        memset(cram, 0xFF, cram_sz);
    } else {
        if (cram) { free(cram); cram = nullptr; }
        cram_sz = 0;
    }

    enum gb_init_error_e r = gb_init(gb, gb_rom_read, gb_cram_r, gb_cram_w, gb_err, nullptr);
    if (r != GB_INIT_NO_ERROR) {
        Serial.printf("[EMU] gb_init failed: %d\n", (int)r);
        return false;
    }
    gb_init_lcd(gb, lcd_line);

    memset(ht, -1, sizeof(ht));
    npg = 0;
    for (int i = 0; i < PG_N; i++) {
        pg[i].v = false;
        if (!pg[i].d) pg[i].d = (uint8_t*)malloc(PG_SZ);
        if (!pg[i].d) break;
        npg++;
    }
    Serial.printf("[EMU] %d pages allocated. RAM: %u. Free heap: %u\n", npg, cram_sz, ESP.getFreeHeap());

    if (npg < 4) {
        Serial.printf("[EMU] Not enough pages (<4)\n");
        return false;
    }

    fcnt = fpsc = cfps = 0;
    fpst = millis();
    acc = 0;
    char t[17] = {0};
    for (int i = 0; i < 16; i++) {
        char c = (char)b0[0x134 + i];
        t[i] = (c >= 32 && c < 127) ? c : 0;
    }
    Serial.printf("[EMU] '%s' %uKB heap:%u\n", t, romlen / 1024, ESP.getFreeHeap());
    return true;
}

void emu_run_frame() {
    gb->direct.joypad_bits.a=!(jpad&0x10); gb->direct.joypad_bits.b=!(jpad&0x20);
    gb->direct.joypad_bits.select=!(jpad&0x40); gb->direct.joypad_bits.start=!(jpad&0x80);
    gb->direct.joypad_bits.right=!(jpad&0x01); gb->direct.joypad_bits.left=!(jpad&0x02);
    gb->direct.joypad_bits.up=!(jpad&0x04); gb->direct.joypad_bits.down=!(jpad&0x08);
    uint32_t t0 = micros();
    gb_run_frame(gb);
    emu_time_us += (micros() - t0);
    fcnt++; fpsc++;
    uint32_t n=millis();
    if(n-fpst>=1000){
        cfps=fpsc;
        uint32_t e_avg = fpsc ? (emu_time_us / fpsc) : 0;
        uint32_t r_avg = fpsc ? (render_time_us / fpsc) : 0;
        Serial.printf("[PERF] FPS: %u | emu: %u ms | render: %u ms | misses/s: %u | heap: %u\n",
                      cfps, e_avg / 1000, r_avg / 1000, cmiss, ESP.getFreeHeap());
        fpsc=0; fpst=n; cmiss=0; emu_time_us=0; render_time_us=0;
    }
}

void emu_set_joypad(uint8_t b){jpad=b;}
uint8_t* emu_get_cart_ram(uint32_t* s){if(s)*s=cram_sz;return cram;}
void emu_set_cart_ram(const uint8_t* d,uint32_t s){if(cram&&d&&s>0){if(s>cram_sz)s=cram_sz;memcpy(cram,d,s);}}
bool emu_cart_ram_dirty(){return false;}
uint32_t emu_get_cart_ram_last_write_ms(){return 0;}
void emu_clear_cart_ram_dirty(){}
void emu_set_frame_skip(uint8_t s){fskip=s;}
uint8_t emu_get_frame_skip(){return fskip;}
uint32_t emu_get_fps(){return cfps;}
uint16_t* emu_get_line_buffer(){return lbuf;}
void emu_reset(){gb_reset(gb);fcnt=0;}

struct SaveStateHeader {
    char     magic[8];
    uint32_t version;
    uint32_t struct_sz;
    uint32_t cram_sz;
    uint8_t  palette_idx;
    uint8_t  reserved[7];
};

bool emu_save_state(const char* rom_path) {
    if (!gb || !rom_path || !rom_path[0]) return false;
    char path[96];
    sd_get_state_path(rom_path, path, sizeof(path));
    if (SD.exists(path)) SD.remove(path);
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        Serial.printf("[STATE] Save open failed: %s\n", path);
        return false;
    }

    SaveStateHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, "CYDGBS1", 8);
    hdr.version = 1;
    hdr.struct_sz = sizeof(struct gb_s);
    hdr.cram_sz = (cram && cram_sz > 0) ? cram_sz : 0;
    hdr.palette_idx = curpal;

    if (f.write((const uint8_t*)&hdr, sizeof(hdr)) != sizeof(hdr)) {
        f.close();
        return false;
    }

    if (f.write((const uint8_t*)gb, sizeof(struct gb_s)) != sizeof(struct gb_s)) {
        f.close();
        return false;
    }

    if (hdr.cram_sz > 0 && cram) {
        if (f.write(cram, hdr.cram_sz) != hdr.cram_sz) {
            f.close();
            return false;
        }
    }

    f.close();
    Serial.printf("[STATE] State saved: %s (size: %u + %u)\n", path, (unsigned)sizeof(struct gb_s), (unsigned)hdr.cram_sz);
    return true;
}

bool emu_load_state(const char* rom_path) {
    if (!gb || !rom_path || !rom_path[0]) return false;
    char path[96];
    sd_get_state_path(rom_path, path, sizeof(path));
    if (!SD.exists(path)) {
        Serial.printf("[STATE] File not found: %s\n", path);
        return false;
    }

    File f = SD.open(path, FILE_READ);
    if (!f) {
        Serial.printf("[STATE] Load open failed: %s\n", path);
        return false;
    }

    SaveStateHeader hdr;
    if (f.read((uint8_t*)&hdr, sizeof(hdr)) != sizeof(hdr)) {
        f.close();
        return false;
    }

    if (memcmp(hdr.magic, "CYDGBS1", 8) != 0 || hdr.version != 1) {
        Serial.println("[STATE] Magic or version mismatch");
        f.close();
        return false;
    }

    if (hdr.struct_sz != sizeof(struct gb_s)) {
        Serial.printf("[STATE] Struct size mismatch: %u != %u\n", hdr.struct_sz, (unsigned)sizeof(struct gb_s));
        f.close();
        return false;
    }

    uint8_t (*rom_r)(struct gb_s*, const uint_fast32_t) = gb->gb_rom_read;
    uint8_t (*cram_r)(struct gb_s*, const uint_fast32_t) = gb->gb_cart_ram_read;
    void (*cram_w)(struct gb_s*, const uint_fast32_t, const uint8_t) = gb->gb_cart_ram_write;
    void (*err)(struct gb_s*, const enum gb_error_e, const uint16_t) = gb->gb_error;
    void (*ser_tx)(struct gb_s*, const uint8_t) = gb->gb_serial_tx;
    enum gb_serial_rx_ret_e (*ser_rx)(struct gb_s*, uint8_t*) = gb->gb_serial_rx;
    uint8_t (*boot_r)(struct gb_s*, const uint_fast16_t) = gb->gb_bootrom_read;
    void (*lcd_line_fn)(struct gb_s*, const uint8_t*, const uint_fast8_t) = gb->display.lcd_draw_line;
    void* priv = gb->direct.priv;

    if (f.read((uint8_t*)gb, sizeof(struct gb_s)) != sizeof(struct gb_s)) {
        Serial.println("[STATE] Failed reading struct gb_s");
        f.close();
        return false;
    }

    gb->gb_rom_read = rom_r;
    gb->gb_cart_ram_read = cram_r;
    gb->gb_cart_ram_write = cram_w;
    gb->gb_error = err;
    gb->gb_serial_tx = ser_tx;
    gb->gb_serial_rx = ser_rx;
    gb->gb_bootrom_read = boot_r;
    gb->display.lcd_draw_line = lcd_line_fn;
    gb->direct.priv = priv;

    if (hdr.cram_sz > 0) {
        if (cram && cram_sz >= hdr.cram_sz) {
            if (f.read(cram, hdr.cram_sz) != hdr.cram_sz) {
                Serial.println("[STATE] Failed reading cart RAM");
                f.close();
                return false;
            }
        } else {
            f.seek(f.position() + hdr.cram_sz);
        }
    }

    f.close();

    if (hdr.palette_idx < NUM_PALETTES) {
        curpal = hdr.palette_idx;
    }

    for (int i = 0; i < PG_N; i++) pg[i].v = false;
    memset(ht, -1, sizeof(ht));
    fcnt = 0;

    Serial.printf("[STATE] State loaded: %s\n", path);
    return true;
}

bool emu_has_save_state(const char* rom_path) {
    if (!rom_path || !rom_path[0]) return false;
    char path[96];
    sd_get_state_path(rom_path, path, sizeof(path));
    return SD.exists(path);
}
