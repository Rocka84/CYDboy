// Run from the repository root:
// c++ -std=c++17 -O1 -fno-strict-aliasing tests/cgb_palette_registers.cpp -o /tmp/cgb-palette-test
// /tmp/cgb-palette-test

#include <cassert>
#include <cstdio>
#include <cstring>
#define ENABLE_SOUND 0
#define ENABLE_LCD 1
#define WALNUT_FULL_GBC_SUPPORT 1
#include "../include/walnut_cgb.h"

int main() {
  static gb_s gb;
  gb.cgb.cgbMode = 1;
  // First program native GBC palettes through the real emulated I/O registers.
  __gb_write(&gb, 0xff68, 0x80);
  __gb_write(&gb, 0xff6a, 0x80);
  for (unsigned i=0;i<64;i++) {
    __gb_write(&gb, 0xff69, (i*7+3)&255);
    __gb_write(&gb, 0xff6b, (i*11+5)&255);
  }
  uint16_t saved[64];
  std::memcpy(saved, gb.cgb.fixPalette, sizeof(saved));
  // Games may still write legacy monochrome registers. They must not corrupt CGB colors.
  for (unsigned value = 0; value < 256; ++value) {
    __gb_write(&gb, 0xff47, value);
    __gb_write(&gb, 0xff48, value);
    __gb_write(&gb, 0xff49, value);
    if (std::memcmp(saved, gb.cgb.fixPalette, sizeof(saved))) {
      std::puts("FAIL: legacy DMG register write changed native CGB palette");return 1;
    }
  }
  gb.cgb.cgbMode = 0;
  __gb_write(&gb, 0xff47, 0xe4);
  __gb_write(&gb, 0xff48, 0x1b);
  __gb_write(&gb, 0xff49, 0xe4);
  for(int i=0;i<4;i++) {
    assert(gb.display.bg_palette[i]==i);
    assert(gb.display.sp_palette[i]==3-i);
    assert(gb.display.sp_palette[4+i]==i);
  }
  // Native CGB palette writes must still work after restoring native mode.
  gb.cgb.cgbMode=1;
  __gb_write(&gb, 0xff68, 0);
  __gb_write(&gb, 0xff69, 0x1f);
  __gb_write(&gb, 0xff68, 1);
  __gb_write(&gb, 0xff69, 0);
  assert(gb.cgb.fixPalette[0]==0xf800);
  __gb_write(&gb, 0xff6a, 0);
  __gb_write(&gb, 0xff6b, 0xe0);
  __gb_write(&gb, 0xff6a, 1);
  __gb_write(&gb, 0xff6b, 3);
  assert(gb.cgb.fixPalette[0x20]==0x07e0);
  std::puts("PASS: CGB palettes preserved, DMG mapping correct, CGB RGB565 writes correct");
}
