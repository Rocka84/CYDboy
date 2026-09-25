#include "emulator_bridge.h"
#include "display.h"
#include "hw_config.h"
#include "sd_manager.h"
#include <Arduino.h>
#include <string.h>
#include <SD.h>

#define ENABLE_LCD 1
#define ENABLE_SOUND 0
#define WALNUT_GB_12_COLOUR 1
#define WALNUT_FULL_GBC_SUPPORT 1
#define WALNUT_GB_HIGH_LCD_ACCURACY 0
#define WALNUT_GB_RGB565_BIGENDIAN 0
#define WALNUT_GB_32BIT_DMA 1
#define WALNUT_GB_16BIT_DMA 0
#include "walnut_cgb.h"

#include <ff.h>

// ─── Page cache & Extent mapping ───────────────────────────────────────────
#define PG_SZ 512
#define PG_SHIFT 9
#define PG_N  160
#define PG_LOCKED 2
#define PG_MASK (PG_SZ-1)
#define HASH_SZ 512
#define HASH_M (HASH_SZ-1)

struct RomExtent {
    uint32_t file_sec;
    uint32_t disk_sec;
    uint32_t count;
};
#define MAX_EXTENTS 64
static RomExtent extents[MAX_EXTENTS];
static int num_extents = 0;

static inline uint32_t IRAM_ATTR offset_to_disk_sector(uint32_t pb) {
    if (num_extents == 1) {
        uint32_t fsec = pb >> PG_SHIFT;
        if (fsec < extents[0].count) return extents[0].disk_sec + fsec;
        return 0;
    }
    uint32_t fsec = pb >> PG_SHIFT;
    for (int e = 0; e < num_extents; e++) {
        if (fsec >= extents[e].file_sec && fsec < extents[e].file_sec + extents[e].count) {
            return extents[e].disk_sec + (fsec - extents[e].file_sec);
        }
    }
    return 0;
}

struct Pg { uint32_t addr, acc; uint8_t* d; bool v; };
static Pg pg[PG_N];
static int16_t ht[HASH_SZ];
static uint32_t acc = 0;
static int npg = 0;
static File romf;
static uint32_t romlen = 0;
static uint32_t romf_pos = UINT32_MAX;

static uint32_t cmiss = 0;
static uint32_t emu_time_us = 0;
static uint32_t render_time_us = 0;

static inline uint8_t* IRAM_ATTR cget(uint32_t a) {
    uint32_t pb = a & ~PG_MASK;
    uint16_t h = (pb >> PG_SHIFT) & HASH_M;
    int16_t i = ht[h];
    if (i >= 0 && pg[i].v && pg[i].addr == pb) {
        pg[i].acc = ++acc;
        return &pg[i].d[a & PG_MASK];
    }
    for (int j = 0; j < npg; j++) {
        if (pg[j].v && pg[j].addr == pb) {
            pg[j].acc = ++acc;
            ht[h] = j;
            return &pg[j].d[a & PG_MASK];
        }
    }
    int lru = PG_LOCKED;
    uint32_t old = UINT32_MAX;
    for (int j = PG_LOCKED; j < npg; j++) {
        if (!pg[j].v) { lru = j; break; }
        if (pg[j].acc < old) { old = pg[j].acc; lru = j; }
    }
    if (pg[lru].v) {
        uint16_t oh = (pg[lru].addr >> PG_SHIFT) & HASH_M;
        if (ht[oh] == lru) ht[oh] = -1;
    }
    cmiss++;
    uint32_t sec = offset_to_disk_sector(pb);
    bool ok = false;
    if (sec != 0) {
        ok = SD.readRAW(pg[lru].d, sec);
    }
    if (!ok) {
        if (romf_pos != pb) {
            romf.seek(pb);
            romf_pos = pb;
        }
        size_t r = romf.read(pg[lru].d, min((uint32_t)PG_SZ, romlen - pb));
        romf_pos += r;
        if (r < PG_SZ) memset(pg[lru].d + r, 0xFF, PG_SZ - r);
    } else {
        romf_pos = UINT32_MAX;
        if (pb + PG_SZ > romlen) memset(pg[lru].d + (romlen - pb), 0xFF, PG_SZ - (romlen - pb));
    }
    pg[lru].addr = pb;
    pg[lru].acc = ++acc;
    pg[lru].v = true;
    ht[h] = lru;
    return &pg[lru].d[a & PG_MASK];
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
    (void)g;
    if (a >= romlen) return 0xFF;
    return *cget(a);
}

static uint16_t IRAM_ATTR gb_rom_read_16bit(struct gb_s* g, const uint_fast32_t a) {
    (void)g;
    if (a + 1 < romlen && (a & PG_MASK) < PG_MASK) {
        uint8_t* p = cget(a);
        return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    }
    return (uint16_t)gb_rom_read(g, a) | ((uint16_t)gb_rom_read(g, a + 1) << 8);
}

static uint32_t IRAM_ATTR gb_rom_read_32bit(struct gb_s* g, const uint_fast32_t a) {
    (void)g;
    if (a + 3 < romlen && (a & PG_MASK) <= PG_SZ - 4) {
        uint8_t* p = cget(a);
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    return (uint32_t)gb_rom_read(g, a) |
           ((uint32_t)gb_rom_read(g, a + 1) << 8) |
           ((uint32_t)gb_rom_read(g, a + 2) << 16) |
           ((uint32_t)gb_rom_read(g, a + 3) << 24);
}

static uint8_t IRAM_ATTR gb_cram_r(struct gb_s* g, const uint_fast32_t a) {
    (void)g; return (cram && cram_sz > 0) ? cram[a % cram_sz] : 0xFF;
}
static void IRAM_ATTR gb_cram_w(struct gb_s* g, const uint_fast32_t a, const uint8_t v) {
    (void)g;
    if (cram && cram_sz > 0) cram[a % cram_sz] = v;
}
static void gb_err(struct gb_s* g, const enum gb_error_e e, const uint16_t a) {
    (void)g; Serial.printf("[EMU] Err %d @0x%04X\n",(int)e,a);
}
static void IRAM_ATTR lcd_line(struct gb_s* g, const uint8_t px[160], const uint_fast8_t ln) {
    (void)g;
    if (!g->direct.frame_skip && fskip > 0 && (fcnt % (fskip + 1)) != 0) return;
    uint32_t t0 = micros();
    if (g->cgb.cgbMode) {
        for (int x = 0; x < GB_SCREEN_W; x++) {
            lbuf[x] = g->cgb.fixPalette[px[x] & 0x3F];
        }
    } else {
        const uint16_t* p = pals[curpal];
        for (int x = 0; x < GB_SCREEN_W; x++) {
            lbuf[x] = p[px[x] & 3];
        }
    }
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

    num_extents = 0;
    char fat_path[128];
    if (path[0] == '/') snprintf(fat_path, sizeof(fat_path), "0:%s", path);
    else snprintf(fat_path, sizeof(fat_path), "0:/%s", path);

    FIL fil;
    FRESULT fr = f_open(&fil, fat_path, FA_READ);
    if (fr == FR_OK) {
        FATFS* fs = fil.obj.fs;
        uint32_t csize = fs->csize;
        uint32_t database = fs->database;
        uint32_t fatbase = fs->fatbase;
        uint32_t cur_clst = fil.obj.sclust;
        uint32_t file_sec = 0;
        uint32_t sectors_needed = (romlen + 511) / 512;

        Serial.printf("[EMU] FAT: type=%d sclust=%u database=%u fatbase=%u csize=%u secs=%u\n",
                      (int)fs->fs_type, (unsigned)cur_clst, (unsigned)database, (unsigned)fatbase,
                      (unsigned)csize, (unsigned)sectors_needed);

        uint8_t sec_buf[512];
        uint32_t cached_fat_sec = UINT32_MAX;

        while (cur_clst >= 2 && cur_clst < fs->n_fatent && file_sec < sectors_needed) {
            uint32_t disk_sec = database + (cur_clst - 2) * csize;
            uint32_t nsec = min(csize, sectors_needed - file_sec);

            if (num_extents > 0 &&
                extents[num_extents - 1].disk_sec + extents[num_extents - 1].count == disk_sec) {
                extents[num_extents - 1].count += nsec;
            } else if (num_extents < MAX_EXTENTS) {
                extents[num_extents].file_sec = file_sec;
                extents[num_extents].disk_sec = disk_sec;
                extents[num_extents].count = nsec;
                num_extents++;
            }
            file_sec += nsec;

            if (fs->fs_type == FS_FAT32) {
                uint32_t fat_sec = fatbase + (cur_clst * 4) / 512;
                uint32_t fat_ofs = (cur_clst * 4) % 512;
                if (fat_sec != cached_fat_sec) {
                    if (!SD.readRAW(sec_buf, fat_sec)) break;
                    cached_fat_sec = fat_sec;
                }
                cur_clst = (*((uint32_t*)&sec_buf[fat_ofs])) & 0x0FFFFFFF;
            } else if (fs->fs_type == FS_FAT16) {
                uint32_t fat_sec = fatbase + (cur_clst * 2) / 512;
                uint32_t fat_ofs = (cur_clst * 2) % 512;
                if (fat_sec != cached_fat_sec) {
                    if (!SD.readRAW(sec_buf, fat_sec)) break;
                    cached_fat_sec = fat_sec;
                }
                cur_clst = *((uint16_t*)&sec_buf[fat_ofs]);
            } else {
                break;
            }
        }
        f_close(&fil);

        Serial.printf("[EMU] Extents mapped: %d (mapped %u / %u sectors)\n",
                      num_extents, (unsigned)file_sec, (unsigned)sectors_needed);
        for (int i = 0; i < num_extents; i++) {
            Serial.printf("  Extent %d: file_sec=%u..%u -> disk_sec=%u..%u (cnt=%u)\n",
                          i, (unsigned)extents[i].file_sec, (unsigned)(extents[i].file_sec + extents[i].count - 1),
                          (unsigned)extents[i].disk_sec, (unsigned)(extents[i].disk_sec + extents[i].count - 1),
                          (unsigned)extents[i].count);
        }

        if (file_sec < sectors_needed) {
            Serial.println("[EMU] WARNING: Incomplete cluster chain! Disabling raw sector reads.");
            num_extents = 0;
        } else {
            uint8_t tbuf[512];
            if (SD.readRAW(tbuf, extents[0].disk_sec)) {
                Serial.printf("[EMU] Verify start sector %u: logo[0]=0x%02X logo[1]=0x%02X\n",
                              (unsigned)extents[0].disk_sec, tbuf[0x104], tbuf[0x105]);
                if (tbuf[0x104] != 0xCE || tbuf[0x105] != 0xED) {
                    Serial.println("[EMU] WARNING: Logo mismatch! Disabling raw sector reads.");
                    num_extents = 0;
                } else {
                    Serial.println("[EMU] Raw sector mapping verified successfully!");
                }
            } else {
                Serial.println("[EMU] WARNING: SD.readRAW failed on start sector!");
                num_extents = 0;
            }
        }
    } else {
        Serial.printf("[EMU] f_open failed (%d) for %s\n", (int)fr, fat_path);
    }
    return true;
}
void emu_close_rom() {
    if (romf) romf.close();
    romlen = 0;
    romf_pos = UINT32_MAX;
    num_extents = 0;
    for (int i = 0; i < PG_N; i++) {
        if (pg[i].d) { free(pg[i].d); pg[i].d = nullptr; }
        pg[i].v = false;
    }
    npg = 0;
    if (cram) { free(cram); cram = nullptr; cram_sz = 0; }
    if (gb) { free(gb); gb = nullptr; }
    Serial.printf("[EMU] Closed. Free heap: %u\n", ESP.getFreeHeap());
}

static char emu_err_str[64] = "";
const char* emu_get_error() { return emu_err_str; }

bool emu_init(uint8_t*, uint32_t) {
    emu_err_str[0] = 0;
    if (!romf || !romlen) {
        Serial.println("[EMU] Init failed: no ROM open");
        snprintf(emu_err_str, sizeof(emu_err_str), "No ROM open");
        return false;
    }

    Serial.printf("[EMU] Init: free heap=%u, max alloc=%u, gb_s=%u\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap(), (unsigned)sizeof(struct gb_s));

    if (!gb) gb = (struct gb_s*)malloc(sizeof(struct gb_s));
    if (!gb) {
        Serial.printf("[EMU] Failed to alloc gb (%u bytes, max alloc: %u)\n",
                      (unsigned)sizeof(struct gb_s), ESP.getMaxAllocHeap());
        snprintf(emu_err_str, sizeof(emu_err_str), "Alloc GB failed (%u)", (unsigned)sizeof(struct gb_s));
        return false;
    }
    memset(gb, 0, sizeof(struct gb_s));

    romf.seek(0x0147);
    uint8_t mbc_type = romf.read();
    romf.seek(0x0149);
    uint8_t rc = romf.read();

    uint32_t req_ram = 0;
    if (mbc_type == 0x05 || mbc_type == 0x06) {
        req_ram = 512;
    } else {
        if (rc == 1) req_ram = 2048;
        else if (rc >= 2) req_ram = 8192; // 8KB is plenty for saves and frees 24KB for ROM page cache
    }

    if (req_ram > 0) {
        if (!cram || cram_sz != req_ram) {
            if (cram) free(cram);
            cram = (uint8_t*)malloc(req_ram);
        }
        if (!cram) {
            Serial.printf("[EMU] Failed to alloc cram (%u bytes, max alloc: %u)\n", req_ram, ESP.getMaxAllocHeap());
            snprintf(emu_err_str, sizeof(emu_err_str), "Alloc cram failed (%u)", req_ram);
            return false;
        }
        cram_sz = req_ram;
        memset(cram, 0xFF, cram_sz);
    } else {
        if (cram) { free(cram); cram = nullptr; }
        cram_sz = 0;
    }

    memset(ht, -1, sizeof(ht));

    for (int i = 0; i < PG_N; i++) {
        if (pg[i].d) { free(pg[i].d); pg[i].d = nullptr; }
        pg[i].v = false;
    }
    npg = 0;

    for (int i = 0; i < PG_N; i++) {
        if (ESP.getMaxAllocHeap() < PG_SZ || ESP.getFreeHeap() < 7168) break;
        pg[i].d = (uint8_t*)malloc(PG_SZ);
        if (!pg[i].d) break;
        pg[i].v = false;
        pg[i].acc = 0;
        npg++;
    }

    if (npg < 4) {
        Serial.printf("[EMU] Failed to alloc minimum pages (npg=%d, free_heap=%u)\n", npg, ESP.getFreeHeap());
        snprintf(emu_err_str, sizeof(emu_err_str), "Alloc pages failed (%d)", npg);
        return false;
    }

    romf_pos = UINT32_MAX;
    for (int i = 0; i < PG_LOCKED && i < npg; i++) {
        uint32_t pb = i * PG_SZ;
        if (pb < romlen) {
            uint32_t sec = offset_to_disk_sector(pb);
            bool ok = false;
            if (sec != 0) {
                ok = SD.readRAW(pg[i].d, sec);
            }
            if (!ok) {
                romf.seek(pb);
                size_t r = romf.read(pg[i].d, min((uint32_t)PG_SZ, romlen - pb));
                if (r < PG_SZ) memset(pg[i].d + r, 0xFF, PG_SZ - r);
            } else {
                if (pb + PG_SZ > romlen) memset(pg[i].d + (romlen - pb), 0xFF, PG_SZ - (romlen - pb));
            }
            pg[i].addr = pb;
            pg[i].acc = 0xFFFFFFFF;
            pg[i].v = true;
            uint16_t h = (pb >> PG_SHIFT) & HASH_M;
            ht[h] = i;
        }
    }
    romf_pos = UINT32_MAX;

    enum gb_init_error_e r = gb_init(gb, gb_rom_read, gb_rom_read_16bit, gb_rom_read_32bit, gb_cram_r, gb_cram_w, gb_err, nullptr);
    if (r != GB_INIT_NO_ERROR) {
        Serial.printf("[EMU] gb_init failed: %d\n", (int)r);
        snprintf(emu_err_str, sizeof(emu_err_str), "gb_init err %d", (int)r);
        return false;
    }
    gb_init_lcd(gb, lcd_line);
    if (fskip > 0) gb->direct.frame_skip = true;
    Serial.printf("[EMU] Core: Walnut-CGB | Mode: %s\n", gb->cgb.cgbMode ? "Game Boy Color" : "DMG Classic");
    Serial.printf("[EMU] %d pages allocated (%d KB pool). RAM: %u. Free heap: %u\n",
                  npg, (npg * PG_SZ) / 1024, cram_sz, ESP.getFreeHeap());

    fcnt = fpsc = cfps = 0;
    fpst = millis();
    acc = 0;
    char t[17] = {0};
    for (int i = 0; i < 16; i++) {
        char c = (char)gb_rom_read(gb, 0x134 + i);
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
        Serial.printf("[PERF] FPS: %u | emu: %u ms | render: %u ms | misses/s: %u | heap: %u | npg: %d | ext: %d\n",
                      cfps, e_avg / 1000, r_avg / 1000, cmiss, ESP.getFreeHeap(), npg, num_extents);
        fpsc=0; fpst=n; cmiss=0; emu_time_us=0; render_time_us=0;
    }
}

void emu_set_joypad(uint8_t b){jpad=b;}
uint8_t* emu_get_cart_ram(uint32_t* s){if(s)*s=cram_sz;return cram;}
void emu_set_cart_ram(const uint8_t* d,uint32_t s){if(cram&&d&&s>0){if(s>cram_sz)s=cram_sz;memcpy(cram,d,s);}}
bool emu_cart_ram_dirty(){return false;}
uint32_t emu_get_cart_ram_last_write_ms(){return 0;}
void emu_clear_cart_ram_dirty(){}
void emu_set_frame_skip(uint8_t s) {
    fskip = s;
    if (gb) gb->direct.frame_skip = (fskip > 0);
}
uint8_t emu_get_frame_skip() { return fskip; }
uint32_t emu_get_fps() { return cfps; }
uint16_t* emu_get_line_buffer() { return lbuf; }
void emu_reset() {
    gb_reset(gb);
    if (gb && fskip > 0) gb->direct.frame_skip = true;
    fcnt = 0;
}

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
    char path[MAX_PATHLEN];
    sd_get_state_path(rom_path, path, sizeof(path));
    if (SD.exists(path)) SD.remove(path);
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        Serial.printf("[STATE] Save open failed: %s\n", path);
        return false;
    }

    SaveStateHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, "CYDGBC1", 8);
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
    char path[MAX_PATHLEN];
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

    if (memcmp(hdr.magic, "CYDGBC1", 8) != 0 || hdr.version != 1) {
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
    uint16_t (*rom_r16)(struct gb_s*, const uint_fast32_t) = gb->gb_rom_read_16bit;
    uint32_t (*rom_r32)(struct gb_s*, const uint_fast32_t) = gb->gb_rom_read_32bit;
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
    gb->gb_rom_read_16bit = rom_r16;
    gb->gb_rom_read_32bit = rom_r32;
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
    char path[MAX_PATHLEN];
    sd_get_state_path(rom_path, path, sizeof(path));
    return SD.exists(path);
}
