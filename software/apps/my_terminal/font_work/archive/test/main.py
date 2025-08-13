from PIL import Image, ImageDraw, ImageFont
import freetype

# Configuration
CHAR_WIDTH = 8
CHAR_HEIGHT = 16
FONT_FILE = "PxPlus_IBM_VGA_8x16.ttf"
HEADER_FILE = "font_8x16.h"
PREVIEW_FILE = "font_preview.png"
FG_COLOR = (255, 255, 255)
BG_COLOR = (0, 0, 0)
HIGHLIGHT_INDEX = 187  # Full block character appears here in this font

# Scaling settings for preview clarity
SCALE = 3
CELL_WIDTH = CHAR_WIDTH * SCALE
CELL_HEIGHT = (CHAR_HEIGHT + 10) * SCALE
LABEL_FONT_SIZE = 12 * SCALE
LABEL_FONT = ImageFont.load_default()
LABEL_COLOR = (255, 255, 0)

# CP437 to Unicode mapping (may contain >256 entries)
CP437_TO_UNICODE = [
    0x0000, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
    0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
    0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8,
    0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC,
    *range(0x0020, 0x007F), 0x2302,
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x2518, 0x250C, 0x2588, 0x2584, 0x2580,
    0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566,
    0x2560, 0x2550, 0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559,
    0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2516, 0x2517, 0x2519,
    0x251B, 0x2512, 0x2513, 0x2511, 0x2515, 0x251A, 0x2501, 0x2503,
    0x2505, 0x2507, 0x2509, 0x250B, 0x250D, 0x250F, 0x251D, 0x251E,
    0x251F, 0x2520, 0x2521, 0x2522, 0x2523, 0x2525, 0x2526, 0x2527,
    0x2528, 0x2529, 0x252A, 0x252B, 0x252D, 0x252E, 0x252F, 0x2530,
    0x2531, 0x2532, 0x2533, 0x2535, 0x2536, 0x2537, 0x2538, 0x2539,
    0x253A, 0x253B, 0x253D, 0x253E, 0x253F, 0x2540, 0x2541, 0x2542,
    0x2543, 0x2544, 0x2545, 0x2546, 0x2547, 0x2548, 0x2549, 0x254A,
    0x254B, 0x254C, 0x254D, 0x254E, 0x254F, 0x2554, 0x2555, 0x2556
]

def extract_glyph_bytes_by_index(face, glyph_index):
    face.load_glyph(glyph_index, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
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
        data.append(0x00)
    return data[:CHAR_HEIGHT]

def render_glyph_by_index(face, glyph_index):
    face.load_glyph(glyph_index, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
    bitmap = face.glyph.bitmap
    img = Image.new("RGB", (CHAR_WIDTH, CHAR_HEIGHT), BG_COLOR)
    draw = ImageDraw.Draw(img)

    for y in range(bitmap.rows):
        for x in range(bitmap.width):
            byte = bitmap.buffer[y * bitmap.pitch + x // 8]
            if byte & (0x80 >> (x % 8)):
                draw.point((x, y), fill=FG_COLOR)
    return img

def build_preview_by_index(font_data, face):
    cols = 16
    rows = (len(font_data) + cols - 1) // cols
    preview_width = cols * CELL_WIDTH
    preview_height = rows * CELL_HEIGHT
    img = Image.new("RGB", (preview_width, preview_height), BG_COLOR)
    draw = ImageDraw.Draw(img)

    for i, _ in enumerate(font_data):
        glyph_img = render_glyph_by_index(face, face.get_char_index(CP437_TO_UNICODE[i])).resize((CELL_WIDTH, CHAR_HEIGHT * SCALE), Image.NEAREST)
        x = (i % cols) * CELL_WIDTH
        y = (i // cols) * CELL_HEIGHT

        img.paste(glyph_img, (x, y))

        label_y = y + CHAR_HEIGHT * SCALE + 2
        draw.text((x + 4, label_y), f"{i:03}", font=LABEL_FONT, fill=LABEL_COLOR)

        if i == HIGHLIGHT_INDEX:
            box = [x, y, x + CELL_WIDTH - 1, y + CHAR_HEIGHT * SCALE - 1]
            draw.rectangle(box, outline=(255, 0, 0), width=2)

    img.save(PREVIEW_FILE)
    print(f"Preview saved to {PREVIEW_FILE}")
def write_header(font_data):
    with open(HEADER_FILE, "w") as f:
        f.write("// Generated font header\n")
        f.write(f"#define FONT_WIDTH {CHAR_WIDTH}\n")
        f.write(f"#define FONT_HEIGHT {CHAR_HEIGHT}\n")
        f.write(f"#define FONT_CHAR_COUNT {len(font_data)}\n\n")
        f.write("const uint8_t font_data[][FONT_HEIGHT] = {\n")
        for glyph in font_data:
            f.write("    { " + ", ".join(f"0x{byte:02X}" for byte in glyph) + " },\n")
        f.write("};\n")
    print(f"Header saved to {HEADER_FILE}")

def main():
    face = freetype.Face(FONT_FILE)
    face.set_pixel_sizes(CHAR_WIDTH, CHAR_HEIGHT)

    font_data = []
    for i, codepoint in enumerate(CP437_TO_UNICODE):
        glyph_index = face.get_char_index(codepoint)
        if glyph_index == 0:
            print(f"Missing glyph for index {i} (U+{codepoint:04X})")
            font_data.append([0x00] * CHAR_HEIGHT)
        else:
            font_data.append(extract_glyph_bytes_by_index(face, glyph_index))

    write_header(font_data)
    build_preview_by_index(font_data, face)

if __name__ == "__main__":
    main()
