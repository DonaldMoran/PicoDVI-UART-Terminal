#!/usr/bin/env python
#-------------------------------------------------------------------------
#
#    TTF Font to C bitmap header file converter for dot based displays
#    Copyright(c) 2019 JD Morise, jdmorise@yahoo.com
#
#    Modified by Donald R. Moran to support command-line arguments and
#    generate a C header file in a specific format.
#
#    This script converts a TTF font file into a C header file containing
#    a bitmap representation of the font's characters. By default, it
#    extracts characters from code page 437, as used by VGA.
#    It also generates a preview image of the font.
#
#    Usage:
#        python ttf2bmh.py -f <font_file> -s <font_size> [--all-glyphs]
#
#    Arguments:
#        -f, --font: Path to the TTF font file.
#        -s, --size: Font size (default: 16).
#        --all-glyphs: Extract all glyphs from the font instead of just
#                      code page 437.
#     Note: --all-glyphs creates an array indexed from 0 to the number 
#     of glyphs, and that for full Unicode support, the C code would 
#     need to be adapted to handle a sparse data structure.
#-------------------------------------------------------------------------
#
#    (C) 2019, jdmorise@yahoo.com
#
#    This software is part of the TTF2BMH software package to generate bitmap
#    header files for usage of simple OLED or LCD displays with microprocessors
#
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
#
#-------------------------------------------------------------------------

import re
import os
import sys
import subprocess
from shutil import copyfile
from fontTools.ttLib import TTFont
import unicodedata
from PIL import Image, ImageFont, ImageDraw
import argparse

VERSION = '2.1'

def is_printable(char):
    """Exclude control and invisible characters."""
    category = unicodedata.category(char)
    return not category.startswith("C")  # Control, Format, Surrogate, etc.

def get_all_chars(ttf):
    """Extract all characters from font cmap."""
    chars = set()
    for cmap in ttf['cmap'].tables:
        if cmap.isUnicode():
            chars.update(chr(cp) for cp in cmap.cmap.keys())
    return sorted(chars)

def render_char(char, font, size, yoffset=0):
    """Render a character and return its bitmap image."""
    image = Image.new('1', size, color=255)
    draw = ImageDraw.Draw(image)
    draw.text((0, -yoffset), char, font=font)
    return image

def write_header(chars, font, size, yoffset=0, header_file="font.h"):
    with open(header_file, "w") as f:
        f.write("#ifndef FONT_8X16_H\n")
        f.write("#define FONT_8X16_H\n\n")
        f.write(f"// Font: PxPlus IBM VGA 8x16\n")
        f.write(f"// Total characters: {len(chars)}\n\n")
        f.write(f"static const uint8_t font_8x16[256][16] = {{\n")
        for idx, char in enumerate(chars):
            image = render_char(char, font, size, yoffset)
            pixels = list(image.getdata())
            bytestr = []
            for y in range(size[1]):
                byte = 0
                for x in range(size[0]):
                    if pixels[y * size[0] + x] == 0:
                        byte |= (1 << (7 - x))
                bytestr.append(f"0x{byte:02X}")
            byte_line = ", ".join(bytestr)
            f.write(f"  /* {idx:3} */ {{ {byte_line} }}, // Index {idx}: '{repr(char)}'\n")
        f.write("};\n")
        f.write("#endif\n")

def create_preview(chars, font, size, yoffset=0, preview_image="preview.png"):
    from PIL import ImageFont, ImageDraw, Image

    printable_chars = [(idx, char) for idx, char in enumerate(chars) if is_printable(char)]

    box_padding = 2
    label_height = 12
    spacing = 4
    columns = 30
    label_font = ImageFont.load_default()

    max_label = str(len(chars) - 1)
    max_label_bbox = label_font.getbbox(max_label)
    max_label_width = max_label_bbox[2] - max_label_bbox[0]

    char_width = max(size[0], max_label_width) + box_padding * 2
    char_height = size[1] + label_height + box_padding * 3

    rows = (len(printable_chars) + columns - 1) // columns
    canvas_width = columns * (char_width + spacing)
    canvas_height = rows * (char_height + spacing)

    preview = Image.new('RGB', (canvas_width, canvas_height), color=(255, 255, 255))
    draw = ImageDraw.Draw(preview)

    for i, (idx, char) in enumerate(printable_chars):
        img = render_char(char, font, size, yoffset)

        label = str(idx)
        col = i % columns
        row = i // columns

        x = col * (char_width + spacing)
        y = row * (char_height + spacing)

        draw.rectangle([x, y, x + char_width, y + size[1] + box_padding * 2], outline=(0, 0, 0))

        char_x = x + (char_width - size[0]) // 2
        char_y = y + box_padding
        preview.paste(img.convert('RGB'), (char_x, char_y))

        label_bbox = label_font.getbbox(label)
        label_width = label_bbox[2] - label_bbox[0]
        label_x = x + (char_width - label_width) // 2
        label_y = y + size[1] + box_padding * 2
        draw.text((label_x, label_y), label, font=label_font, fill=(0, 0, 0))

    preview.save(preview_image)
    print(f"✅ Indexed preview saved to {preview_image}")

def main():
    parser = argparse.ArgumentParser(description='TTF to C header converter.')
    parser.add_argument('-f', '--font', dest='font_path', required=True, help='Path to the TTF font file.')
    parser.add_argument('-s', '--size', dest='font_size', type=int, default=16, help='Font size.')
    parser.add_argument('--all-glyphs', action='store_true', help='Extract all glyphs from the font. Default is to use code page 437.')
    args = parser.parse_args()

    font_path = args.font_path
    font_size = args.font_size
    
    font_dir, font_filename = os.path.split(font_path)
    font_name, _ = os.path.splitext(font_filename)
    header_file = f"{font_name}.h"
    preview_image = f"{font_name}_preview.png"

    print(f"Loading font: {font_path}")
    tt = TTFont(font_path)
    
    if args.all_glyphs:
        print("Extracting all glyphs from font...")
        chars = get_all_chars(tt)
    else:
        print("Using code page 437...")
        chars = bytes(range(256)).decode('cp437')


    print(f"Total characters found: {len(chars)}")

    PILfont = ImageFont.truetype(font_path, font_size)
    size = (8, 16)
    yoffset = 0

    print("Writing header file...")
    write_header(chars, PILfont, size, yoffset, header_file)
    print(f"{header_file} written")

    print("Creating preview...")
    create_preview(chars, PILfont, size, yoffset, preview_image)

    print("TTF2BMH Finished")

if (__name__ == '__main__'):
    main()
