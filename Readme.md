PicoDVI-UART-Terminal Preview
======================

## PicoDVI-UART-Terminal

This project is a specialized fork of the original [PicoDVI](https://github.com/Wren6991/PicoDVI) project, focusing on creating a DVI terminal emulator for the Raspberry Pi Pico RP2350. It leverages the powerful DVI output capabilities to provide a text-based console with UART input.

Key features of this terminal emulator include:
*   **80x30 character display** (640x480 resolution).
*   Support for **ANSI escape sequences** for text formatting and cursor control.
*   **UART interface for keyboard input**, enabling a simple serial console experience.
*   Multiple configurable cursor styles and 6-bit RGB color themes.
*   Double-buffered rendering for smooth display.
*   Interactive color selection menus (Ctrl+F, Ctrl+B) and theme presets (Ctrl+T).
*   Support for Microsoft BASIC input via UART.

This project aims to provide a ready-to-use terminal application built upon the robust PicoDVI library, making it easy to integrate a visual console into your RP2350 projects.

# PICO_PLATFORM can also be rp2350-riscv

Build instructions:

```bash
cd PicoDVI-pico2_UART/software
mkdir build
cd build
cmake ..
make -j$(nproc)

# Then flash a binary, e.g.:
cd software/build/apps/my_terminal
picotool load my_terminal.uf2 -F
picotool reboot
```

