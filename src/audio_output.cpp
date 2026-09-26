#include "audio_output.h"

#if ENABLE_SOUND
#include <Arduino.h>
#include <Preferences.h>
#include <driver/ledc.h>
#include <soc/ledc_struct.h>

struct minigb_apu_ctx apu_ctx;

static uint8_t s_volume = AUDIO_VOL_MED;
static audio_sample_t s_raw_samples[AUDIO_SAMPLES_TOTAL];
static int32_t s_mono_samples[AUDIO_SAMPLES];

#define AUDIO_LEDC_CHAN 2
#define RING_BUF_SIZE   2048
#define TARGET_BUF      1024
static uint16_t s_ring_buf[RING_BUF_SIZE];
static volatile uint16_t s_rb_head = 0;
static volatile uint16_t s_rb_tail = 0;

static hw_timer_t *s_audio_timer = NULL;
static bool s_output_enabled = false;
static bool s_timer_started = false;

static uint32_t s_write_count = 0;
static uint32_t s_trig_count = 0;
static uint32_t s_read_count = 0;
static uint32_t s_underrun_count = 0;

static inline void set_ledc_duty(uint32_t duty) {
    LEDC.channel_group[0].channel[AUDIO_LEDC_CHAN].duty.duty = duty << 4;
    LEDC.channel_group[0].channel[AUDIO_LEDC_CHAN].conf0.sig_out_en = 1;
    LEDC.channel_group[0].channel[AUDIO_LEDC_CHAN].conf1.duty_start = 1;
}

void IRAM_ATTR audio_timer_isr() {
    if (s_rb_tail != s_rb_head) {
        uint16_t s = s_ring_buf[s_rb_tail];
        s_rb_tail = (s_rb_tail + 1) & (RING_BUF_SIZE - 1);
        set_ledc_duty(s);
    } else {
        set_ledc_duty(512);
        s_underrun_count++;
    }
}

extern "C" {
uint8_t IRAM_ATTR audio_read(const uint16_t addr) {
    return minigb_apu_audio_read(&apu_ctx, addr);
}

void IRAM_ATTR audio_write(const uint16_t addr, const uint8_t val) {
    minigb_apu_audio_write(&apu_ctx, addr, val);
}
}

bool audio_is_enabled() {
    return s_output_enabled && (s_volume != AUDIO_VOL_MUTE);
}

void audio_set_fps(uint32_t fps) {
}

void audio_enable_output(bool enable) {
    if (enable) {
        if (!s_output_enabled) {
            s_rb_head = 0;
            s_rb_tail = 0;
            s_underrun_count = 0;
            s_timer_started = false;

            pinMode(26, OUTPUT);
            digitalWrite(26, LOW);
            ledcSetup(AUDIO_LEDC_CHAN, 78125, 10);
            ledcAttachPin(26, AUDIO_LEDC_CHAN);
            for (int d = 0; d <= 512; d += 32) {
                set_ledc_duty(d);
                delayMicroseconds(100);
            }
            set_ledc_duty(512);

            s_audio_timer = timerBegin(1, 80, true);
            timerAttachInterrupt(s_audio_timer, &audio_timer_isr, true);
            timerAlarmWrite(s_audio_timer, 45, true);

            s_output_enabled = true;
            Serial.println("[AUDIO] 10-bit PWM (78.1 kHz carrier) active on Channel 2, GPIO 26");
        }
    } else {
        if (s_output_enabled) {
            if (s_audio_timer) {
                timerAlarmDisable(s_audio_timer);
                timerDetachInterrupt(s_audio_timer);
                timerEnd(s_audio_timer);
                s_audio_timer = NULL;
            }
            s_output_enabled = false;
            s_timer_started = false;
            for (int d = 512; d >= 0; d -= 32) {
                set_ledc_duty(d);
                delayMicroseconds(100);
            }
            ledcDetachPin(26);
        }
        pinMode(26, INPUT);
        Serial.println("[AUDIO] Output muted (GPIO 26 in High-Z INPUT mode)");
    }
}

void audio_init() {
    Preferences prefs;
    if (prefs.begin("cyd_gb", false)) {
        s_volume = prefs.getUChar("vol", AUDIO_VOL_MED);
        if (s_volume > AUDIO_VOL_HIGH) s_volume = AUDIO_VOL_MED;
        prefs.end();
    }

    pinMode(26, INPUT);

    minigb_apu_audio_init(&apu_ctx);
    Serial.printf("[AUDIO] Init OK (GPIO 26 in High-Z INPUT mode), vol=%d\n", s_volume);
}

void audio_reset() {
    minigb_apu_audio_init(&apu_ctx);
    s_rb_head = 0;
    s_rb_tail = 0;
    s_write_count = 0;
    s_trig_count = 0;
    s_read_count = 0;
    s_underrun_count = 0;
    s_timer_started = false;
}

void audio_process_frame() {
    if (!s_output_enabled || s_volume == AUDIO_VOL_MUTE) return;

    // Synthesize Game Boy chiptune audio (369 stereo samples)
    minigb_apu_audio_callback(&apu_ctx, s_raw_samples);

    int16_t s_min = 0, s_max = 0;
    for (size_t j = 0; j < AUDIO_SAMPLES; j++) {
        int32_t mono = (s_raw_samples[j * 2] + s_raw_samples[j * 2 + 1]) / 2;
        s_mono_samples[j] = mono;
        if (mono < s_min) s_min = mono;
        if (mono > s_max) s_max = mono;
    }

    // Dynamic buffer feedback: 372 samples/frame exactly matches 22,222 Hz timer at 59.73 FPS.
    // Fixed pitch locking: N is clamped strictly to 372 +/- 1% (368..376) so pitch never wavers or drops.
    uint16_t buffered = (s_rb_head - s_rb_tail) & (RING_BUF_SIZE - 1);
    int32_t diff = (int32_t)TARGET_BUF - (int32_t)buffered;
    int32_t N = 372 + (diff / 64);
    if (N < 368) N = 368;
    if (N > 376) N = 376;

    // Linear interpolation resampler: perfectly smooth transitions, zero gaps, zero underruns
    for (int32_t i = 0; i < N; i++) {
        uint32_t pos = ((uint32_t)i * (AUDIO_SAMPLES - 1) * 256) / (N - 1);
        uint16_t idx = pos >> 8;
        uint8_t frac = pos & 0xFF;
        int32_t s0 = s_mono_samples[idx];
        int32_t s1 = (idx + 1 < AUDIO_SAMPLES) ? s_mono_samples[idx + 1] : s0;
        int32_t mono = s0 + (((s1 - s0) * frac) >> 8);

        int32_t val = 0;
        if (s_volume == AUDIO_VOL_MUTE) {
            val = 0;
        } else if (s_volume == AUDIO_VOL_LOW) {
            val = (mono * 25) / 32768;
        } else if (s_volume == AUDIO_VOL_MED) {
            val = (mono * 50) / 32768;
        } else if (s_volume == AUDIO_VOL_HIGH) {
            val = (mono * 90) / 32768;
        }

        if (val < -100) val = -100;
        if (val > 100) val = 100;

        uint16_t pwm_val = (uint16_t)(512 + val);

        uint16_t next_head = (s_rb_head + 1) & (RING_BUF_SIZE - 1);
        if (next_head != s_rb_tail) {
            s_ring_buf[s_rb_head] = pwm_val;
            s_rb_head = next_head;
        }
    }

    // Start timer once pre-buffer reaches comfortable safety margin
    if (!s_timer_started) {
        uint16_t b = (s_rb_head - s_rb_tail) & (RING_BUF_SIZE - 1);
        if (b >= TARGET_BUF) {
            timerAlarmEnable(s_audio_timer);
            s_timer_started = true;
            Serial.printf("[AUDIO] Pre-buffer filled (%u samples). Timer started.\n", b);
        }
    } else {
        // Frame pacing: lock emulator rate to hardware audio timer (59.73 FPS)
        while (((s_rb_head - s_rb_tail) & (RING_BUF_SIZE - 1)) > (TARGET_BUF + 40)) {
            delayMicroseconds(200);
        }
    }

    static int s_dbg = 0;
    if (++s_dbg >= 60) {
        s_dbg = 0;
        uint16_t b = (s_rb_head - s_rb_tail) & (RING_BUF_SIZE - 1);
        Serial.printf("[AUDIO] buf=%u/%u | N=%d | underruns=%u | min=%d max=%d | vol=%d\n",
                      b, RING_BUF_SIZE, (int)N, s_underrun_count,
                      (int)s_min, (int)s_max, s_volume);
    }
}

size_t audio_ring_buf_free() {
    uint16_t b = (s_rb_head - s_rb_tail) & (RING_BUF_SIZE - 1);
    return (RING_BUF_SIZE - 1) - b;
}

bool audio_ring_buf_push(uint16_t sample) {
    uint16_t next_head = (s_rb_head + 1) & (RING_BUF_SIZE - 1);
    if (next_head == s_rb_tail) return false;
    s_ring_buf[s_rb_head] = sample;
    s_rb_head = next_head;
    return true;
}

void audio_start_playback() {
    if (!s_timer_started && s_audio_timer) {
        uint16_t b = (s_rb_head - s_rb_tail) & (RING_BUF_SIZE - 1);
        if (b >= TARGET_BUF) {
            timerAlarmEnable(s_audio_timer);
            s_timer_started = true;
            Serial.printf("[AUDIO] Pre-buffer filled (%u samples). Playback started.\n", b);
        }
    }
}

void audio_set_volume(uint8_t vol) {
    if (vol > AUDIO_VOL_HIGH) vol = AUDIO_VOL_HIGH;
    uint8_t prev = s_volume;
    s_volume = vol;

    Preferences prefs;
    if (prefs.begin("cyd_gb", false)) {
        prefs.putUChar("vol", s_volume);
        prefs.end();
    }

    if (s_volume == AUDIO_VOL_MUTE) {
        audio_enable_output(false);
    } else if (prev == AUDIO_VOL_MUTE && s_volume > AUDIO_VOL_MUTE) {
        audio_enable_output(true);
    }
}

uint8_t audio_get_volume() {
    return s_volume;
}

const char* audio_get_volume_str() {
    switch (s_volume) {
        case AUDIO_VOL_MUTE: return "OFF (Max Speed)";
        case AUDIO_VOL_LOW:  return "LOW (33%)";
        case AUDIO_VOL_MED:  return "MED (66%)";
        case AUDIO_VOL_HIGH: return "HIGH (100%)";
        default:             return "MED (66%)";
    }
}

void audio_cycle_volume() {
    uint8_t next = (s_volume + 1) % 4;
    audio_set_volume(next);
}
#endif // ENABLE_SOUND
