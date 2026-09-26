<p align="center">
  <img src="docs/assets/logo.svg" alt="CYDboy Logo" width="420">
</p>

<p align="center">
  <strong>Game Boy (DMG) & Game Boy Color (GBC) on the ESP32 Cheap Yellow Display (ESP32-2432S028R)</strong>
</p>

<p align="center">
  <a href="https://rocka84.github.io/CYDboy/"><img src="https://img.shields.io/badge/⚡_1--Click_Web_Flasher-Live_Site-00f2fe?style=for-the-badge" alt="Web Flasher"></a>
  <a href="https://github.com/Rocka84/CYDboy"><img src="https://img.shields.io/badge/GitHub-Rocka84%2FCYDboy-facc15?style=for-the-badge&logo=github" alt="GitHub Repo"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-10b981?style=for-the-badge" alt="License"></a>
</p>

---

## 🌟 Overview & Web Portal

**CYDboy** brings the legendary Nintendo Game Boy and Game Boy Color gaming experience to the ESP32-2432S028R "Cheap Yellow Display" (CYD). 

Featuring full CGB color emulation (Walnut-CGB), direct FAT32 cluster streaming from microSD (zero PSRAM needed!), Bluetooth gamepad pairing, hardware save states, and a **1-click Web Serial flasher** right in your browser.

> ⚡ **Try it now in your browser:**  
> **[https://rocka84.github.io/CYDboy/](https://rocka84.github.io/CYDboy/)**  
> Flash the latest firmware and manage your microSD ROMs over USB without installing any software or compilers!

---

## ✨ Features

- **Full Game Boy & Game Boy Color (GBC) Support** — Powered by the Walnut-CGB core: authentic double-speed CPU mode, dual-bank VRAM (16 KB), 8-bank WRAM (32 KB), CGB color palettes, and HDMA/GDMA support.
- **No PSRAM Required** — Highly optimized memory footprint tailored for stock ESP32 internal SRAM (320 KB total).
- **High-Performance Direct SD Streaming** — Direct FAT32 cluster table traversal to build contiguous disk extents on ROM open. Uses raw SPI sector reads (`SD.readRAW`) bypassing VFS overhead, backed by a 512-byte hardware-aligned page cache with hash indexing for fluid 50+ FPS gameplay with 0 misses.
- **Bluetooth Gamepad Support (Bluepad32)** — Connect Xbox, PlayStation (PS4/PS5), Nintendo Switch Pro, 8BitDo, and generic Bluetooth controllers. Auto-pauses background inquiry scanning once connected for ultra-low latency.
- **Auto-Hiding Touch Controls** — Automatically hides on-screen controls when a Bluetooth controller connects, freeing up the display.
- **1-Click Web Flasher & SD ROM Manager** — Flash releases in 1 click and upload `.gb` / `.gbc` ROMs directly over USB via Google Chrome or Edge at [https://rocka84.github.io/CYDboy/](https://rocka84.github.io/CYDboy/).
- **True Save States** — Full emulator state serialization (CPU, registers, timers, VRAM, WRAM, OAM, I/O, palettes, cart RAM) saved to `/saves/<rom>.state`. Supports long filenames (up to 160 characters) and instant resume from the pause menu!
- **Battery Saves (SRAM)** — Automatic `.sav` battery backup for cartridge games (Pokémon, Zelda, Wario Land, etc.).
- **20 Curated Color Palettes (DMG Mode)** — Classic Green, Original DMG, Pocket Gray, Warm Sepia, Lava, Neon, Ocean, Forest, Gold, and more for classic monochrome Game Boy titles.
- **I2C Button Board Support** — Optional PCF8574 I2C button board auto-detection on GPIO 16/17 for DIY handheld shells.
- **Persistent Settings (NVS)** — Palette, frame skip, backlight brightness, and 5-point touch calibration remembered across reboots.

---

## 🛠️ Hardware Requirements

| Component | Description |
|-----------|-------------|
| **Device** | [ESP32-2432S028R](https://makeradvisor.com/tools/cyd-cheap-yellow-display-esp32-2432s028r/) ("Cheap Yellow Display", 2.8" TFT 240×320 resistive touch) |
| **MicroSD Card** | Formatted as FAT32 (any standard capacity up to 32 GB+) |
| **Gamepad (Optional)** | Standard Bluetooth gamepad (Xbox One/Series, DualShock 4, DualSense, Switch Pro, 8BitDo, etc.) |
| **Hardware Buttons (Optional)** | PCF8574 I2C button board connected to SDA (GPIO 16) and SCL (GPIO 17) |

---

## 🚀 Quick Start

### Option A: 1-Click Web Flasher (Recommended)

1. Connect your CYD to your computer via USB-C.
2. Open **[https://rocka84.github.io/CYDboy/](https://rocka84.github.io/CYDboy/)** in Google Chrome or Microsoft Edge.
3. Click **"Connect & Flash CYDboy"**. That's it!

### Option B: Build from Source (PlatformIO)

```bash
git clone https://github.com/Rocka84/CYDboy.git
cd CYDboy
pio run -t upload --upload-port /dev/ttyUSB0
```


### 2. Prepare the MicroSD Card

Format your microSD card as **FAT32** and create this folder structure:

```
SD Card/
├── roms/
│   ├── gb/      <- Place your .gb ROM files here
│   └── gbc/     <- Place your .gbc ROM files here
└── saves/       <- Created automatically for .sav and .state files
```

### 3. Build & Flash (PlatformIO)

Connect your CYD board via USB:

```bash
# Build and flash firmware
pio run -t upload --upload-port /dev/ttyUSB0

# (Optional) Open serial monitor
pio device monitor -b 115200 --port /dev/ttyUSB0
```

> **Note:**
> - On Windows, replace `/dev/ttyUSB0` with your COM port (e.g. `COM3`).
> - On macOS, use `/dev/tty.usbserial-*` or `/dev/cu.usbserial-*`.

---

## 🌐 Web Serial 1-Click Flasher & ROM Manager

CYDboy includes a full browser-based 1-click flasher and SD file manager hosted live on GitHub Pages:

👉 **[https://rocka84.github.io/CYDboy/](https://rocka84.github.io/CYDboy/)**  
*(Also available locally at [`docs/index.html`](docs/index.html))*

1. Open the page in **Google Chrome** or **Microsoft Edge**.
2. **To Flash:** Connect your CYD via USB and click **"Connect & Flash CYDboy"** — no file downloads or IDE required!
3. **To Manage SD ROMs:**
   - On the CYD launcher screen, select **"USB ROM Manager"**.
   - Click **Connect CYD USB** in the web interface.
   - View SD storage capacity, drag & drop `.gb` and `.gbc` ROMs, or delete titles directly.

---

## 🎮 Controls & Gameplay

### On-Screen Touch Controls

| Control | Position | Description |
|---------|----------|-------------|
| **D-Pad** | Bottom-left | 68×68 cross; directional arrows (supports diagonal inputs) |
| **A / B** | Bottom-right | A (upper-right, red) and B (lower-left, blue) |
| **START** | Bottom-center | Start button (`STA`) |
| **SELECT** | Bottom-center | Select button (`SEL`) |
| **Pause Menu** | Top-right | Green **`\|\|`** icon opens in-game pause menu |

### Bluetooth Gamepad Controls

When a Bluetooth gamepad is connected:
- **D-Pad / Left Stick**: Move
- **A / B**: A and B action buttons
- **Start / Select**: Game Boy Start and Select
- **In-Game Pause Menu**: Press **Start + Select** (either simultaneously or sequentially within 800 ms), or **L1 + R1** / **Menu** button.
- **On-Screen Touch Controls**: Automatically hidden for a distraction-free display.

### In-Game Pause Menu

Pressing the **`\|\|`** button or the gamepad menu combo opens the in-game menu:
- **Resume** — Return to the active game.
- **Save State** — Instant full-state snapshot saved to `/saves/<ROM>.state`.
- **Load State** — Restore the snapshot seamlessly without resetting the game.
- **Settings** — Choose from 20 color palettes, adjust frame skip (0–4), or change display backlight brightness.
- **Quit** — Save battery SRAM (`.sav`) and return to the ROM launcher.

---

## ⚙️ Project Structure

```
cyd-gb/
├── platformio.ini         # PlatformIO environment and library configuration
├── partitions.csv         # Custom flash partition table
├── include/
│   ├── bt_controller.h    # Bluepad32 Bluetooth gamepad driver & input tester
│   ├── button_input.h     # Combined input manager (Touch, BT, PCF8574 I2C)
│   ├── display.h          # Display primitives and scanline pusher
│   ├── emulator_bridge.h  # Emulator bridge, raw sector cache & save state serialization
│   ├── hw_config.h        # Pin assignments, display geometry, touch coordinates
│   ├── sd_manager.h       # SD card mounting, ROM scanner, path helpers
│   ├── serial_manager.h   # Web Serial protocol handler for USB ROM management
│   ├── touch_input.h      # XPT2046 touch driver & 5-point calibration
│   ├── ui_launcher.h      # ROM list UI, BT indicator, in-game pause menu
│   └── walnut_cgb.h       # Walnut-CGB Game Boy & Game Boy Color emulator core (MIT)
├── src/
│   ├── bt_controller.cpp
│   ├── button_input.cpp
│   ├── display.cpp
│   ├── emulator_bridge.cpp
│   ├── main.cpp
│   ├── sd_manager.cpp
│   ├── serial_manager.cpp
│   ├── touch_input.cpp
│   └── ui_launcher.cpp
└── tools/
    └── web-installer/
        └── index.html     # Web Serial flasher & SD ROM file manager
```

---

## 🔧 Troubleshooting

| Issue | Solution |
|-------|----------|
| **SD Card Error on boot** | Verify microSD card is formatted as FAT32 and `/roms/gb` & `/roms/gbc` folders exist. |
| **Touchscreen uncalibrated** | Touch the `[CAL]` button in the launcher nav bar to run the 5-point calibration. Calibration is saved to NVS. |
| **Bluetooth controller input lag** | Once connected, background inquiry scanning is automatically paused. In the in-game Settings, set **Frame Skip** to `1` or `0` for instantaneous response. |
| **Bluetooth controller reconnect slow** | Fast interlaced page scan is enabled. Turn on controller before or right at boot; the indicator pill `[BT]` in the top-right header will turn green once connected. |
| **GBC ROM hacks / Color palettes** | Both standard Game Boy (`.gb`) and Game Boy Color (`.gbc`) ROMs and color hacks (e.g. *Super Mario Land 2 DX*, *Wario Land II*) are fully supported with authentic hardware palettes. |
| **USB upload fails (port locked)** | Disconnect any open Web Serial sessions in Google Chrome/Edge before running `pio run -t upload`. |

---

## 📜 Credits & License
 
- **[artanergin44-collab/cyd-gb](https://github.com/artanergin44-collab/cyd-gb)** — Upstream project and foundation for the initial ESP32 Cheap Yellow Display Game Boy port.
- **[Walnut](https://github.com/Gronis/walnut)** — High-performance, portable Game Boy & Game Boy Color emulator core (MIT License).
- **[Peanut-GB](https://github.com/deltabeard/Peanut-GB)** — Original lightweight DMG emulator core reference by Mahyar Koshkouei (MIT License).
- **[Bluepad32](https://github.com/ricardoquesada/bluepad32)** — Bluetooth gamepad library by Ricardo Quesada.
- **[TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)** — High-performance display driver by Bodmer.
- **[ESP32 Cheap Yellow Display Community](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display)** — Hardware documentation and pinouts.

Licensed under the **MIT License**.

