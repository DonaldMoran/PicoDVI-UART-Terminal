# PicoDVI-UART-Terminal

This project is a specialized DVI terminal emulator for the Raspberry Pi Pico RP2350, building upon the original [PicoDVI](https://github.com/Wren6991/PicoDVI) project. It provides a robust text-based console experience leveraging the Pico's DVI output capabilities and a UART interface for input.

**Author:** Donald R. Moran
**License:** MIT

## Features

*   **Display:** 80x30 character display (640x480 resolution) with double-buffered, VSYNC-synchronized rendering to prevent tearing.
*   **ANSI Support:** Comprehensive support for ANSI escape sequences, including text formatting, cursor positioning (e.g., `ESC[<row>;<col>H`), movement (`ESC[A`, `ESC[B`, `ESC[C`, `ESC[D`), screen/line erasing (`ESC[2J`, `ESC[K`), and saving/restoring cursor position (`ESC[s`, `ESC[u`). Also supports blinking text (`ESC[5m` to enable, `ESC[25m` to disable).
*   **Input:** UART interface for keyboard input, enabling a simple serial console experience. Supports Microsoft BASIC input.
*   **Color System:** Utilizes a 6-bit RGB color system (64 colors total, 2 bits per component). Colors can be set via ANSI escape sequences, interactive menus (Ctrl+F for foreground, Ctrl+B for background), or predefined theme presets (Ctrl+T).
*   **User Experience:** Features multiple configurable cursor styles (IBM retro, underline, bar, Apple I, shaded block, arrow), interactive color selection menus, and vertical scrolling when new lines exceed screen height. Terminal state is preserved during menu operations.
*   **System Stability:** Includes a watchdog timer for enhanced system stability.
*   **Performance:** Leverages multicore processing (Core0 for logic, Core1 for DVI rendering) for efficient operation.

## Hardware Requirements

*   Raspberry Pi Pico RP2350
*   DVI output board (e.g., Adafruit HDMI sock)
*   UART connection for keyboard input (RX: GPIO1)

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

