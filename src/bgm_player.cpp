#include "bgm_player.h"

#if ENABLE_SOUND
#include "audio_output.h"
#include <Arduino.h>
#include <SD.h>
#include <Preferences.h>

static TaskHandle_t s_bgm_task = NULL;
static volatile bool s_bgm_running = false;
static bool s_bgm_enabled = true;
static File s_bgm_file;

static uint32_t s_data_offset = 0;
static uint32_t s_data_size = 0;
static uint16_t s_channels = 1;
static uint32_t s_sample_rate = 22050;
static uint16_t s_bits_per_sample = 8;
static uint16_t s_block_align = 1;
static bool s_downsample = false;

static bool parse_wav_header(File &f) {
    if (f.size() < 44) return false;

    char riff[4];
    if (f.read((uint8_t*)riff, 4) != 4 || memcmp(riff, "RIFF", 4) != 0) return false;

    uint32_t riff_sz = 0;
    f.read((uint8_t*)&riff_sz, 4);

    char wave[4];
    if (f.read((uint8_t*)wave, 4) != 4 || memcmp(wave, "WAVE", 4) != 0) return false;

    bool found_fmt = false;
    bool found_data = false;

    while (f.available() >= 8) {
        char chunk_id[4];
        uint32_t chunk_sz = 0;
        if (f.read((uint8_t*)chunk_id, 4) != 4) break;
        if (f.read((uint8_t*)&chunk_sz, 4) != 4) break;

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            uint16_t format = 0;
            f.read((uint8_t*)&format, 2);
            f.read((uint8_t*)&s_channels, 2);
            f.read((uint8_t*)&s_sample_rate, 4);

            uint32_t byte_rate = 0;
            f.read((uint8_t*)&byte_rate, 4);
            f.read((uint8_t*)&s_block_align, 2);
            f.read((uint8_t*)&s_bits_per_sample, 2);

            if (format != 1) { // 1 = PCM
                Serial.printf("[BGM] Unsupported WAV format (%u), PCM required\n", format);
                return false;
            }

            if (chunk_sz > 16) {
                f.seek(f.position() + (chunk_sz - 16));
            }
            found_fmt = true;
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            s_data_offset = f.position();
            s_data_size = chunk_sz;
            found_data = true;
            break;
        } else {
            f.seek(f.position() + chunk_sz);
        }
    }

    if (!found_fmt || !found_data) {
        Serial.println("[BGM] Invalid WAV: missing fmt or data chunk");
        return false;
    }

    s_downsample = (s_sample_rate >= 32000);
    Serial.printf("[BGM] WAV: %u Hz, %u ch, %u-bit, data_sz=%u (downsample=%d)\n",
                  s_sample_rate, s_channels, s_bits_per_sample, s_data_size, (int)s_downsample);
    return true;
}

static void bgm_worker_task(void *param) {
    const size_t CHUNK_SIZE = 512;
    uint8_t raw_buf[CHUNK_SIZE];

    s_bgm_file.seek(s_data_offset);
    uint32_t bytes_played = 0;

    while (s_bgm_running) {
        if (audio_ring_buf_free() < CHUNK_SIZE) {
            audio_start_playback();
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (bytes_played >= s_data_size || !s_bgm_file.available()) {
            s_bgm_file.seek(s_data_offset);
            bytes_played = 0;
        }

        size_t to_read = CHUNK_SIZE;
        if (bytes_played + to_read > s_data_size) {
            to_read = s_data_size - bytes_played;
        }

        int bytes_read = s_bgm_file.read(raw_buf, to_read);
        if (bytes_read <= 0) {
            s_bgm_file.seek(s_data_offset);
            bytes_played = 0;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        bytes_played += bytes_read;

        uint8_t vol = audio_get_volume();
        size_t step = s_block_align * (s_downsample ? 2 : 1);

        for (size_t i = 0; i + s_block_align <= (size_t)bytes_read; i += step) {
            int32_t sample = 0;

            if (s_bits_per_sample == 8) {
                if (s_channels == 1) {
                    sample = (int32_t)raw_buf[i] - 128;
                } else {
                    int32_t left = (int32_t)raw_buf[i] - 128;
                    int32_t right = (int32_t)raw_buf[i + 1] - 128;
                    sample = (left + right) / 2;
                }
            } else if (s_bits_per_sample == 16) {
                if (s_channels == 1) {
                    int16_t s = (int16_t)(raw_buf[i] | (raw_buf[i + 1] << 8));
                    sample = s >> 8;
                } else {
                    int16_t left = (int16_t)(raw_buf[i] | (raw_buf[i + 1] << 8));
                    int16_t right = (int16_t)(raw_buf[i + 2] | (raw_buf[i + 3] << 8));
                    sample = ((int32_t)left + right) / (2 * 256);
                }
            }

            int32_t val = 0;
            uint8_t eff_vol = (vol == AUDIO_VOL_MUTE) ? AUDIO_VOL_MED : vol;
            if (eff_vol == AUDIO_VOL_LOW) {
                val = (sample * 25) / 128;
            } else if (eff_vol == AUDIO_VOL_MED) {
                val = (sample * 50) / 128;
            } else {
                val = (sample * 90) / 128;
            }

            if (val < -100) val = -100;
            if (val > 100) val = 100;

            uint16_t pwm_val = (uint16_t)(512 + val);
            audio_ring_buf_push(pwm_val);
        }

        audio_start_playback();
    }

    s_bgm_file.close();
    s_bgm_task = NULL;
    vTaskDelete(NULL);
}

void bgm_init() {
    s_bgm_running = false;
    s_bgm_task = NULL;
    Preferences prefs;
    if (prefs.begin("cyd_gb", false)) {
        s_bgm_enabled = prefs.getBool("bgm_en", true);
        prefs.end();
    }
}

bool bgm_is_enabled() {
    return s_bgm_enabled;
}

void bgm_set_enabled(bool enabled) {
    if (s_bgm_enabled == enabled) return;
    s_bgm_enabled = enabled;
    Preferences prefs;
    if (prefs.begin("cyd_gb", false)) {
        prefs.putBool("bgm_en", s_bgm_enabled);
        prefs.end();
    }
    if (!s_bgm_enabled && s_bgm_running) {
        bgm_stop();
    } else if (s_bgm_enabled && !s_bgm_running) {
        bgm_start();
    }
}

bool bgm_start() {
    if (!s_bgm_enabled) return false;
    if (s_bgm_running) return true;

    if (!SD.exists("/bgm.wav")) {
        return false;
    }

    s_bgm_file = SD.open("/bgm.wav", FILE_READ);
    if (!s_bgm_file) {
        Serial.println("[BGM] Failed to open /bgm.wav");
        return false;
    }

    if (!parse_wav_header(s_bgm_file)) {
        s_bgm_file.close();
        return false;
    }

    audio_reset();
    audio_enable_output(true);

    s_bgm_running = true;
    xTaskCreatePinnedToCore(bgm_worker_task, "bgm_worker", 4096, NULL, 1, &s_bgm_task, 1);
    Serial.println("[BGM] Background music playback started");
    return true;
}

void bgm_stop() {
    if (!s_bgm_running) return;

    s_bgm_running = false;
    uint32_t t0 = millis();
    while (s_bgm_task != NULL && (millis() - t0 < 400)) {
        delay(10);
    }

    audio_enable_output(false);
    Serial.println("[BGM] Background music stopped");
}

bool bgm_is_playing() {
    return s_bgm_running;
}

bool bgm_file_exists() {
    return SD.exists("/bgm.wav");
}
#endif // ENABLE_SOUND

