#include "tmds_encode_font_2bpp_c.h"
#include "tmds_palette.h"

void tmds_encode_font_2bpp_c(const uint8_t *charbuf, const uint32_t *colourbuf, const uint8_t *attrbuf, uint32_t *tmdsbuf, uint n_pix, const uint8_t *font_line, uint font_y) {
    uint32_t *tmds_out = tmdsbuf;
    extern bool blink_phase; // Provided by main rendering loop
    for (uint i = 0; i < n_pix / 8; ++i) {
        uint32_t eight_colours = colourbuf[i];
        for (uint j = 0; j < 8; ++j) {
            uint8_t c = charbuf[i * 8 + j];
            uint8_t attr = attrbuf[i * 8 + j];
            uint8_t font_bits = font_line[c];
            if ((attr & ATTR_UNDERLINE) && (font_y == 15)) {
                font_bits = 0xff;
            }
            // BLINK: If blink attribute set and blink_phase is off, render as space
            if ((attr & ATTR_BLINK) && !blink_phase) {
                font_bits = 0x00;
            }
            uint32_t colour_bits = (eight_colours >> (j * 4)) & 0xf;
            // low nibble
            uint32_t lut_index1 = (colour_bits << 4) | (font_bits & 0xf);
            const uint32_t *lut_ptr1 = &palettised_1bpp_tables[lut_index1 * 2];
            *tmds_out++ = *lut_ptr1++;
            *tmds_out++ = *lut_ptr1;
            // high nibble
            uint32_t lut_index2 = (colour_bits << 4) | (font_bits >> 4);
            const uint32_t *lut_ptr2 = &palettised_1bpp_tables[lut_index2 * 2];
            *tmds_out++ = *lut_ptr2++;
            *tmds_out++ = *lut_ptr2;
        }
    }
}