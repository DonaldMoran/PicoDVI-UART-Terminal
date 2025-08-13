#!/bin/bash

echo "Creating virtual environment..."
python3 -m venv venv

echo "Activating environment..."
source venv/bin/activate

echo "Installing Pillow and freetype-py..."
pip install pillow freetype-py

echo "Installing matplotlib..."
pip install matplotlib

echo "Running main.py..."
python main.py
