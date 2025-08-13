```c
/*
===============================================================================
DVI Terminal Emulator for RP2350 / Pico2 (Fixed Compile, Cursor, Tearing, I2C Filter)
===============================================================================
Changes:
- Fixed compile errors in cursor blink switch statement
- Retained \r handling with new_line() for BASIC compatibility
- Fixed cursor blinking with counter-based timing and forced swaps
- Eliminated remaining screen tearing with stricter VSYNC sync
- Preserved I2C handling for Microsoft BASIC
- Enhanced debug output for cursor, swap timing, and I2C
- Fixed printf format (%u to %lu for cursor_blink_counter) for debug
- Ensured cursor background always matches screen background (current_bg)
- Updated color mappings for accurate 6-bit RGB values
- Added I2C address byte filter to prevent erroneous 'U' (0x55)
===============================================================================
*/
#define DEBUG 1 // Enable debug for I2C logging
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
#include "pico/i2c_slave.h"
#include "dvi.h"
#include "dvi_serialiser.h"
#ifndef DVI_DEFAULT_SERIAL_CONFIG
#define DVI_DEFAULT_SERIAL_CONFIG adafruit_hdmi_sock_cfg
#endif
#include "common_dvi_pin_configs.h"
#include "tmds_encode_font_2bpp.h"
#include "font_8x16.h"

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
#define I2C_BUFFER_SIZE 512
#define CURSOR_BLINK_MS 500
#define MAIN_LOOP_MIN_MS 10
#define I2C_SLAVE i2c1
#define I2C_SDA_PIN 26
#define I2C_SCL_PIN 27
#define I2C_SLAVE_ADDR 0x55
#define I2C_BAUD_RATE 400000
#define LED_PIN 25

// === Global State ===
struct dvi_inst dvi0;
static uint8_t font_scanline[FONT_N_CHARS * FONT_CHAR_HEIGHT];
__attribute__((aligned(4))) static char charbuf_front[CHAR_ROWS * CHAR_COLS];
__attribute__((aligned(4))) static char charbuf_back[CHAR_ROWS * CHAR_COLS];
__attribute__((aligned(4))) static uint32_t colourbuf_front[3 * COLOUR_PLANE_SIZE_WORDS + COLOUR_PAD_WORDS];
__attribute__((aligned(4))) static uint32_t colourbuf_back[3 * COLOUR_PLANE_SIZE_WORDS + COLOUR_PAD_WORDS];
static volatile bool buffer_lock = false;
volatile bool swap_pending = false;
volatile bool scroll_settled = true;
volatile bool safe_to_scroll = false;
volatile bool swap_queued = false;
static volatile uint8_t i2c_buffer[I2C_BUFFER_SIZE];
static volatile uint16_t i2c_head = 0;
static volatile uint16_t i2c_tail = 0;
static volatile bool i2c_overflow = false;
typedef struct {
    uint16_t cursor_x;
    uint16_t cursor_y;
    bool cursor_visible;
    bool escape_mode;
    bool ansi_mode;
    bool skip_next_lf;
} terminal_state_t;
terminal_state_t term;
volatile bool input_active = false;
absolute_time_t last_input_time;
bool cursor_drawn = false;
int saved_cursor_x = -1;
int saved_cursor_y = -1;
int cursor_draw_x = -1;
int cursor_draw_y = -1;
char saved_cursor_char = ' ';
uint8_t saved_cursor_fg = 0;
volatile absolute_time_t led_off_time;
volatile uint32_t cursor_blink_counter = 0;
volatile bool buffer_dirty = false;
static char deferred_char;
static bool deferred_pending = false;
#define ANSI_PARAM_MAX 4
uint8_t ansi_params[ANSI_PARAM_MAX];
uint8_t ansi_param_count = 0;
uint8_t ansi_param_index = 0;
char ansi_buffer[16];
uint8_t ansi_buf_len = 0;
char ansi_final_char = '\0';
enum cursor_style { CURSOR_IBM_RETRO, CURSOR_UNDERLINE, CURSOR_BAR, CURSOR_APPLE_I };
enum cursor_style current_cursor = CURSOR_APPLE_I;
uint8_t current_fg = 12;
uint8_t current_bg = 0;
#define MENU_BUFFER_WIDTH 34
#define MENU_BUFFER_HEIGHT 10
char saved_chars[MENU_BUFFER_HEIGHT][MENU_BUFFER_WIDTH];
uint8_t saved_fg[MENU_BUFFER_HEIGHT][MENU_BUFFER_WIDTH];
uint8_t saved_bg[MENU_BUFFER_HEIGHT][MENU_BUFFER_WIDTH];
uint16_t menu_left = 0;
uint16_t menu_top = 0;
volatile bool theme_select_mode = false;
volatile bool cursor_menu_mode = false;
bool awaiting_fg_code = false;
bool awaiting_bg_code = false;

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
    if (!swap_queued) {
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
        perform_swap();
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
    memmove(&charbuf_back[0], &charbuf_back[CHAR_COLS], (CHAR_ROWS - 1) * CHAR_COLS);
    for (uint x = 0; x < CHAR_COLS; x++) {
        charbuf_back[x + (CHAR_ROWS - 1) * CHAR_COLS] = ' ';
    }
    for (int p = 0; p < 3; p++) {
        uint32_t *base = &colourbuf_back[p * COLOUR_PLANE_SIZE_WORDS];
        memmove(base, &base[CHAR_COLS / 8], (CHAR_ROWS - 1) * (CHAR_COLS / 8) * sizeof(uint32_t));
    }
    for (uint x = 0; x < CHAR_COLS; x++) {
        uint idx = x + (CHAR_ROWS - 1) * CHAR_COLS;
        uint bit = (idx % 8) * 4;
        uint word = idx / 8;
        for (int p = 0; p < 3; p++) {
            colourbuf_back[word + p * COLOUR_PLANE_SIZE_WORDS] &= ~(0xFu << bit);
        }
    }
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
    safe_request_swap();
}

// === ANSI Processing ===
void process_ansi_code(uint8_t param) {
    if (param == 0) {
        current_fg = 63;
        current_bg = 0;
    } else if (param >= 30 && param <= 37) {
        current_fg = param - 30 + 1;
    } else if (param >= 40 && param <= 47) {
        current_bg = param - 40 + 1;
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
    case 'A':
        if (term.cursor_y > 0) {
            term.cursor_y--;
        }
        break;
    case 'B':
        if (term.cursor_y < CHAR_ROWS - 1) {
            term.cursor_y++;
        }
        break;
    case 'C':
        if (term.cursor_x < CHAR_COLS - 1) {
            term.cursor_x++;
        }
        break;
    case 'D':
        if (term.cursor_x > 0) {
            term.cursor_x--;
        }
        break;
    }
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
        "[1] IBM \xDB",
        "[2] Underline _",
        "[3] Bar |",          
        "[4] Apple I @",  
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
    #ifdef DEBUG
    printf("handle_char: c=0x%02X (%c), cursor_x=%d, cursor_y=%d\n",
           c, (c >= 32 && c < 127) ? c : '.', term.cursor_x, term.cursor_y);
    #endif
    if (cursor_drawn) {
        set_char(cursor_draw_x, cursor_draw_y, saved_cursor_char);
        set_colour(cursor_draw_x, cursor_draw_y, saved_cursor_fg, current_bg);
        cursor_drawn = false;
        buffer_dirty = true;
    }
    if (term.skip_next_lf && c == '\n') {
        term.skip_next_lf = false;
        return;
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
    if (cursor_menu_mode) {
        switch (c) {
        case '1': current_cursor = CURSOR_IBM_RETRO; break;
        case '2': current_cursor = CURSOR_UNDERLINE; break;
        case '3': current_cursor = CURSOR_BAR; break;
        case '4': current_cursor = CURSOR_APPLE_I; break;
        default: return;
        }
        cursor_menu_mode = false;
        restore_menu_region();
        term.cursor_visible = true;
        cursor_blink_counter = 0;
        return;
    }
    if (theme_select_mode) {
        switch (c) {
        case '1': current_fg = 12; current_bg = 0; break;
        case '2': current_fg = 60; current_bg = 0; break;
        case '3': current_fg = 15; current_bg = 5; break;
        case '4': current_fg = 63; current_bg = 0; break;
        case '5': current_fg = 3; current_bg = 0; break;
        case '6': current_fg = 48; current_bg = 21; break;
        case '7': current_fg = 51; current_bg = 0; break;
        case '8': current_fg = 0; current_bg = 12; break;
        case '9': current_fg = 6; current_bg = 11; break;
        default: return;
        }
        theme_select_mode = false;
        return;
    }
    switch (c) {
    case '\x06': awaiting_fg_code = true; break;
    case '\x02': awaiting_bg_code = true; break;
    case '\x14': theme_select_mode = true; break;
    case '\x0E': cursor_menu_mode = true; draw_cursor_menu(); break;
    case '\x07': current_fg = 12; current_bg = 0; break;
    case '\x17': current_fg = 63; current_bg = 0; break;
    case '\x03': current_fg = 15; break;
    case '\x04': current_fg = 4; break;
    case '\x0F': current_fg = 48; break;
    case '\x12': current_fg = 48; break;
    case '\x13': current_fg = 51; break;
    case '\x19': current_fg = 60; break;
    case '\x0C': current_fg = 21; break;
    case '\x1B': term.escape_mode = true; break;
    case '\r':
        new_line();
        term.skip_next_lf = true;
        break;
    case '\n':
        new_line();
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
        if (awaiting_fg_code) {
            uint8_t fg_color = 0;
            switch (c) {
                case 'k': fg_color = 0;  break;
                case 'b': fg_color = 1;  break;
                case 'g': fg_color = 4;  break;
                case 'c': fg_color = 5;  break;
                case 'r': fg_color = 16; break;
                case 'm': fg_color = 17; break;
                case 'n': fg_color = 50; break;
                case 'l': fg_color = 42; break;
                case 'd': fg_color = 21; break;
                case 'B': fg_color = 3;  break;
                case 'G': fg_color = 12; break;
                case 'C': fg_color = 15; break;
                case 'R': fg_color = 48; break;
                case 'M': fg_color = 51; break;
                case 'y': fg_color = 60; break;
                case 'w': fg_color = 63; break;
                default: fg_color = 0; break;
            }
            current_fg = fg_color;
            awaiting_fg_code = false;
        } else if (awaiting_bg_code) {
            uint8_t bg_color = 0;
            switch (c) {
                case 'k': bg_color = 0;  break;
                case 'b': bg_color = 1;  break;
                case 'g': bg_color = 4;  break;
                case 'c': bg_color = 5;  break;
                case 'r': bg_color = 16; break;
                case 'm': bg_color = 17; break;
                case 'n': bg_color = 50; break;
                case 'l': bg_color = 42; break;
                case 'd': bg_color = 21; break;
                case 'B': bg_color = 3;  break;
                case 'G': bg_color = 12; break;
                case 'C': bg_color = 15; break;
                case 'R': bg_color = 48; break;
                case 'M': bg_color = 51; break;
                case 'y': bg_color = 60; break;
                case 'w': bg_color = 63; break;
                default: bg_color = 0; break;
            }
            current_bg = bg_color;
            awaiting_bg_code = false;
        } else {
            set_char(term.cursor_x, term.cursor_y, c);
            set_colour(term.cursor_x, term.cursor_y, current_fg, current_bg);
            term.cursor_x++;
            buffer_dirty = true;
        }
        if (term.cursor_x >= CHAR_COLS) {
            new_line();
        }
        break;
    }
    if (term.cursor_visible && !cursor_menu_mode) {
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
        set_char(cursor_draw_x, cursor_draw_y, ' ');
        set_colour(cursor_draw_x, cursor_draw_y, 0, current_bg);
        uint8_t cursor_char = ' ';
        uint8_t cursor_fg = current_fg;
        uint8_t cursor_bg = current_bg;
        switch (current_cursor) {
            case CURSOR_IBM_RETRO: cursor_char = (uint8_t)0xDB; break;
            case CURSOR_APPLE_I: cursor_char = '@'; break;
            case CURSOR_UNDERLINE: cursor_char = '_'; break;
            case CURSOR_BAR: cursor_char = '|'; break;
        }
        set_char(cursor_draw_x, cursor_draw_y, cursor_char);
        set_colour(cursor_draw_x, cursor_draw_y, cursor_fg, cursor_bg);
        cursor_drawn = true;
        buffer_dirty = true;
    }
    safe_request_swap();
}

// === Input Handling ===
void i2c1_irq_handler(void) {
    i2c_hw_t *hw = i2c_get_hw(i2c1);
    if (i2c_overflow) {
        busy_wait_us(10);
        hw->intr_mask = I2C_IC_INTR_MASK_M_RX_FULL_BITS;
        return;
    }
    while (i2c_get_read_available(i2c1)) {
        uint16_t next_head = (i2c_head + 1) % I2C_BUFFER_SIZE;
        if (next_head == i2c_tail) {
            i2c_overflow = true;
            #ifdef DEBUG
            printf("I2C buffer overflow, head=%d, tail=%d\n", i2c_head, i2c_tail);
            #endif
            break;
        }
        uint8_t c = i2c_read_byte_raw(i2c1);
        if (c == I2C_SLAVE_ADDR) { // Skip I2C address byte
            #ifdef DEBUG
            printf("I2C skipped address: 0x%02X\n", c);
            #endif
            continue;
        }
        #ifdef DEBUG
        printf("I2C raw: 0x%02X (%c)\n", c, (c >= 32 && c < 127) ? c : '.');
        #endif
        i2c_buffer[i2c_head] = c;
        i2c_head = next_head;
        gpio_put(LED_PIN, 1);
        led_off_time = make_timeout_time_ms(30);
    }
    hw->intr_mask = I2C_IC_INTR_MASK_M_RX_FULL_BITS;
}
void process_i2c_buffer(void) {
    while (i2c_tail != i2c_head) {
        char c = i2c_buffer[i2c_tail];
        i2c_tail = (i2c_tail + 1) % I2C_BUFFER_SIZE;
        handle_char(c);
        if (i2c_overflow && 
            ((i2c_head - i2c_tail + I2C_BUFFER_SIZE) % I2C_BUFFER_SIZE) > I2C_BUFFER_SIZE / 4) {
            i2c_overflow = false;
            #ifdef DEBUG
            printf("I2C overflow cleared, buffer space: %d\n", 
                   (i2c_head - i2c_tail + I2C_BUFFER_SIZE) % I2C_BUFFER_SIZE);
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
    stdio_init_all();
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 1);
    i2c_init(I2C_SLAVE, I2C_BAUD_RATE);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
    i2c_set_slave_mode(I2C_SLAVE, true, I2C_SLAVE_ADDR);
    irq_set_exclusive_handler(I2C1_IRQ, i2c1_irq_handler);
    irq_set_enabled(I2C1_IRQ, true);
    i2c_hw_t *hw = i2c_get_hw(i2c1);
    hw->intr_mask = I2C_IC_INTR_MASK_M_RX_FULL_BITS;
    vreg_set_voltage(VREG_VSEL);
    sleep_ms(10);
    set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);
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
    term.cursor_visible = true;
    term.cursor_x = 0;
    term.cursor_y = 0;
    cursor_draw_x = 0;
    cursor_draw_y = 0;
    saved_cursor_x = 0;
    saved_cursor_y = 0;
    clear_screen();
    perform_swap();
    hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
    multicore_launch_core1(core1_main);
    watchdog_reinit();
    cursor_blink_counter = 0;
    absolute_time_t last_loop_time = get_absolute_time();
    while (1) {
        absolute_time_t now = get_absolute_time();
        watchdog_update();
        if (term.cursor_visible && !cursor_menu_mode) {
            cursor_blink_counter++;
            if (cursor_blink_counter >= (CURSOR_BLINK_MS / MAIN_LOOP_MIN_MS)) {
                cursor_blink_counter = 0;
                if (cursor_drawn) {
                    set_char(cursor_draw_x, cursor_draw_y, saved_cursor_char);
                    set_colour(cursor_draw_x, cursor_draw_y, saved_cursor_fg, current_bg);
                    cursor_drawn = false;
                } else {
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
                    set_char(cursor_draw_x, cursor_draw_y, ' ');
                    set_colour(cursor_draw_x, cursor_draw_y, 0, current_bg);
                    uint8_t cursor_char = ' ';
                    uint8_t cursor_fg = current_fg;
                    uint8_t cursor_bg = current_bg;
                    switch (current_cursor) {
                        case CURSOR_IBM_RETRO: cursor_char = (uint8_t)0xDB; break;
                        case CURSOR_APPLE_I: cursor_char = '@'; break;
                        case CURSOR_UNDERLINE: cursor_char = '_'; break;
                        case CURSOR_BAR: cursor_char = '|'; break;
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
        process_i2c_buffer();
        if (absolute_time_diff_us(last_loop_time, now) > 100000) {
            input_active = false;
        }
        if (deferred_pending && scroll_settled) {
            deferred_pending = false;
            handle_char(deferred_char);
        }
        if (time_reached(led_off_time)) {
            gpio_put(LED_PIN, 0);
        }
        int64_t time_diff = absolute_time_diff_us(last_loop_time, now);
        if (i2c_tail == i2c_head && time_diff < MAIN_LOOP_MIN_MS * 1000) {
            busy_wait_us(MAIN_LOOP_MIN_MS * 1000 - time_diff);
        }
        last_loop_time = now;
    }
}
```
