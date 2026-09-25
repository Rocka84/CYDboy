#pragma once
#include <stdint.h>
#include <stdbool.h>

void bt_controller_init();
void bt_controller_update();
uint16_t bt_controller_get_buttons();
bool bt_controller_is_connected();
const char* bt_controller_get_name();
void bt_controller_ui_show();
