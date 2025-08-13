from PIL import Image, ImageDraw, ImageFont
import freetype

# Configuration
CHAR_WIDTH = 8
CHAR_HEIGHT = 16
CHARS = 256
FONT_FILE = "Bm437_IBM_VGA_8x16.otb"
HEADER_FILE = "font_8x16.h"
PREVIEW_FILE = "font_preview.png"
FG_COLOR = (255, 255, 255)
BG_COLOR = (0, 0, 0)
HIGHLIGHT_INDEX = 219  # CP437 solid block

# Scaling settings for preview clarity
SCALE = 3  # Try 2 or 3 for readable labels
CELL_WIDTH = CHAR_WIDTH * SCALE
CELL_HEIGHT = (CHAR_HEIGHT + 10) * SCALE

LABEL_FONT_SIZE = 12 * SCALE
LABEL_FONT = ImageFont.load_default()
LABEL_COLOR = (255, 255, 0)


def extract_glyph_bytes(face, char_code):
    face.load_char(char_code, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
    bitmap = face.glyph.bitmap
    rows = bitmap.rows
    pitch = bitmap.pitch
    data = []

    for y in range(rows):
        row = 0
        for x in range(CHAR_WIDTH):
            byte = bitmap.buffer[y * pitch + x // 8]
            if byte & (0x80 >> (x % 8)):
                row |= (1 << (7 - x))
        data.append(row)
    while len(data) < CHAR_HEIGHT:
        data.append(0x00)  # pad if shorter than expected
    return data[:CHAR_HEIGHT]


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

    for i, _ in enumerate(font_data):
        glyph_img = render_glyph(face, i).resize((CELL_WIDTH, CHAR_HEIGHT * SCALE), Image.NEAREST)
        x = (i % cols) * CELL_WIDTH
        y = (i // cols) * CELL_HEIGHT

        img.paste(glyph_img, (x, y))

        label_y = y + CHAR_HEIGHT * SCALE + 2
        draw.text((x + 4, label_y), f"{i:03}", font=LABEL_FONT, fill=LABEL_COLOR)

        if i == HIGHLIGHT_INDEX:
            box = [x, y, x + CELL_WIDTH - 1, y + CHAR_HEIGHT * SCALE - 1]
            draw.rectangle(box, outline="red", width=2)

    img.save(PREVIEW_FILE)
    print(f"✅ Saved improved preview to {PREVIEW_FILE}")


def export_header(font_data):
    with open(HEADER_FILE, "w") as f:
        f.write("#ifndef FONT_8X16_H\n")
        f.write("#define FONT_8X16_H\n\n")
        f.write("static const uint8_t font_8x16[256][16] = {\n")
        for i, glyph in enumerate(font_data):
            hex_bytes = ", ".join(f"0x{byte:02x}" for byte in glyph)
            f.write(f"    {{ {hex_bytes} }},  // {i:3}\n")
        f.write("};\n\n")
        f.write("#endif\n")
    print(f"📦 Exported header to {HEADER_FILE}")


def main():
    face = freetype.Face(FONT_FILE)
    face.set_pixel_sizes(0, CHAR_HEIGHT)

    font_data = [extract_glyph_bytes(face, i) for i in range(CHARS)]
    build_preview(font_data, face)
    export_header(font_data)


if __name__ == "__main__":
    main()
