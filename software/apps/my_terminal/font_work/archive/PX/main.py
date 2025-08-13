from PIL import Image, ImageDraw, ImageFont
import freetype

# Configuration
CHAR_WIDTH = 8
CHAR_HEIGHT = 16
CHARS = 256
FONT_FILE = "Px437_IBM_VGA_8x16.ttf"
HEADER_FILE = "font_8x16.h"
PREVIEW_FILE = "font_preview.png"
HIGHLIGHT_INDEX = 219  # Full block

# Colors & Layout
FG_COLOR = (255, 255, 255)
BG_COLOR = (0, 0, 0)
LABEL_COLOR = (255, 255, 0)
SCALE = 3
CELL_WIDTH = CHAR_WIDTH * SCALE
CELL_HEIGHT = (CHAR_HEIGHT + 10) * SCALE
LABEL_FONT = ImageFont.load_default()


def extract_glyph_bytes(face, char_code):
    face.load_char(char_code, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
    bitmap = face.glyph.bitmap
    rows = bitmap.rows
    pitch = bitmap.pitch
    data = []

    for y in range(CHAR_HEIGHT):
        row = 0
        for x in range(CHAR_WIDTH):
            if y < rows:
                byte = bitmap.buffer[y * pitch + x // 8]
                if byte & (0x80 >> (x % 8)):
                    row |= (1 << (7 - x))
        data.append(row)
    return data


def render_glyph(face, char_code):
    face.load_char(char_code, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
    bitmap = face.glyph.bitmap
    img = Image.new("RGB", (CHAR_WIDTH, CHAR_HEIGHT), BG_COLOR)
    draw = ImageDraw.Draw(img)

    for y in range(bitmap.rows):
        for x in range(bitmap.width):
            byte = bitmap.buffer[y * bitmap.pitch + x // 8]
            if byte & (0x80 >> (x % 8)):
                draw.point((x, y), fill=FG_COLOR)
    return img


def build_preview(font_data, face):
    cols, rows = 16, 16
    preview_width = cols * CELL_WIDTH
    preview_height = rows * CELL_HEIGHT
    img = Image.new("RGB", (preview_width, preview_height), BG_COLOR)
    draw = ImageDraw.Draw(img)

    for i in range(CHARS):
        glyph_img = render_glyph(face, i).resize((CELL_WIDTH, CHAR_HEIGHT * SCALE), Image.NEAREST)
        x = (i % cols) * CELL_WIDTH
        y = (i // cols) * CELL_HEIGHT
        img.paste(glyph_img, (x, y))

        # Label below glyph
        label_y = y + CHAR_HEIGHT * SCALE + 2
        draw.text((x + 4, label_y), f"{i:03}", font=LABEL_FONT, fill=LABEL_COLOR)

        # Highlight character 219 with red box
        if i == HIGHLIGHT_INDEX:
            box = [x, y, x + CELL_WIDTH - 1, y + CHAR_HEIGHT * SCALE - 1]
            draw.rectangle(box, outline="red", width=2)

    img.save(PREVIEW_FILE)
    print(f"🖼️ Preview saved to {PREVIEW_FILE}")


def export_header(font_data):
    with open(HEADER_FILE, "w") as f:
        f.write("#ifndef FONT_8X16_H\n#define FONT_8X16_H\n\n")
        f.write("static const uint8_t font_8x16[256][16] = {\n")
        for i, glyph in enumerate(font_data):
            hex_bytes = ", ".join(f"0x{byte:02x}" for byte in glyph)
            f.write(f"    {{ {hex_bytes} }},  // {i:3}\n")
        f.write("};\n\n#endif\n")
    print(f"📦 Header exported to {HEADER_FILE}")


def main():
    face = freetype.Face(FONT_FILE)
    face.set_pixel_sizes(CHAR_WIDTH, CHAR_HEIGHT)
    font_data = [extract_glyph_bytes(face, i) for i in range(CHARS)]

    # 🔍 Diagnostic output: Show binary glyph for char 219 (0xDB)
    print("\nCharacter 219 glyph scanlines (binary):")
    for row in font_data[219]:
        print(f"{row:08b}")
    print()

    build_preview(font_data, face)
    export_header(font_data)

if __name__ == "__main__":
    main()
