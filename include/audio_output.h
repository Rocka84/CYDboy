#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef ENABLE_SOUND
#define ENABLE_SOUND 1
#endif

enum AudioVolume {
    AUDIO_VOL_MUTE = 0,
    AUDIO_VOL_LOW  = 1, // 33%
    AUDIO_VOL_MED  = 2, // 66%
    AUDIO_VOL_HIGH = 3  // 100%
};

#if ENABLE_SOUND
#include "minigb_apu.h"

void audio_init();
void audio_process_frame();
void audio_reset();
void audio_enable_output(bool enable);
void audio_set_fps(uint32_t fps);

void audio_set_volume(uint8_t vol);
uint8_t audio_get_volume();
const char* audio_get_volume_str();
void audio_cycle_volume();
bool audio_is_enabled();

size_t audio_ring_buf_free();
bool audio_ring_buf_push(uint16_t sample);
void audio_start_playback();

extern struct minigb_apu_ctx apu_ctx;

#ifdef __cplusplus
extern "C" {
#endif
uint8_t audio_read(const uint16_t addr);
void audio_write(const uint16_t addr, const uint8_t val);
#ifdef __cplusplus
}
#endif

#else // !ENABLE_SOUND

static inline void audio_init() {}
static inline void audio_process_frame() {}
static inline void audio_reset() {}
static inline void audio_enable_output(bool enable) { (void)enable; }
static inline void audio_set_fps(uint32_t fps) { (void)fps; }

static inline void audio_set_volume(uint8_t vol) { (void)vol; }
static inline uint8_t audio_get_volume() { return 0; }
static inline const char* audio_get_volume_str() { return "DISABLED"; }
static inline void audio_cycle_volume() {}
static inline bool audio_is_enabled() { return false; }

static inline size_t audio_ring_buf_free() { return 0; }
static inline bool audio_ring_buf_push(uint16_t sample) { (void)sample; return false; }
static inline void audio_start_playback() {}

#ifdef __cplusplus
extern "C" {
#endif
static inline uint8_t audio_read(const uint16_t addr) { (void)addr; return 0xFF; }
static inline void audio_write(const uint16_t addr, const uint8_t val) { (void)addr; (void)val; }
#ifdef __cplusplus
}
#endif

#endif // ENABLE_SOUND

