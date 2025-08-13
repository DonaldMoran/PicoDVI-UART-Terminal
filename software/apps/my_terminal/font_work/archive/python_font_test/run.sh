#!/bin/bash

echo "Creating virtual environment..."
python3 -m venv venv

echo "Activating environment..."
source venv/bin/activate

echo "Installing Pillow..."
pip install pillow freetype-py

echo "Running main.py..."
python main.py
