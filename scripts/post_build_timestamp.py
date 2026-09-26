from datetime import datetime
from pathlib import Path
import json
import shutil

Import("env")


def copy_firmware_with_timestamp(source, target, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    project_dir = Path(env.subst("$PROJECT_DIR"))
    firmware_bin = build_dir / f"{env.subst('$PROGNAME')}.bin"

    if not firmware_bin.exists():
        print(f"[post-build] firmware not found: {firmware_bin}")
        return

    now = datetime.now()
    timestamp = now.strftime("%Y%m%d_%H%M%S")
    date_str = now.strftime("%Y-%m-%d %H:%M")

    # 1. Archive build
    builds_dir = project_dir / "builds"
    builds_dir.mkdir(parents=True, exist_ok=True)
    archive_bin = builds_dir / f"cydboy-{timestamp}.bin"
    shutil.copy2(firmware_bin, archive_bin)
    print(f"[post-build] Archived firmware to {archive_bin}")

    # 2. Deploy to docs/ for GitHub Pages
    docs_dir = project_dir / "docs"
    docs_dir.mkdir(parents=True, exist_ok=True)
    docs_bin = docs_dir / "firmware.bin"
    shutil.copy2(firmware_bin, docs_bin)

    # 3. Deploy to tools/web-installer/ for local testing
    tools_dir = project_dir / "tools" / "web-installer"
    if tools_dir.exists():
        tools_bin = tools_dir / "firmware.bin"
        shutil.copy2(firmware_bin, tools_bin)

    # 4. Write metadata JSON to docs/
    size_bytes = firmware_bin.stat().st_size
    info = {
        "project": "CYDboy",
        "version": "v1.1",
        "build_date": date_str,
        "timestamp": timestamp,
        "size_bytes": size_bytes,
        "size_kb": round(size_bytes / 1024, 1),
        "board": "ESP32-2432S028R",
        "features": [
            "Game Boy & Game Boy Color (Walnut-CGB)",
            "Direct SD Sector Streaming (FAT32 cache)",
            "Bluepad32 Bluetooth Gamepads",
            "Hardware Save States"
        ],
        "upstream": "https://github.com/artanergin44-collab/cyd-gb",
        "repository": "https://github.com/Rocka84/cydboy"
    }

    info_path = docs_dir / "firmware_info.json"
    with open(info_path, "w", encoding="utf-8") as f:
        json.dump(info, f, indent=2)
    print(f"[post-build] Deployed firmware and info to {docs_dir}")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_firmware_with_timestamp)

