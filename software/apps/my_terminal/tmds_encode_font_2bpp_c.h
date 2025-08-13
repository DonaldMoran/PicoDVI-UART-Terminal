#ifndef _TMDS_ENCODE_FONT_2BPP_C_H
#define _TMDS_ENCODE_FONT_2BPP_C_H

#include "pico/types.h"

#define ATTR_UNDERLINE 0x01
#define ATTR_BLINK     0x02

void tmds_encode_font_2bpp_c(const uint8_t *charbuf, const uint32_t *colourbuf, const uint8_t *attrbuf, uint32_t *tmdsbuf, uint n_pix, const uint8_t *font_line, uint font_y);

#endif
