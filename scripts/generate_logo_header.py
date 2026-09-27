from PIL import Image

def generate():
    im = Image.open('docs/assets/logo.png').convert('RGBA')
    bbox = im.getbbox()
    cropped = im.crop(bbox)
    target_w = 200
    target_h = int(cropped.height * target_w / cropped.width)
    resized = cropped.resize((target_w, target_h), Image.Resampling.LANCZOS)

    bg = Image.new('RGBA', resized.size, (0, 0, 0, 255))
    comp = Image.alpha_composite(bg, resized).convert('RGB')

    w, h = comp.size
    pixels = []
    for y in range(h):
        for x in range(w):
            r, g, b = comp.getpixel((x, y))
            r5 = (r >> 3) & 0x1F
            g6 = (g >> 2) & 0x3F
            b5 = (b >> 3) & 0x1F
            rgb565 = (r5 << 11) | (g6 << 5) | b5
            pixels.append(f"0x{rgb565:04X}")

    header_content = [
        "#pragma once",
        "#include <stdint.h>",
        "#include <pgmspace.h>",
        "",
        f"#define CYDBOY_LOGO_W {w}",
        f"#define CYDBOY_LOGO_H {h}",
        "",
        f"static const uint16_t cydboy_logo_bitmap[{w * h}] PROGMEM = {{"
    ]

    for i in range(0, len(pixels), 12):
        chunk = ", ".join(pixels[i:i+12])
        if i + 12 < len(pixels):
            chunk += ","
        header_content.append("    " + chunk)

    header_content.append("};")
    header_content.append("")

    with open('include/cydboy_logo.h', 'w') as f:
        f.write("\n".join(header_content))

    print(f"Generated include/cydboy_logo.h: {w}x{h} ({w*h*2} bytes)")

if __name__ == '__main__':
    generate()
