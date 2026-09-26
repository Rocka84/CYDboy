#pragma once
#include <TFT_eSPI.h>
extern TFT_eSPI tft;

void display_init();
void display_set_backlight(uint8_t level);
void display_clear(uint16_t color = TFT_BLACK);
void display_push_gb_line(uint8_t line_y, const uint8_t* px, const uint16_t* pal, uint8_t mask);
void display_draw_controls();
void display_clear_controls();
