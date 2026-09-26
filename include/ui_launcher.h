#pragma once
#include "sd_manager.h"

#define LAUNCHER_SEL_BT_SCANNER   (-2)
#define LAUNCHER_SEL_USB_MANAGER  (-3)
#define LAUNCHER_SEL_WIFI_UPLOAD  (-4)
#define LAUNCHER_SEL_SETTINGS     (-5)

int launcher_show(RomEntry* roms, int count);
int launcher_ingame_menu();   // 0=resume 1=save 2=load 3=quit 4=calibrate 5=settings
void launcher_settings_menu(bool* show_fps_overlay, bool* show_save_overlay); // palette, frameskip, brightness, overlays
