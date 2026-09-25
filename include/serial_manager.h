#pragma once
#include <Arduino.h>

void serial_manager_init();
bool serial_manager_check_handshake();
void serial_manager_run();
