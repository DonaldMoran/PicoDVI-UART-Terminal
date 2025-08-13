#!/bin/bash

echo "Creating virtual environment..."
python3 -m venv venv

echo "Activating environment..."
source venv/bin/activate

echo "Installing requirements"
pip install pillow
pip install fontTools
pip install freetype-py

echo "Creating header for Px437_IBM_VGA_8x16"
rm -fR Px437_IBM_VGA_8x16
mkdir Px437_IBM_VGA_8x16
python ttf2bmh.py -s 16 -f "packs/olschool/ttf - Px (pixel outline)/Px437_IBM_VGA_8x16.ttf"
mv *.h *.png Px437_IBM_VGA_8x16/

echo "Creating header for Px437_TridentEarly_8x16"
rm -fR Px437_TridentEarly_8x16
mkdir Px437_TridentEarly_8x16
python ttf2bmh.py -s 16 -f "packs/olschool/ttf - Px (pixel outline)/Px437_TridentEarly_8x16.ttf"
mv *.h *.png Px437_TridentEarly_8x16/

echo "Creating header for Tamzen8x16r"
rm -fR Tamzen8x16r
mkdir Tamzen8x16r
python ttf2bmh.py -s 16 -f "packs/tamzen-font/ttf/Tamzen8x16r.ttf"
mv *.h *.png Tamzen8x16r/

echo "Creating header for Tamzen8x16b"
rm -fR Tamzen8x16b
mkdir Tamzen8x16b
python ttf2bmh.py -s 16 -f "packs/tamzen-font/ttf/Tamzen8x16b.ttf"
mv *.h *.png Tamzen8x16b/






