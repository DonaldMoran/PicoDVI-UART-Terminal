from PIL import Image, ImageDraw
import freetype

# Configuration
CHAR_WIDTH = 8
CHAR_HEIGHT = 16
CHARS = 256
FONT_FILE = "BmPlus_IBM_VGA_8x16.otb"
HEADER_FILE = "font_8x16.h"
PREVIEW_FILE = "font_preview.png"
FG_COLOR = (255, 255, 255)
BG_COLOR = (0, 0, 0)

# Extract monochrome glyph bytes (16 bytes per char)
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
    return data

# Render a glyph as an RGB image for preview
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

# Save a grid PNG with all glyphs
def build_preview(font_data, face):
    cols, rows = 16, 16
    img = Image.new("RGB", (cols * CHAR_WIDTH, rows * CHAR_HEIGHT), BG_COLOR)

    for i, _ in enumerate(font_data):
        glyph_img = render_glyph(face, i)
        x = (i % cols) * CHAR_WIDTH
        y = (i // cols) * CHAR_HEIGHT
        img.paste(glyph_img, (x, y))

    img.save(PREVIEW_FILE)
    print(f"✅ Saved preview to {PREVIEW_FILE}")

# Export glyph bytes as C header
def export_header(font_data):
    with open(HEADER_FILE, "w") as f:
        f.write("#ifndef FONT_8X16_H\n")
        f.write("#define FONT_8X16_H\n\n")
        f.write("static const uint8_t font_8x16[256][16] = {\n")
        for i, glyph in enumerate(font_data):
            hex_bytes = ", ".join(f"0x{byte:02x}" for byte in glyph)
            f.write(f"    {{ {hex_bytes} }},\n")
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
