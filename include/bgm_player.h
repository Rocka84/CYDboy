#pragma once
#include <stdbool.h>

#ifndef ENABLE_SOUND
#define ENABLE_SOUND 1
#endif

#if ENABLE_SOUND
void bgm_init();
bool bgm_start();
void bgm_stop();
bool bgm_is_playing();
bool bgm_is_enabled();
void bgm_set_enabled(bool enabled);
bool bgm_file_exists();
#else
static inline void bgm_init() {}
static inline bool bgm_start() { return false; }
static inline void bgm_stop() {}
static inline bool bgm_is_playing() { return false; }
static inline bool bgm_is_enabled() { return false; }
static inline void bgm_set_enabled(bool enabled) { (void)enabled; }
static inline bool bgm_file_exists() { return false; }
#endif

