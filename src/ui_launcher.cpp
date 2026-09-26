#include "ui_launcher.h"
#include "display.h"
#include "touch_input.h"
#include "button_input.h"
#include "emulator_bridge.h"
#include "serial_manager.h"
#include "bt_controller.h"
#include "hw_config.h"
#include "audio_output.h"
#include "bgm_player.h"
#include <Arduino.h>

#define ITEMS_PP 5
#define ITEM_H   34
#define ITEM_Y0  44
#define ITEM_X   8

// ─── Helpers ────────────────────────────────────────────────────────────────
static void wait_release() { 
	// wait until no button is pressed
	button_update();
	while(button_get_buttons()) { button_update(); delay(10); }
	delay(100);
}

static void draw_bt_header_status(bool connected) {
	tft.fillRect(SCREEN_W-38,0,38,36,0x18C3);
	if (connected) {
		tft.fillRoundRect(SCREEN_W-35,9,26,18,3,0x07E0);
		tft.setTextColor(TFT_BLACK,0x07E0);
		tft.setTextDatum(MC_DATUM);
		tft.drawString("BT",SCREEN_W-22,18,1);
	} else {
		tft.fillRoundRect(SCREEN_W-35,9,26,18,3,0x2104);
		tft.drawRoundRect(SCREEN_W-35,9,26,18,3,0x528A);
		tft.setTextColor(0x7BEF,0x2104);
		tft.setTextDatum(MC_DATUM);
		tft.drawString("BT",SCREEN_W-22,18,1);
	}
}

static void draw_header(const char* t) {
	tft.fillRect(0,0,SCREEN_W,36,0x18C3);
	tft.setTextColor(TFT_WHITE,0x18C3); tft.setTextDatum(ML_DATUM);
	tft.drawString(t,10,18,2);
	draw_bt_header_status(bt_controller_is_connected());
}

// ─── ROM List ───────────────────────────────────────────────────────────────
static void draw_list(RomEntry* r, int cnt, int pg, int sel) {
	int total = cnt + 3; // extra entries: BT scanner, USB ROM Manager, Settings
	int s = pg*ITEMS_PP, e = min(s+ITEMS_PP, total);
	tft.fillRect(0,38,SCREEN_W,202,TFT_BLACK);

	for (int i=s; i<e; i++) {
		int y = ITEM_Y0 + (i-s)*ITEM_H;
		uint16_t bg = (i==sel) ? 0x0014 : 0x0000;
		uint16_t fg = (i==sel) ? 0xFFE0 : TFT_WHITE;
		tft.fillRoundRect(ITEM_X,y,SCREEN_W-ITEM_X-8,ITEM_H-4,4,bg);

		if (i < cnt) {
			// Badge
			uint16_t bc = r[i].is_gbc ? 0x07E0 : 0x7BEF;
			const char* bt = r[i].is_gbc ? "GBC" : "GB";
			tft.fillRoundRect(ITEM_X+3,y+5,26,18,3,bc);
			tft.setTextColor(TFT_BLACK,bc); tft.setTextDatum(MC_DATUM);
			tft.drawString(bt,ITEM_X+16,y+14,1);

			// Name (truncated, readable)
			char nm[30]; strncpy(nm,r[i].filename,28); nm[28]=0;
			char* dot=strrchr(nm,'.'); if(dot)*dot=0;
			tft.setTextColor(fg,bg); tft.setTextDatum(ML_DATUM);
			tft.drawString(nm,ITEM_X+34,y+ITEM_H/2-2,2);

			// Size
			char sz[12]; snprintf(sz,12,"%uK",r[i].size/1024);
			tft.setTextColor(0x7BEF,bg); tft.setTextDatum(MR_DATUM);
			tft.drawString(sz,SCREEN_W-12,y+ITEM_H/2-2,1);
		} else if (i == cnt) {
			// Virtual app entry: Bluetooth Gamepad
			tft.fillRoundRect(ITEM_X+3,y+5,26,18,3,0x07FF);
			tft.setTextColor(TFT_BLACK,0x07FF); tft.setTextDatum(MC_DATUM);
			tft.drawString("BT",ITEM_X+16,y+14,1);

			tft.setTextColor(fg,bg); tft.setTextDatum(ML_DATUM);
			tft.drawString("Bluetooth Gamepad",ITEM_X+34,y+ITEM_H/2-2,2);

			bool bt_conn = bt_controller_is_connected();
			tft.setTextColor(bt_conn ? 0x07E0 : 0x7BEF, bg); tft.setTextDatum(MR_DATUM);
			tft.drawString(bt_conn ? "CONNECTED" : "PAIRING", SCREEN_W-12, y+ITEM_H/2-2, 1);
		} else if (i == cnt + 1) {
			// Virtual app entry: USB ROM Manager
			tft.fillRoundRect(ITEM_X+3,y+5,26,18,3,0xFDE0);
			tft.setTextColor(TFT_BLACK,0xFDE0); tft.setTextDatum(MC_DATUM);
			tft.drawString("USB",ITEM_X+16,y+14,1);

			tft.setTextColor(fg,bg); tft.setTextDatum(ML_DATUM);
			tft.drawString("USB ROM Manager",ITEM_X+34,y+ITEM_H/2-2,2);

			tft.setTextColor(0x7BEF,bg); tft.setTextDatum(MR_DATUM);
			tft.drawString("SYNC",SCREEN_W-12,y+ITEM_H/2-2,1);
		} else {
			// Virtual app entry: System Settings
			tft.fillRoundRect(ITEM_X+3,y+5,26,18,3,0xFD20);
			tft.setTextColor(TFT_BLACK,0xFD20); tft.setTextDatum(MC_DATUM);
			tft.drawString("SET",ITEM_X+16,y+14,1);

			tft.setTextColor(fg,bg); tft.setTextDatum(ML_DATUM);
			tft.drawString("System Settings",ITEM_X+34,y+ITEM_H/2-2,2);

			tft.setTextColor(0x7BEF,bg); tft.setTextDatum(MR_DATUM);
			tft.drawString("CONFIG",SCREEN_W-12,y+ITEM_H/2-2,1);
		}
	}

	// Nav bar
	tft.fillRect(0,SCREEN_H-20,SCREEN_W,20,0x18C3);
	tft.setTextColor(0x07E0,0x18C3); tft.setTextDatum(ML_DATUM);
	tft.drawString("[SET]",5,SCREEN_H-10,1);
	int tp = (total+ITEMS_PP-1)/ITEMS_PP;
	if (tp>1) {
		tft.setTextColor(TFT_WHITE,0x18C3); tft.setTextDatum(MC_DATUM);
		char ps[16]; snprintf(ps,16,"< %d/%d >",pg+1,tp);
		tft.drawString(ps,SCREEN_W/2,SCREEN_H-10,1);
	}
	tft.setTextColor(0xFFE0,0x18C3); tft.setTextDatum(MR_DATUM);
	tft.drawString("[CAL]",SCREEN_W-5,SCREEN_H-10,1);
}

int launcher_show(RomEntry* roms, int cnt) {
	int pg=0, sel=0;
	tft.fillScreen(TFT_BLACK);
	draw_header("Game Boy ROMs");

	draw_list(roms,cnt,pg,sel);
	uint16_t prev = 0;
	uint32_t dbg_t = 0;
	int total = cnt + 3;
	int tp = (total+ITEMS_PP-1)/ITEMS_PP;
	bool last_conn = bt_controller_is_connected();
	while (true) {
		if (serial_manager_check_handshake()) {
			return LAUNCHER_SEL_USB_MANAGER;
		}

		button_update();
		uint16_t b = button_get_buttons();

		bool curr_conn = bt_controller_is_connected();
		if (curr_conn != last_conn) {
			last_conn = curr_conn;
			draw_bt_header_status(curr_conn);
			int s = pg * ITEMS_PP, e = min(s + ITEMS_PP, total);
			if (cnt >= s && cnt < e) {
				draw_list(roms, cnt, pg, sel);
			}
		}

		if (touch_is_pressed()) {
			int16_t tx = touch_get_x(), ty = touch_get_y();
			if (ty >= ITEM_Y0 && ty < ITEM_Y0 + ITEMS_PP * ITEM_H) {
				int idx = pg * ITEMS_PP + (ty - ITEM_Y0) / ITEM_H;
				if (idx < total) {
					if (sel != idx) {
						sel = idx;
						draw_list(roms, cnt, pg, sel);
						delay(180);
					} else {
						if (sel == cnt) return LAUNCHER_SEL_BT_SCANNER;
						if (sel == cnt + 1) return LAUNCHER_SEL_USB_MANAGER;
						if (sel == cnt + 2) return LAUNCHER_SEL_SETTINGS;
						return sel;
					}
				}
			}
			if (ty >= SCREEN_H - 24) {
				if (tx < 50) {
					return LAUNCHER_SEL_SETTINGS;
				} else if (tx > SCREEN_W - 50) {
					touch_run_calibration();
					draw_header("Game Boy ROMs");
					draw_list(roms, cnt, pg, sel);
					delay(250);
				} else if (tx >= 50 && tx < 100 && pg > 0) {
					pg--; sel = pg * ITEMS_PP; draw_list(roms, cnt, pg, sel); delay(250);
				} else if (tx >= 140 && tx <= SCREEN_W - 50 && pg < tp - 1) {
					pg++; sel = pg * ITEMS_PP; draw_list(roms, cnt, pg, sel); delay(250);
				}
			}
		}

		// Up
		if ((b & GB_BTN_UP) && !(prev & GB_BTN_UP)) {
			if (sel > 0) sel--; else if (pg>0) { pg--; sel = min(total-1, pg*ITEMS_PP+ITEMS_PP-1); }
			draw_list(roms,cnt,pg,sel);
		}
		// Down
		if ((b & GB_BTN_DOWN) && !(prev & GB_BTN_DOWN)) {
			if (sel < total-1 && sel < (pg+1)*ITEMS_PP-1) sel++; else if (pg<tp-1) { pg++; sel = pg*ITEMS_PP; }
			draw_list(roms,cnt,pg,sel);
		}
		// Left = prev page
		if ((b & GB_BTN_LEFT) && !(prev & GB_BTN_LEFT)) {
			if (pg>0) { pg--; sel = pg*ITEMS_PP; draw_list(roms,cnt,pg,sel); }
		}
		// Right = next page
		if ((b & GB_BTN_RIGHT) && !(prev & GB_BTN_RIGHT)) {
			if (pg<tp-1) { pg++; sel = pg*ITEMS_PP; draw_list(roms,cnt,pg,sel); }
		}
		// A = select
		if ((b & GB_BTN_A) && !(prev & GB_BTN_A)) {
			if (sel == cnt) return LAUNCHER_SEL_BT_SCANNER;
			if (sel == cnt + 1) return LAUNCHER_SEL_USB_MANAGER;
			if (sel == cnt + 2) return LAUNCHER_SEL_SETTINGS;
			return sel;
		}

		prev = b;
		if (millis()-dbg_t>3000) { dbg_t=millis(); Serial.printf("[LAUNCH] pg=%d sel=%d\n",pg,sel); }
		delay(20);
	}
}

// ─── In-game menu ───────────────────────────────────────────────────────────
static void mbtn(int x, int w, int y, const char* t, uint16_t fg, bool hl) {
	uint16_t bg = hl ? 0x2945 : 0x1082;
	tft.fillRoundRect(x,y,w,26,5,bg);
	tft.drawRoundRect(x,y,w,26,5,0x528A);
	tft.setTextColor(fg,bg); tft.setTextDatum(MC_DATUM);
	tft.drawString(t,SCREEN_W/2,y+13,2);
}

int launcher_ingame_menu() {
	const int panel_w = SCREEN_W - 24;
	const int panel_x = (SCREEN_W - panel_w) / 2;
	const int panel_y = 10;
	const int panel_h = 220;
	const int btn_w = panel_w - 24;
	const int btn_x = panel_x + 12;

	tft.fillRect(panel_x,panel_y,panel_w,panel_h,TFT_BLACK);
	tft.drawRoundRect(panel_x,panel_y,panel_w,panel_h,6,0x528A);
	tft.setTextColor(0xFFE0,TFT_BLACK); tft.setTextDatum(MC_DATUM);
	tft.drawString("PAUSED",SCREEN_W/2,28,4);

	#define MI 5
	int yp[MI]={58,88,118,148,178};
	const char* lb[MI]={"Resume","Save State","Load State","Settings","Quit"};
	uint16_t fc[MI]={TFT_GREEN,0x07FF,0x07FF,0xFFE0,TFT_RED};
	for(int i=0;i<MI;i++) mbtn(btn_x,btn_w,yp[i],lb[i],fc[i],false);
	wait_release();

	int hl=0;
	mbtn(btn_x,btn_w,yp[hl],lb[hl],fc[hl],true);
	uint16_t prev = 0;
	while(true) {
		button_update();
		uint16_t b = button_get_buttons();

		if (touch_is_pressed()) {
			int16_t tx = touch_get_x(), ty = touch_get_y();
			if (tx >= btn_x && tx <= btn_x + btn_w) {
				for (int i = 0; i < MI; i++) {
					if (ty >= yp[i] && ty < yp[i] + 26) {
						if (hl != i) {
							mbtn(btn_x, btn_w, yp[hl], lb[hl], fc[hl], false);
							hl = i;
							mbtn(btn_x, btn_w, yp[hl], lb[hl], fc[hl], true);
						}
						delay(200);
						switch(hl) {
							case 0: return 0;
							case 1: return 1;
							case 2: return 2;
							case 3: return 5;
							case 4: return 3;
						}
					}
				}
			}
		}

		if ((b & GB_BTN_UP) && !(prev & GB_BTN_UP)) {
			mbtn(btn_x,btn_w,yp[hl],lb[hl],fc[hl],false);
			hl = (hl==0)?MI-1:hl-1;
			mbtn(btn_x,btn_w,yp[hl],lb[hl],fc[hl],true);
		}
		if ((b & GB_BTN_DOWN) && !(prev & GB_BTN_DOWN)) {
			mbtn(btn_x,btn_w,yp[hl],lb[hl],fc[hl],false);
			hl = (hl+1)%MI;
			mbtn(btn_x,btn_w,yp[hl],lb[hl],fc[hl],true);
		}
		// A = select
		if ((b & GB_BTN_A) && !(prev & GB_BTN_A)) {
			int s = hl;
			// 0=resume 1=save 2=load 3=settings 4=quit
			switch(s){case 0:return 0;case 1:return 1;case 2:return 2;case 3:return 5;case 4:return 3;}
		}
		// B = cancel -> resume
		if ((b & GB_BTN_B) && !(prev & GB_BTN_B)) return 0;

		prev = b;
		delay(15);
	}
}

// ─── Settings menu ──────────────────────────────────────────────────────────
#if ENABLE_SOUND
#define SETTINGS_NUM_ROWS 6
#define SETTINGS_ROW_DONE 5
#else
#define SETTINGS_NUM_ROWS 4
#define SETTINGS_ROW_DONE 3
#endif

void launcher_settings_menu(bool* show_fps_overlay, bool* show_save_overlay) {
	uint8_t pal = emu_get_palette();
	uint8_t fs = emu_get_frame_skip();
	uint8_t bl = 255; // brightness
	touch_load_settings(&pal, &fs, &bl, show_fps_overlay, show_save_overlay);

	int sel = 0;
	uint16_t prev = 0;

	auto draw_settings = [&](int selrow) {
		const int row_x = 12;
		const int row_w = SCREEN_W - 24;
		const int arrow_l = row_x + 10;
		const int arrow_r = row_x + row_w - 10;

		tft.fillScreen(TFT_BLACK);
		tft.setTextDatum(MC_DATUM);
		tft.setTextColor(0xFFE0); tft.drawString("SETTINGS",SCREEN_W/2,15,4);

		// Palette
		tft.setTextColor(TFT_WHITE); tft.drawString("Color Palette:",SCREEN_W/2,40,2);
		uint16_t palbg = (selrow==0)?0x2945:0x1082;
		tft.fillRoundRect(row_x,52,row_w,24,5,palbg);
		char palstr[40]; snprintf(palstr,40,"%d/%d %s",pal+1,NUM_PALETTES,emu_get_palette_name(pal));
		tft.setTextColor(0x07E0,palbg);
		tft.drawString(palstr,SCREEN_W/2,64,2);
		tft.setTextColor(0x7BEF,palbg);
		tft.setTextDatum(ML_DATUM); tft.drawString("<<",arrow_l,64,2);
		tft.setTextDatum(MR_DATUM); tft.drawString(">>",arrow_r,64,2);

		// Frame skip
		tft.setTextDatum(MC_DATUM);
		uint16_t fsbg = (selrow==1)?0x2945:0x1082;
		tft.fillRoundRect(row_x,97,row_w,24,5,fsbg);
		char fss[32];
		if (fs == 0) snprintf(fss, sizeof(fss), "0 (No Skip, 40 FPS)");
		else if (fs == 1) snprintf(fss, sizeof(fss), "1 (Fast, 60 FPS)");
		else if (fs == 2) snprintf(fss, sizeof(fss), "2 (Smooth 60 FPS)");
		else snprintf(fss, sizeof(fss), "%d (Skip %d)", fs, fs);
		tft.setTextColor(0x07E0,fsbg); tft.drawString(fss,SCREEN_W/2,109,2);
		tft.setTextColor(0x7BEF,fsbg);
		tft.setTextDatum(ML_DATUM); tft.drawString("<",arrow_l,109,2);
		tft.setTextDatum(MR_DATUM); tft.drawString(">",arrow_r,109,2);

		// Brightness
		tft.setTextDatum(MC_DATUM);
		tft.setTextColor(TFT_WHITE); tft.drawString("Brightness:",SCREEN_W/2,130,2);
		uint16_t blbg = (selrow==2)?0x2945:0x1082;
		tft.fillRoundRect(row_x,142,row_w,24,5,blbg);
		char bls[16]; snprintf(bls,16,"%d%%",bl*100/255);
		tft.setTextColor(0x07E0,blbg); tft.drawString(bls,SCREEN_W/2,154,2);
		tft.setTextColor(0x7BEF,blbg);
		tft.setTextDatum(ML_DATUM); tft.drawString("<",arrow_l,154,2);
		tft.setTextDatum(MR_DATUM); tft.drawString(">",arrow_r,154,2);

#if ENABLE_SOUND
		// Game Sound
		tft.setTextDatum(MC_DATUM);
		tft.setTextColor(TFT_WHITE); tft.drawString("Game Sound:",SCREEN_W/2,175,2);
		uint16_t sndbg = (selrow==3)?0x2945:0x1082;
		tft.fillRoundRect(row_x,187,row_w,24,5,sndbg);
		tft.setTextColor(0x07E0,sndbg); tft.drawString(audio_get_volume_str(),SCREEN_W/2,199,2);
		tft.setTextColor(0x7BEF,sndbg);
		tft.setTextDatum(ML_DATUM); tft.drawString("<",arrow_l,199,2);
		tft.setTextDatum(MR_DATUM); tft.drawString(">",arrow_r,199,2);

		// Menu Music (BGM)
		tft.setTextDatum(MC_DATUM);
		tft.setTextColor(TFT_WHITE); tft.drawString("Menu Music (BGM):",SCREEN_W/2,220,2);
		uint16_t bgmbg = (selrow==4)?0x2945:0x1082;
		tft.fillRoundRect(row_x,232,row_w,24,5,bgmbg);
		bool bgm_on = bgm_is_enabled();
		tft.setTextColor(bgm_on ? 0x07E0 : 0xF800, bgmbg);
		tft.drawString(bgm_on ? "ENABLED" : "DISABLED", SCREEN_W/2, 244, 2);
		tft.setTextColor(0x7BEF,bgmbg);
		tft.setTextDatum(ML_DATUM); tft.drawString("<",arrow_l,244,2);
		tft.setTextDatum(MR_DATUM); tft.drawString(">",arrow_r,244,2);

		// Done button
		uint16_t donebg = (selrow==5)?0x2945:0x07E0;
		tft.fillRoundRect(60,272,120,28,5,donebg);
		tft.setTextColor(TFT_BLACK,donebg); tft.setTextDatum(MC_DATUM);
		tft.drawString("DONE",SCREEN_W/2,286,2);
#else
		// Done button (no sound options)
		uint16_t donebg = (selrow==3)?0x2945:0x07E0;
		tft.fillRoundRect(60,195,120,32,5,donebg);
		tft.setTextColor(TFT_BLACK,donebg); tft.setTextDatum(MC_DATUM);
		tft.drawString("DONE",SCREEN_W/2,211,2);
#endif
	};

	draw_settings(sel);
	wait_release();

	while(true) {
		button_update();
		uint16_t b = button_get_buttons();

		if (touch_is_pressed()) {
			int16_t tx = touch_get_x(), ty = touch_get_y();
			if (ty >= 40 && ty < 80) {
				if (tx < 120) { pal = (pal+NUM_PALETTES-1)%NUM_PALETTES; }
				else { pal = (pal+1)%NUM_PALETTES; }
				emu_set_palette(pal);
				draw_settings(0);
				delay(200);
			} else if (ty >= 85 && ty < 125) {
				if (tx < 120 && fs > 0) fs--;
				else if (tx >= 120 && fs < 4) fs++;
				emu_set_frame_skip(fs);
				draw_settings(1);
				delay(200);
			} else if (ty >= 130 && ty < 170) {
				if (tx < 120 && bl > 30) bl -= 25;
				else if (tx >= 120 && bl < 255) bl = min(255, bl + 25);
				display_set_backlight(bl);
				draw_settings(2);
				delay(200);
#if ENABLE_SOUND
			} else if (ty >= 175 && ty < 215) {
				if (tx < 120) {
					uint8_t v = audio_get_volume();
					if (v > 0) audio_set_volume(v - 1);
				} else {
					uint8_t v = audio_get_volume();
					if (v < 3) audio_set_volume(v + 1);
				}
				draw_settings(3);
				delay(200);
			} else if (ty >= 220 && ty < 260) {
				bgm_set_enabled(!bgm_is_enabled());
				draw_settings(4);
				delay(200);
			} else if (ty >= 265) {
				touch_save_settings(pal, fs, bl, false, false);
				wait_release();
				return;
			}
#else
			} else if (ty >= 185) {
				touch_save_settings(pal, fs, bl, false, false);
				wait_release();
				return;
			}
#endif
		}

		// Navigation: up/down change selected row (edge detect)
		if ((b & GB_BTN_UP) && !(prev & GB_BTN_UP)) { sel = (sel==0)?SETTINGS_NUM_ROWS-1:sel-1; draw_settings(sel); }
		if ((b & GB_BTN_DOWN) && !(prev & GB_BTN_DOWN)) { sel = (sel+1)%SETTINGS_NUM_ROWS; draw_settings(sel); }

		// Row actions (use edge detection where appropriate)
		if (sel==0) {
			if ((b & GB_BTN_LEFT) && !(prev & GB_BTN_LEFT)) { pal = (pal+NUM_PALETTES-1)%NUM_PALETTES; emu_set_palette(pal); draw_settings(sel); }
			if ((b & GB_BTN_RIGHT) && !(prev & GB_BTN_RIGHT)) { pal = (pal+1)%NUM_PALETTES; emu_set_palette(pal); draw_settings(sel); }
		} else if (sel==1) {
			if ((b & GB_BTN_LEFT) && !(prev & GB_BTN_LEFT)) { if (fs>0) { fs--; emu_set_frame_skip(fs); draw_settings(sel); } }
			if ((b & GB_BTN_RIGHT) && !(prev & GB_BTN_RIGHT)) { if (fs<4) { fs++; emu_set_frame_skip(fs); draw_settings(sel); } }
		} else if (sel==2) {
			if ((b & GB_BTN_LEFT) && !(prev & GB_BTN_LEFT)) { if (bl>30) { bl-=25; display_set_backlight(bl); draw_settings(sel); } }
			if ((b & GB_BTN_RIGHT) && !(prev & GB_BTN_RIGHT)) { if (bl<255) { bl=min(255,bl+25); display_set_backlight(bl); draw_settings(sel); } }
#if ENABLE_SOUND
		} else if (sel==3) {
			if ((b & GB_BTN_LEFT) && !(prev & GB_BTN_LEFT)) {
				uint8_t v = audio_get_volume();
				if (v > 0) { audio_set_volume(v - 1); draw_settings(sel); }
			}
			if ((b & GB_BTN_RIGHT) && !(prev & GB_BTN_RIGHT)) {
				uint8_t v = audio_get_volume();
				if (v < 3) { audio_set_volume(v + 1); draw_settings(sel); }
			}
		} else if (sel==4) {
			if (((b & GB_BTN_LEFT) && !(prev & GB_BTN_LEFT)) || ((b & GB_BTN_RIGHT) && !(prev & GB_BTN_RIGHT))) {
				bgm_set_enabled(!bgm_is_enabled());
				draw_settings(sel);
			}
#endif
		} else if (sel==SETTINGS_ROW_DONE) {
			if ((b & GB_BTN_A) && !(prev & GB_BTN_A)) {
				touch_save_settings(pal, fs, bl, false, false);
				wait_release();
				return;
			}
		}

		// A button on actionable rows
		if ((b & GB_BTN_A) && !(prev & GB_BTN_A)) {
#if ENABLE_SOUND
			if (sel == 3) {
				audio_cycle_volume();
				draw_settings(sel);
			} else if (sel == 4) {
				bgm_set_enabled(!bgm_is_enabled());
				draw_settings(sel);
			} else if (sel < SETTINGS_ROW_DONE) {
				sel = (sel+1)%SETTINGS_NUM_ROWS;
				draw_settings(sel);
			}
#else
			if (sel < SETTINGS_ROW_DONE) {
				sel = (sel+1)%SETTINGS_NUM_ROWS;
				draw_settings(sel);
			}
#endif
		}

		prev = b;
		delay(20);
	}
}
