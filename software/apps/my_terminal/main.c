/*
===============================================================================
DVI Terminal Emulator for Raspberry Pi Pico RP2350
===============================================================================
Description:
  Terminal emulator for Raspberry Pi Pico RP2350 with DVI output, featuring:
  - 80x30 character display (640x480 resolution)
  - ANSI escape sequence support
  - UART interface for input
  - Multiple cursor styles and color themes
  - Double-buffered rendering to prevent tearing
  - Interactive color selection menus for both foreground (Ctrl+F) and background (Ctrl+B) colors, allowing users to pick any of the 64 6-bit RGB colors by entering a two-digit code.

Hardware Requirements:
  - Raspberry Pi Pico RP2350
  - DVI output board (e.g., Adafruit HDMI sock)
  - UART connection for keyboard input (RX: GPIO1)

Key Features:
  - Support for Microsoft BASIC input via UART
  - Configurable cursor styles (IBM retro, underline, bar, Apple I)
  - 6-bit RGB color support (64 colors) with 2 bits per component (RRGGBB)
  - VSYNC-synchronized rendering
  - Terminal state preservation during menu operations
  - Interactive color selection menus for both foreground and background colors (Ctrl+F, Ctrl+B)

Color System:
  The terminal uses 6-bit RGB colors (2 bits per component) for a total of 64 colors.
  Colors can be set via:
  1. ANSI escape sequences (e.g., \x1B[31m for red text)
  2. Control sequences (menu-based):
     - Ctrl+F: Open foreground color menu (choose by code)
     - Ctrl+B: Open background color menu (choose by code)
  3. Theme presets (Ctrl+T then 1-9)

  When Ctrl+F or Ctrl+B is pressed, a color menu appears, showing all 64 color codes and samples. The user enters a two-digit code (00–63) to select the desired color, or presses ESC to cancel.

  Standard Color Codes (use after Ctrl+F/Ctrl+B):
    k: Black    (0x00)   r: Red      (0x30)   g: Green    (0x0C)
    y: Yellow   (0x3C)   b: Blue     (0x03)   m: Magenta  (0x33)
    c: Cyan     (0x0F)   w: White    (0x3F)
    d: Dark Gray (0x15)  l: Light Gray (0x2A)

  Bright Variants (use uppercase after Ctrl+F/Ctrl+B):
    B: Bright Blue (0x03) R: Bright Red (0x30) G: Bright Green (0x0C)
    Y: Bright Yellow (0x3C) M: Bright Magenta (0x33) C: Bright Cyan (0x0F)


  Theme Presets (Ctrl+T then number):
    0: Green on Black      (VT100/Apple IIe)
    1: Amber on Black      (Wyse/VT220)
    2: White on Blue       (DOS/PC BIOS)
    3: Black on White      (Mac Classic/Light mode)
    4: Light Blue on Blue  (Commodore 64)
    5: Yellow on Blue      (Turbo Pascal / DOS IDEs)
    6: Magenta on Black    (ZX Spectrum/CPM)
    7: Light Gray on Black (DOS/Windows text mode)
    8: Cyan on Black       (Retro secondary text)
    9: Red on Dark Gray    (Mainframe/Alert)

Recent Changes:
  - Replaced I2C with UART for input.
  - Added interactive foreground color selection menu (Ctrl+F) matching the background color menu (Ctrl+B).
  - Fixed compile errors in cursor blink switch statement
  - Retained \r handling with new_line() for BASIC compatibility
  - Fixed cursor blinking with counter-based timing and forced swaps
  - Eliminated remaining screen tearing with stricter VSYNC sync
  - Enhanced debug output for cursor, swap timing, and UART
  - Fixed printf format (%u to %lu for cursor_blink_counter) for debug
  - Ensured cursor background always matches screen background (current_bg)
  - Updated color mappings for accurate 6-bit RGB values

How UART Reception Works

   1. Interrupt Setup: The code configures an interrupt to trigger whenever the UART hardware's receive buffer is not empty. It
      registers the function on_uart_rx to run when this interrupt occurs.
   2. ISR (Interrupt Service Routine): When data arrives on the UART bus, the on_uart_rx function is automatically
      executed. This function reads the byte from the UART hardware and stores it in a circular software buffer called
      uart_buffer.
   3. Main Loop Processing: The main while(1) loop of the program continuously calls the process_uart_buffer() function.
      This function checks if there is any unread data in the uart_buffer (by comparing its head and tail pointers). If
      there is, it processes the character.

License: MIT
Author: Donald R. Moran
===============================================================================
*/
//#define DEBUG 0
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include "pico/stdlib.h" 
#include "pico/multicore.h" 
#include "hardware/clocks.h" 
#include "hardware/gpio.h" 
#include "hardware/vreg.h" 
#include "hardware/watchdog.h" 
#include "hardware/structs/bus_ctrl.h" 
#include "hardware/dma.h" 
#include "hardware/uart.h"
#include "dvi.h" 
#include "dvi_serialiser.h" 
#ifndef DVI_DEFAULT_SERIAL_CONFIG
#define DVI_DEFAULT_SERIAL_CONFIG adafruit_hdmi_sock_cfg
#endif
#include "common_dvi_pin_configs.h" 
#include "tmds_encode_font_2bpp.h" 
//#include "font_8x16.h" 
#include "Px437_IBM_VGA_8x16.h" 

// === Configuration ===
#define FONT_CHAR_WIDTH 8
#define FONT_CHAR_HEIGHT 16
#define FONT_N_CHARS 256

#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480
#define VREG_VSEL VREG_VOLTAGE_1_20
#define DVI_TIMING dvi_timing_640x480p_60hz

#define CHAR_COLS (FRAME_WIDTH / FONT_CHAR_WIDTH)
#define CHAR_ROWS (FRAME_HEIGHT / FONT_CHAR_HEIGHT)
#define COLOUR_PLANE_SIZE_WORDS (CHAR_ROWS * CHAR_COLS * 4 / 32)
#define COLOUR_PAD_WORDS 8

// Input buffering
#define UART_BUFFER_SIZE 512

// Cursor blink configuration
#define CURSOR_BLINK_MS 500 // Blink interval in milliseconds
#define MAIN_LOOP_MIN_MS 10 // Minimum loop frequency to ensure responsiveness

// === Global State ===
struct dvi_inst dvi0;
static uint8_t font_scanline[FONT_N_CHARS * FONT_CHAR_HEIGHT];

// Double buffering
__attribute__((aligned(4))) static char charbuf_front[CHAR_ROWS * CHAR_COLS];
__attribute__((aligned(4))) static char charbuf_back[CHAR_ROWS * CHAR_COLS];

__attribute__((aligned(4))) static uint32_t colourbuf_front[3 * COLOUR_PLANE_SIZE_WORDS + COLOUR_PAD_WORDS];
__attribute__((aligned(4))) static uint32_t colourbuf_back[3 * COLOUR_PLANE_SIZE_WORDS + COLOUR_PAD_WORDS];

static volatile bool buffer_lock = false;
volatile bool swap_pending = false;
volatile bool scroll_settled = true;
volatile bool safe_to_scroll = false;
volatile bool swap_queued = false; // Track if a swap is already queued

// Input buffers
static volatile uint8_t uart_buffer[UART_BUFFER_SIZE];
static volatile uint16_t uart_head = 0;
static volatile uint16_t uart_tail = 0;
static volatile bool uart_overflow = false;

// Terminal state
typedef struct {
    uint16_t cursor_x;
    uint16_t cursor_y;
    bool cursor_visible;
    bool escape_mode;
    bool ansi_mode;
    bool skip_next_lf;
    bool skip_next_cr;
    bool suppress_next_cr;
} terminal_state_t;

terminal_state_t term;

// === Global Additions ===
volatile bool input_active = false;
absolute_time_t last_input_time;

// Cursor and rendering state
bool cursor_drawn = false;
int saved_cursor_x = -1;
int saved_cursor_y = -1;
int cursor_draw_x = -1;
int cursor_draw_y = -1;
char saved_cursor_char = ' ';
uint8_t saved_cursor_fg = 0;
volatile absolute_time_t led_off_time;
volatile uint32_t cursor_blink_counter = 0; // Counter for blink timing
volatile bool buffer_dirty = false;
static char deferred_char;
static bool deferred_pending = false;

// ANSI parsingUART_ID
#define ANSI_PARAM_MAX 4
uint8_t ansi_params[ANSI_PARAM_MAX];
uint8_t ansi_param_count = 0;
uint8_t ansi_param_index = 0;
char ansi_buffer[16];
uint8_t ansi_buf_len = 0;
char ansi_final_char = '\0';

// Theme and cursor
enum cursor_style { CURSOR__SOLID_BLOCK, CURSOR_UNDERLINE, CURSOR_BAR, CURSOR_APPLE_I, CURSOR_SHADED_BLOCK, CURSOR__SOLID_ARROW };
enum cursor_style current_cursor = CURSOR_APPLE_I;
uint8_t current_fg = 12;
uint8_t current_bg = 0;

// Menu system
#define MENU_BUFFER_WIDTH 34
#define MENU_BUFFER_HEIGHT 12  // Increased for color menu
char saved_chars[MENU_BUFFER_HEIGHT][MENU_BUFFER_WIDTH];
uint8_t saved_fg[MENU_BUFFER_HEIGHT][MENU_BUFFER_WIDTH];
uint8_t saved_bg[MENU_BUFFER_HEIGHT][MENU_BUFFER_WIDTH];
uint16_t menu_left = 0;
uint16_t menu_top = 0;
volatile bool theme_select_mode = false;
volatile bool cursor_menu_mode = false;
volatile bool bg_color_menu_mode = false;
volatile bool fg_color_menu_mode = false;
char color_menu_buf[3] = {0};
uint8_t color_menu_buf_len = 0;

// Function Prototypes
uint8_t reverse_byte(uint8_t b);

// Hardware config
#define UART_ID uart0
#define BAUD_RATE 115200
#define UART_RX_PIN 1

#define LED_PIN 25

// Removed awaiting_fg_code and awaiting_bg_code - now using menu system

// === Utilities ===
uint8_t reverse_byte(uint8_t b) {
    b = (b >> 4) | (b << 4);
    b = ((b & 0xCC) >> 2) | ((b & 0x33) << 2);
    b = ((b & 0xAA) >> 1) | ((b & 0x55) << 1);
    return b;
}

void reset_ansi_state(void) {
    ansi_param_count = 0;
    ansi_param_index = 0;
    ansi_buf_len = 0;
    ansi_final_char = '\0';
}

// === Buffering System ===
void request_swap(void) {
    if (!swap_queued) { // Prevent redundant swap requests
        swap_pending = true;
        swap_queued = true;
        #ifdef DEBUG
        printf("Swap requested at time=%lld\n", get_absolute_time());
        #endif
    }
}

void perform_swap(void) {
    while (__atomic_test_and_set(&buffer_lock, __ATOMIC_ACQUIRE)) {
        __wfe();
    }
    
    memcpy(charbuf_front, charbuf_back, sizeof(charbuf_back));
    memcpy(colourbuf_front, colourbuf_back, sizeof(colourbuf_back));
    
    __atomic_clear(&buffer_lock, __ATOMIC_RELEASE);
    swap_pending = false;
    swap_queued = false;
    scroll_settled = true;
    safe_to_scroll = true;
    
    #ifdef DEBUG
    printf("Buffer swapped at time=%lld\n", get_absolute_time());
    #endif
}

void safe_request_swap(void) {
    if (buffer_dirty || cursor_drawn || term.cursor_visible) {
        request_swap();
        perform_swap(); // Forced immediate swap
        buffer_dirty = false;
    }
}

void set_char(uint x, uint y, uint8_t c) {
    if (x < CHAR_COLS && y < CHAR_ROWS) {
        charbuf_back[x + y * CHAR_COLS] = c;
    }
}

void set_colour(uint x, uint y, uint8_t fg, uint8_t bg) {
    if (x >= CHAR_COLS || y >= CHAR_ROWS) return;
    
    uint idx = x + y * CHAR_COLS;
    uint bit = (idx % 8) * 4;
    uint word = idx / 8;

    #ifdef DEBUG
    printf("set_colour: x=%u, y=%u, fg=0x%02X, bg=0x%02X, idx=%u, bit=%u, word=%u\n", 
           x, y, fg, bg, idx, bit, word);
    #endif
    
    for (int p = 0; p < 3; ++p) {
        uint32_t val = (fg & 0x3) | ((bg << 2) & 0xC);
        colourbuf_back[word + p * COLOUR_PLANE_SIZE_WORDS] =
            (colourbuf_back[word + p * COLOUR_PLANE_SIZE_WORDS] & ~(0xFu << bit)) | (val << bit);
        fg >>= 2;
        bg >>= 2;
    }
}

// === Terminal Operations ===
void clear_screen(void) {
    while (__atomic_test_and_set(&buffer_lock, __ATOMIC_ACQUIRE)) {
        __wfe();
    }
    
    memset(colourbuf_back, 0, sizeof(colourbuf_back));
    
    for (uint y = 0; y < CHAR_ROWS; y++) {
        for (uint x = 0; x < CHAR_COLS; x++) {
            charbuf_back[x + y * CHAR_COLS] = ' ';
            set_colour(x, y, current_fg, current_bg);
        }
    }
    
    term.cursor_x = 0;
    term.cursor_y = 0;
    __atomic_clear(&buffer_lock, __ATOMIC_RELEASE);
    request_swap();
}

void scroll_up(void) {
    while (__atomic_test_and_set(&buffer_lock, __ATOMIC_ACQUIRE)) {
        __wfe();
    }
    
    // Shift character buffer
    memmove(&charbuf_back[0], 
            &charbuf_back[CHAR_COLS], 
            (CHAR_ROWS - 1) * CHAR_COLS);
    
    // Clear the last row of characters
    for (uint x = 0; x < CHAR_COLS; x++) {
        charbuf_back[x + (CHAR_ROWS - 1) * CHAR_COLS] = ' ';
    }
    
    // Shift color buffer for all planes
    for (int p = 0; p < 3; p++) {
        uint32_t *base = &colourbuf_back[p * COLOUR_PLANE_SIZE_WORDS];
        memmove(base, 
                &base[CHAR_COLS / 8], 
                (CHAR_ROWS - 1) * (CHAR_COLS / 8) * sizeof(uint32_t));
    }
    
    // Clear the last row's color data before setting new color
    for (uint x = 0; x < CHAR_COLS; x++) {
        uint idx = x + (CHAR_ROWS - 1) * CHAR_COLS;
        uint bit = (idx % 8) * 4;
        uint word = idx / 8;
        for (int p = 0; p < 3; p++) {
            colourbuf_back[word + p * COLOUR_PLANE_SIZE_WORDS] &= ~(0xFu << bit); // Clear 4 bits
        }
    }
    
    // Set the last row with current colors
    for (uint x = 0; x < CHAR_COLS; x++) {
        set_colour(x, CHAR_ROWS - 1, current_fg, current_bg);
    }
    
    __atomic_clear(&buffer_lock, __ATOMIC_RELEASE);
    buffer_dirty = true;
    safe_request_swap();
}

void new_line(void) {
    term.cursor_x = 0;
    
    if (++term.cursor_y >= CHAR_ROWS) {
        term.cursor_y = CHAR_ROWS - 1;
        scroll_up();
    }
    buffer_dirty = true;
    safe_request_swap(); // Ensure swap after new line
}

// === ANSI Processing ===
// Map ANSI color codes to 6-bit RGB values
// (2 bits per component: R, G, B)
void process_ansi_code(uint8_t param) {
    if (param == 0) {
        // Reset: white on black
        current_fg = 63;  // 0b111111
        current_bg = 0;   // 0b000000
    } else if (param >= 30 && param <= 37) {
        // Standard ANSI foreground colors
        static const uint8_t ansi_colors[] = {
            0,   // 30: black   (0b000000)
            48,  // 31: red     (0b110000)
            12,  // 32: green   (0b001100)
            60,  // 33: yellow  (0b111100)
            3,   // 34: blue    (0b000011)
            51,  // 35: magenta (0b110011)
            15,  // 36: cyan    (0b001111)
            63   // 37: white   (0b111111)
        };
        current_fg = ansi_colors[param - 30];
    } else if (param >= 40 && param <= 47) {
        // Standard ANSI background colors
        static const uint8_t ansi_colors[] = {
            0,   // 40: black
            48,  // 41: red
            12,  // 42: green
            60,  // 43: yellow
            3,   // 44: blue
            51,  // 45: magenta
            15,  // 46: cyan
            63   // 47: white
        };
        current_bg = ansi_colors[param - 40];
    }
}

void process_ansi_sequence(uint8_t *params, uint8_t count, char final) {
    switch (final) {
    case 'J':
        if (count == 1 && params[0] == 2) {
            clear_screen();
        }
        break;
        
    case 'K':
        for (uint x = term.cursor_x; x < CHAR_COLS; x++) {
            set_char(x, term.cursor_y, ' ');
            set_colour(x, term.cursor_y, current_fg, current_bg);
        }
        buffer_dirty = true;
        break;
        
    case 'H':
        if (count >= 1) {
            term.cursor_y = (params[0] > 0 ? params[0] - 1 : 0);
        }
        if (count >= 2) {
            term.cursor_x = (params[1] > 0 ? params[1] - 1 : 0);
        }
        break;
        
    case 'm':
        for (uint8_t i = 0; i < count; i++) {
            process_ansi_code(params[i]);
        }
        break;
        
    case 's':
        saved_cursor_x = term.cursor_x;
        saved_cursor_y = term.cursor_y;
        break;
        
    case 'u':
        if (saved_cursor_x != -1 && saved_cursor_y != -1) {
            term.cursor_x = saved_cursor_x;
            term.cursor_y = saved_cursor_y;
        }
        break;
        
        case 'A': { // Cursor Up
            uint8_t n = (count >= 1 && params[0] > 0) ? params[0] : 1;
            if (term.cursor_y >= n)
                term.cursor_y -= n;
            else
                term.cursor_y = 0;
            break;
        }
        case 'B': { // Cursor Down
            uint8_t n = (count >= 1 && params[0] > 0) ? params[0] : 1;
            if (term.cursor_y + n < CHAR_ROWS)
                term.cursor_y += n;
            else
                term.cursor_y = CHAR_ROWS - 1;
            break;
        }
        case 'C': { // Cursor Forward
            uint8_t n = (count >= 1 && params[0] > 0) ? params[0] : 1;
            if (term.cursor_x + n < CHAR_COLS)
                term.cursor_x += n;
            else
                term.cursor_x = CHAR_COLS - 1;
            break;
        }
        case 'D': { // Cursor Back
            uint8_t n = (count >= 1 && params[0] > 0) ? params[0] : 1;
            if (term.cursor_x >= n)
                term.cursor_x -= n;
            else
                term.cursor_x = 0;
            break;
        }
    }
}

// === Color Menu ===
void draw_color_menu(const char *title, const char *prompt) {
    uint16_t x = 2;
    uint16_t y;
    
    if (term.cursor_y + 12 < CHAR_ROWS) {
        y = term.cursor_y + 1;
    } else {
        y = CHAR_ROWS - 12;
    }
    
    menu_left = x - 1;
    menu_top = y - 1;
    
    // Save current screen region
    for (uint8_t row = 0; row < 12; row++) {
        for (uint8_t col = 0; col < 34; col++) {
            uint px = menu_left + col;
            uint py = menu_top + row;
            
            if (px < CHAR_COLS && py < CHAR_ROWS) {
                saved_chars[row][col] = charbuf_back[px + py * CHAR_COLS];
                
                uint idx = px + py * CHAR_COLS;
                uint bit = (idx % 8) * 4;
                uint word = idx / 8;
                
                uint8_t fg = 0, bg = 0;
                for (int p = 2; p >= 0; --p) {
                    uint32_t val = colourbuf_back[word + p * COLOUR_PLANE_SIZE_WORDS];
                    uint8_t nibble = (val >> bit) & 0xF;
                    fg = (fg << 2) | (nibble & 0x3);
                    bg = (bg << 2) | ((nibble >> 2) & 0x3);
                }
                
                saved_fg[row][col] = fg;
                saved_bg[row][col] = bg;
            }
        }
    }
    
    // Draw menu border
    set_char(menu_left, menu_top, '+');
    for (uint8_t i = 0; i < 32; i++) {
        set_char(x + i, menu_top, '-');
    }
    set_char(menu_left + 32, menu_top, '+');
    
    for (uint8_t i = 0; i < 10; i++) {
        set_char(menu_left, y + i, '|');
        set_char(menu_left + 32, y + i, '|');
    }
    
    set_char(menu_left, menu_top + 10, '+');
    for (uint8_t i = 0; i < 32; i++) {
        set_char(x + i, menu_top + 10, '-');
    }
    set_char(menu_left + 32, menu_top + 10, '+');
    
    // Draw title
    for (size_t i = 0; i < strlen(title); i++) {
        set_char(x + i, y, title[i]);
        set_colour(x + i, y, current_fg, current_bg);
    }
    
    // Draw color grid with two-digit numbers
    for (uint8_t row = 0; row < 8; row++) {
        for (uint8_t col = 0; col < 8; col++) {
            uint8_t color_idx = row * 8 + col;
            uint16_t pos_x = x + col * 4;
            uint16_t pos_y = y + row + 1;
            
            // Draw two-digit color number with leading zero
            char num[4];
            snprintf(num, sizeof(num), "%02d", color_idx);
            set_char(pos_x, pos_y, num[0]);
            set_char(pos_x+1, pos_y, num[1]);
            set_colour(pos_x, pos_y, 63, color_idx);
            set_colour(pos_x+1, pos_y, 63, color_idx);
            
            // Draw color sample
            set_char(pos_x+2, pos_y, 0xDB); // Block character
            set_colour(pos_x+2, pos_y, 63, color_idx);
        }
    }
    
    // Draw input prompt
    for (size_t i = 0; i < strlen(prompt); i++) {
        set_char(x + i, y + 9, prompt[i]);
        set_colour(x + i, y + 9, current_fg, current_bg);
    }
    
    buffer_dirty = true;
    safe_request_swap();
}

// === Menu System ===
void restore_menu_region(void) {
    for (uint8_t row = 0; row < MENU_BUFFER_HEIGHT; row++) {
        for (uint8_t col = 0; col < MENU_BUFFER_WIDTH; col++) {
            uint px = menu_left + col;
            uint py = menu_top + row;
            if (px < CHAR_COLS && py < CHAR_ROWS) {
                set_char(px, py, saved_chars[row][col]);
                set_colour(px, py, saved_fg[row][col], saved_bg[row][col]);
            }
        }
    }
    buffer_dirty = true;
    safe_request_swap();
}

void draw_cursor_menu(void) {
    uint16_t x = 2;
    uint16_t y;
    
    if (term.cursor_y + MENU_BUFFER_HEIGHT + 1 < CHAR_ROWS) {
        y = term.cursor_y + 1;
    } else {
        y = CHAR_ROWS - MENU_BUFFER_HEIGHT - 1;
    }
    
    const char *lines[] = {
        "Cursor Style Menu:", 
        "[1] Block        \xDB",
        "[2] Underline    _",
        "[3] Bar          |",          
        "[4] Apple I      @",
        "[5] Shaded Block \xB2",  
        "[6] Arrow        >",
        "Select style: "
    };
    
    size_t num_lines = sizeof(lines) / sizeof(lines[0]);
    uint8_t box_width = 32;
    uint8_t box_height = num_lines + 2;
    menu_left = x - 1;
    menu_top = y - 1;
    
    for (uint8_t row = 0; row < MENU_BUFFER_HEIGHT; row++) {
        for (uint8_t col = 0; col < MENU_BUFFER_WIDTH; col++) {
            uint px = menu_left + col;
            uint py = menu_top + row;
            
            if (px < CHAR_COLS && py < CHAR_ROWS) {
                saved_chars[row][col] = charbuf_back[px + py * CHAR_COLS];
                
                uint idx = px + py * CHAR_COLS;
                uint bit = (idx % 8) * 4;
                uint word = idx / 8;
                
                uint8_t fg = 0, bg = 0;
                for (int p = 2; p >= 0; --p) {
                    uint32_t val = colourbuf_back[word + p * COLOUR_PLANE_SIZE_WORDS];
                    uint8_t nibble = (val >> bit) & 0xF;
                    fg = (fg << 2) | (nibble & 0x3);
                    bg = (bg << 2) | ((nibble >> 2) & 0x3);
                }
                
                saved_fg[row][col] = fg;
                saved_bg[row][col] = bg;
            }
        }
    }
    
    set_char(menu_left, menu_top, '+');
    for (uint8_t i = 0; i < box_width; i++) {
        set_char(x + i, menu_top, '-');
    }
    set_char(menu_left + box_width, menu_top, '+');
    
    for (uint8_t i = 0; i < box_height - 2; i++) {
        set_char(menu_left, y + i, '|');
        set_char(menu_left + box_width, y + i, '|');
    }
    
    set_char(menu_left, menu_top + box_height - 1, '+');
    for (uint8_t i = 0; i < box_width; i++) {
        set_char(x + i, menu_top + box_height - 1, '-');
    }
    set_char(menu_left + box_width, menu_top + box_height - 1, '+');
    
    for (size_t i = 0; i < num_lines; i++) {
        size_t len = strlen(lines[i]);
        for (size_t j = 0; j < len; j++) {
            set_char(x + j, y + i, lines[i][j]);
            set_colour(x + j, y + i, current_fg, current_bg);
        }
    }
    
    buffer_dirty = true;
    safe_request_swap();
}

// === Character Handling ===
void handle_char(char c) {
    input_active = true;
    last_input_time = get_absolute_time();

    // Remove cursor if it's drawn
    if (cursor_drawn) {
        set_char(cursor_draw_x, cursor_draw_y, saved_cursor_char);
        set_colour(cursor_draw_x, cursor_draw_y, saved_cursor_fg, current_bg);
        cursor_drawn = false;
        buffer_dirty = true;
    }

    // 1. First handle BASIC echo suppression
    if (term.suppress_next_cr && c == '\r') {
        term.suppress_next_cr = false;
        #ifdef DEBUG
        printf("Suppressed BASIC echo CR\n");
        #endif
        return;
    }
    term.suppress_next_cr = false;  // Reset if not matched
    

    if (term.skip_next_lf && c == '\n') {
        term.skip_next_lf = false;
        return;
    }
    if (term.skip_next_cr && c == '\r') {
        term.skip_next_cr = false;
        return;
    }

    // Reset skip flags if we get any non-matching character
    if (term.skip_next_lf && c != '\n') {
        term.skip_next_lf = false;  // Reset if next char isn't LF
    }
    if (term.skip_next_cr && c != '\r') {
        term.skip_next_cr = false;  // Reset if next char isn't CR
    }
    
    if (term.escape_mode) {
        if (c == '[') {
            term.ansi_mode = true;
            reset_ansi_state();
            return;
        }
        
        if (term.ansi_mode) {
            if (c >= '0' && c <= '9') {
                if (ansi_buf_len < sizeof(ansi_buffer) - 1) {
                    ansi_buffer[ansi_buf_len++] = c;
                }
                return;
            } else if (c == ';') {
                ansi_buffer[ansi_buf_len] = '\0';
                if (ansi_param_count < ANSI_PARAM_MAX) {
                    ansi_params[ansi_param_count++] = atoi(ansi_buffer);
                }
                ansi_buf_len = 0;
                return;
            } else {
                ansi_buffer[ansi_buf_len] = '\0';
                if (ansi_buf_len > 0 && ansi_param_count < ANSI_PARAM_MAX) {
                    ansi_params[ansi_param_count++] = atoi(ansi_buffer);
                }
                process_ansi_sequence(ansi_params, ansi_param_count, c);
                term.escape_mode = false;
                term.ansi_mode = false;
                return;
            }
        }
        
        term.escape_mode = false;
        return;
    }
    
    if (fg_color_menu_mode) {
        if (c >= '0' && c <= '9' && color_menu_buf_len < 2) {
            color_menu_buf[color_menu_buf_len++] = c;
            if (color_menu_buf_len == 2) {
                // Convert to number and set foreground color
                uint8_t color_num = (color_menu_buf[0] - '0') * 10 + (color_menu_buf[1] - '0');
                if (color_num < 64) {
                    current_fg = color_num;
                }
                restore_menu_region();
                fg_color_menu_mode = false;
                color_menu_buf_len = 0;
            }
        } else if (c == '\b' && color_menu_buf_len > 0) {
            // Handle backspace
            color_menu_buf_len--;
        } else if (c == '\x1B') {
            // ESC pressed - cancel menu
            restore_menu_region();
            fg_color_menu_mode = false;
            color_menu_buf_len = 0;
        }
        return;
    }
    if (bg_color_menu_mode) {
        if (c >= '0' && c <= '9' && color_menu_buf_len < 2) {
            color_menu_buf[color_menu_buf_len++] = c;
            if (color_menu_buf_len == 2) {
                // Convert to number and set background color
                uint8_t color_num = (color_menu_buf[0] - '0') * 10 + (color_menu_buf[1] - '0');
                if (color_num < 64) {
                    current_bg = color_num;
                }
                restore_menu_region();
                bg_color_menu_mode = false;
                color_menu_buf_len = 0;
            }
        } else if (c == '\b' && color_menu_buf_len > 0) {
            // Handle backspace
            color_menu_buf_len--;
        } else if (c == '\x1B') {
            // ESC pressed - cancel menu
            restore_menu_region();
            bg_color_menu_mode = false;
            color_menu_buf_len = 0;
        }
        return;
    }
    
    if (cursor_menu_mode) {
        switch (c) {
        case '1': current_cursor = CURSOR__SOLID_BLOCK; break;
        case '2': current_cursor = CURSOR_UNDERLINE; break;
        case '3': current_cursor = CURSOR_BAR; break;
        case '4': current_cursor = CURSOR_APPLE_I; break;
        case '5': current_cursor = CURSOR_SHADED_BLOCK; break;
        case '6': current_cursor = CURSOR__SOLID_ARROW; break; // Default to solid block
        default: return;
        }
        cursor_menu_mode = false;
        restore_menu_region();
        term.cursor_visible = true;
        cursor_blink_counter = 0; // Reset blink counter
        return;
    }
    
    if (theme_select_mode) {
        switch (c) {
        case '0': current_fg = 12; current_bg = 0;  break;  // Green on Black (VT100/Apple IIe)
        case '1': current_fg = 60; current_bg = 0;  break;  // Amber on Black (Wyse/VT220)
        case '2': current_fg = 63; current_bg = 3;  break;  // White on Blue (DOS/PC BIOS)
        case '3': current_fg = 0;  current_bg = 63; break;  // Black on White (Mac Classic/Light mode)
        case '4': current_fg = 11; current_bg = 3;  break;  // Light Blue on Blue (Commodore 64)
        case '5': current_fg = 60; current_bg = 3;  break;  // Yellow on Blue (Turbo Pascal / DOS IDEs)
        case '6': current_fg = 51; current_bg = 0;  break;  // Magenta on Black (ZX Spectrum/CPM)
        case '7': current_fg = 42; current_bg = 0;  break;  // Light Gray on Black (DOS/Windows text mode)
        case '8': current_fg = 15; current_bg = 0;  break;  // Cyan on Black (Retro secondary text)
        case '9': current_fg = 48; current_bg = 21; break;  // Red on Dark Gray (Mainframe/Alert)
        default: return;
        }
        theme_select_mode = false;
        return;
    }
    
    switch (c) {
    case '\x06': // Ctrl+F
        fg_color_menu_mode = true;
        color_menu_buf_len = 0;
        memset(color_menu_buf, 0, sizeof(color_menu_buf));
        draw_color_menu("Foreground Color Menu", "Enter color code (00-63):");
        break;
    case '\x02': // Ctrl+B
        bg_color_menu_mode = true;
        color_menu_buf_len = 0;
        memset(color_menu_buf, 0, sizeof(color_menu_buf));
        draw_color_menu("Background Color Menu", "Enter color code (00-63):");
        break;
    case '\x14': theme_select_mode = true; break;
    case '\x0E': cursor_menu_mode = true; draw_cursor_menu(); break;
    //case '\x07': current_fg = 12; current_bg = 0; break;
    //case '\x17': current_fg = 63; current_bg = 0; break;
    //case '\x03': current_fg = 15; break;
    //case '\x04': current_fg = 4; break;
    //case '\x0F': current_fg = 48; break;
    //case '\x12': current_fg = 48; break;
    //case '\x13': current_fg = 51; break;
    //case '\x19': current_fg = 60; break;
    //case '\x0C': current_fg = 21; break;
    case '\x1B': term.escape_mode = true; break;
    case '\r':
        new_line();
        term.skip_next_lf = true;
        term.suppress_next_cr = true;
        break;
    
    case '\n':
        new_line();
        term.skip_next_cr = true;
        break;
    
    case '\b':
        if (term.cursor_x > 0) {
            term.cursor_x--;
            set_char(term.cursor_x, term.cursor_y, ' ');
            set_colour(term.cursor_x, term.cursor_y, current_fg, current_bg);
            buffer_dirty = true;
        }
        break;
        
    default:
        // Handle normal character input
        set_char(term.cursor_x, term.cursor_y, c);
        set_colour(term.cursor_x, term.cursor_y, current_fg, current_bg);
        term.cursor_x++;
        buffer_dirty = true;
        if (term.cursor_x >= CHAR_COLS) {
            new_line();
        }
        break;
    }
    
    // Force cursor redraw after character processing
    if (term.cursor_visible && !cursor_menu_mode) {
        // Always clear the old cursor position if drawn
        if (cursor_drawn) {
            set_char(cursor_draw_x, cursor_draw_y, saved_cursor_char);
            set_colour(cursor_draw_x, cursor_draw_y, saved_cursor_fg, current_bg);
            #ifdef DEBUG
            uint idx = cursor_draw_x + cursor_draw_y * CHAR_COLS;
            uint bit = (idx % 8) * 4;
            uint word = idx / 8;
            uint8_t check_bg = 0;
            for (int p = 2; p >= 0; --p) {
                uint32_t val = colourbuf_back[word + p * COLOUR_PLANE_SIZE_WORDS];
                uint8_t nibble = (val >> bit) & 0xF;
                check_bg = (check_bg << 2) | ((nibble >> 2) & 0x3);
            }
            printf("Cleared old pos: x=%d, y=%d, intended_bg=0x%02X, actual_bg=0x%02X\n",
                   cursor_draw_x, cursor_draw_y, current_bg, check_bg);
            #endif
        }

        cursor_draw_x = term.cursor_x;
        cursor_draw_y = term.cursor_y;
        saved_cursor_char = charbuf_back[cursor_draw_x + cursor_draw_y * CHAR_COLS];

        // Clear the new area with current background
        set_char(cursor_draw_x, cursor_draw_y, ' ');
        set_colour(cursor_draw_x, cursor_draw_y, 0, current_bg);
        
        uint8_t cursor_char = ' ';
        uint8_t cursor_fg = current_fg;
        uint8_t cursor_bg = current_bg;
        switch (current_cursor) {
            case CURSOR__SOLID_BLOCK: cursor_char = (uint8_t)0xDB; break;
            case CURSOR_APPLE_I: cursor_char = '@'; break;
            case CURSOR_UNDERLINE: cursor_char = '_'; break;
            case CURSOR_BAR: cursor_char = '|'; break;
            case CURSOR_SHADED_BLOCK: cursor_char = (uint8_t)0xB2; break;
            case CURSOR__SOLID_ARROW: cursor_char = '>'; break; // Solid arrow
        }
        set_char(cursor_draw_x, cursor_draw_y, cursor_char);
        set_colour(cursor_draw_x, cursor_draw_y, cursor_fg, cursor_bg);
        cursor_drawn = true;
        buffer_dirty = true;
    }
    
    safe_request_swap();
  
    #ifdef DEBUG
    printf("Char processed: %c (0x%02X), cursor_x=%d, cursor_y=%d\n", 
           (c >= 32 && c < 127) ? c : '.', c, term.cursor_x, term.cursor_y);
    #endif
}

// === Input Handling ===
void on_uart_rx() {
    while (uart_is_readable(UART_ID)) {
        uint8_t ch = uart_getc(UART_ID);
        uint16_t next_head = (uart_head + 1) % UART_BUFFER_SIZE;
        if (next_head == uart_tail) {
            uart_overflow = true;
            #ifdef DEBUG
            printf("UART buffer overflow, head=%d, tail=%d\n", uart_head, uart_tail);
            #endif
            continue;
        }
        uart_buffer[uart_head] = ch;
        uart_head = next_head;
        gpio_put(LED_PIN, 1);
        led_off_time = make_timeout_time_ms(30);
    }
}

void inject_debug_to_uart(const char *msg) {
    for (int i = 0; msg[i]; i++) {
        uart_buffer[uart_head] = msg[i];
        uart_head = (uart_head + 1) % UART_BUFFER_SIZE;
    }
}

void process_uart_buffer(void) {
    while (uart_tail != uart_head) {
        char c = uart_buffer[uart_tail];
        uart_tail = (uart_tail + 1) % UART_BUFFER_SIZE;
        handle_char(c);
        
        if (uart_overflow && 
            ((uart_head - uart_tail + UART_BUFFER_SIZE) % UART_BUFFER_SIZE) > UART_BUFFER_SIZE / 4) {
            uart_overflow = false;
            #ifdef DEBUG
            printf("UART overflow cleared, buffer space: %d\n", 
                   (uart_head - uart_tail + UART_BUFFER_SIZE) % UART_BUFFER_SIZE);
            #endif
        }
    }
}

// === Rendering Core ===
void core1_main(void) {
    dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
    dvi_start(&dvi0);
    
    while (1) {
        watchdog_update();
        
        for (uint y = 0; y < FRAME_HEIGHT; y++) {
            uint32_t *tmdsbuf;
            queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);
            
            // Perform swap only at VSYNC (y == 0) and if pending
            if (y == 0 && swap_pending) {
                perform_swap();
                #ifdef DEBUG
                printf("Swap performed at VSYNC, y=%d\n", y);
                #endif
            }
            
            uint row = y / FONT_CHAR_HEIGHT;
            if (row >= CHAR_ROWS) row = CHAR_ROWS - 1;
            
            uint font_y = y % FONT_CHAR_HEIGHT;
            const uint8_t *scanline = &font_scanline[font_y * FONT_N_CHARS];
            
            for (int plane = 0; plane < 3; plane++) {
                tmds_encode_font_2bpp((const uint8_t *)&charbuf_front[row * CHAR_COLS],
                                      &colourbuf_front[row * (COLOUR_PLANE_SIZE_WORDS / CHAR_ROWS) +
                                                       plane * COLOUR_PLANE_SIZE_WORDS],
                                      tmdsbuf + plane * (FRAME_WIDTH / DVI_SYMBOLS_PER_WORD),
                                      FRAME_WIDTH, scanline);
            }
            
            queue_add_blocking(&dvi0.q_tmds_valid, &tmdsbuf);
        }
    }
}

// === Watchdog Timer ===
void watchdog_reinit(void) {
    watchdog_hw->ctrl = 0;
    watchdog_enable(1000, 1);
    watchdog_update();
}

// === Main Application ===
int main(void) {
    vreg_set_voltage(VREG_VSEL);
    sleep_ms(10);
    set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

    //stdio_init_all();
    stdio_usb_init();
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 1);

    // UART initialization
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_ID, false);
    int UART_IRQ = UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
    irq_set_enabled(UART_IRQ, true);
    uart_set_irq_enables(UART_ID, true, false);

    dvi0.timing = &DVI_TIMING;
    dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
    dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

    for (uint16_t ch = 0; ch < FONT_N_CHARS; ++ch) {
        for (uint8_t row = 0; row < FONT_CHAR_HEIGHT; ++row) {
            uint8_t byte = font_8x16[ch][row];
            font_scanline[row * FONT_N_CHARS + ch] = reverse_byte(byte);
        }
    }

    memset(&term, 0, sizeof(term));
    term.suppress_next_cr = false;
    term.cursor_visible = true;
    term.cursor_x = 0;
    term.cursor_y = 0;
    cursor_draw_x = 0;
    cursor_draw_y = 0;
    saved_cursor_x = 0;
    saved_cursor_y = 0;
    
    clear_screen();
    perform_swap();

    // Set bus priority for core1
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_PROC1_BITS;
    multicore_launch_core1(core1_main);
    
    watchdog_reinit();
    cursor_blink_counter = 0;

    absolute_time_t last_loop_time = get_absolute_time();
    while (1) {
        absolute_time_t now = get_absolute_time();
        watchdog_update();
        
        // Cursor blinking (counter-based)
        if (term.cursor_visible && !cursor_menu_mode) {
            cursor_blink_counter++;
            if (cursor_blink_counter >= (CURSOR_BLINK_MS / MAIN_LOOP_MIN_MS)) {
                cursor_blink_counter = 0;
                if (cursor_drawn) {
                    // Remove cursor by restoring original character and colors
                    set_char(cursor_draw_x, cursor_draw_y, saved_cursor_char);
                    set_colour(cursor_draw_x, cursor_draw_y, saved_cursor_fg, current_bg);
                    cursor_drawn = false;
                } else {
                    // Draw cursor
                    cursor_draw_x = term.cursor_x;
                    cursor_draw_y = term.cursor_y;
                    saved_cursor_char = charbuf_back[cursor_draw_x + cursor_draw_y * CHAR_COLS];
                    
                    uint idx = cursor_draw_x + cursor_draw_y * CHAR_COLS;
                    uint bit = (idx % 8) * 4;
                    uint word = idx / 8;
                    saved_cursor_fg = 0;
                    for (int p = 2; p >= 0; --p) {
                        uint32_t val = colourbuf_back[word + p * COLOUR_PLANE_SIZE_WORDS];
                        uint8_t nibble = (val >> bit) & 0xF;
                        saved_cursor_fg = (saved_cursor_fg << 2) | (nibble & 0x3);
                    }
                    
                    // Clear the area with current background color before drawing
                    set_char(cursor_draw_x, cursor_draw_y, ' ');
                    set_colour(cursor_draw_x, cursor_draw_y, 0, current_bg);
                    
                    uint8_t cursor_char = ' ';
                    uint8_t cursor_fg = current_fg;
                    uint8_t cursor_bg = current_bg;
                    switch (current_cursor) {
                        case CURSOR__SOLID_BLOCK: cursor_char = (uint8_t)0xDB; break;
                        case CURSOR_APPLE_I: cursor_char = '@'; break;
                        case CURSOR_UNDERLINE: cursor_char = '_'; break;
                        case CURSOR_BAR: cursor_char = '|'; break;
                        case CURSOR_SHADED_BLOCK: cursor_char = (uint8_t)0xB2; break;
                        case CURSOR__SOLID_ARROW: cursor_char = '>'; break;
                    }
                    set_char(cursor_draw_x, cursor_draw_y, cursor_char);
                    set_colour(cursor_draw_x, cursor_draw_y, cursor_fg, cursor_bg);
                    cursor_drawn = true;
                }
                buffer_dirty = true;
                safe_request_swap();
                
                #ifdef DEBUG
                static int cursor_debug_count = 0;
                if (cursor_debug_count++ > 100) {
                    cursor_debug_count = 0;
                    printf("Cursor: x=%d, y=%d, visible=%d, drawn=%d, fg=%d, bg=%d\n",
                           term.cursor_x, term.cursor_y, term.cursor_visible, 
                           cursor_drawn, current_fg, current_bg);
                }
                #endif
            }
        }
        
        // Process UART input
        process_uart_buffer();
        
        if (absolute_time_diff_us(last_loop_time, now) > 100000) {
            input_active = false;
        }
        
        if (deferred_pending && scroll_settled) {
            //deprocess_uart_bufferferred_pending = false;
            handle_char(deferred_char);
        }
        
        if (time_reached(led_off_time)) {
            gpio_put(LED_PIN, 0);
        }
        
        // Ensure minimum loop frequency to keep cursor blinking
        int64_t time_diff = absolute_time_diff_us(last_loop_time, now);
        if (uart_tail == uart_head && time_diff < MAIN_LOOP_MIN_MS * 1000) {
            busy_wait_us(MAIN_LOOP_MIN_MS * 1000 - time_diff);
        }
        last_loop_time = now;
    }
}
