#include "bdm_core.h"
#include "h8.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define BDM_CART_MAX (128u * 1024u)
#define BDM_CART_BANK_SIZE 0x8000u
#define BDM_BIOS_MAX 0x6000u
#define BDM_EXT_RAM_BASE 0x8000u
#define BDM_EXT_RAM_SIZE 0x7b80u
#define BDM_IRAM_BASE 0xfb80u
#define BDM_IRAM_SIZE 0x0400u
#define BDM_IO_BASE 0xff80u
#define BDM_IO_SIZE 0x0080u

struct bdm_core {
    int trace_io;
    h8_cpu_t cpu;
    bdm_video_t *video;
    bdm_input_t *input;
    bdm_sound_t *sound;
    bool own_video;
    bool own_input;
    bool own_sound;

    uint8_t cart[BDM_CART_MAX];
    size_t cart_size;
    bool cart_loaded;
    unsigned cart_bank;

    uint8_t media_cart[BDM_CART_MAX];
    size_t media_size;
    bool media_loaded;
    unsigned media_bank;
    bool media_selected;

    uint8_t bios[BDM_BIOS_MAX];
    size_t bios_size;
    bool bios_loaded;

    uint8_t ext_ram[BDM_EXT_RAM_SIZE];
    uint8_t internal_ram[BDM_IRAM_SIZE];
    uint8_t io[BDM_IO_SIZE];

    /* H8/3334 on-chip peripheral shadows used by the bootstrap paths.
       MAME marks this family incomplete; the mappings below follow its
       h83337.cpp map plus the Design Master cartridge's observed writes. */
    uint8_t timer16_tier;
    uint8_t timer16_tsr;
    uint16_t timer16_tcnt;
    uint16_t timer16_ocra;
    uint8_t timer16_tcr;
    unsigned timer16_prescale_accum;

    uint8_t timer8_tcr[2];
    uint8_t timer8_tcsr[2];
    uint8_t timer8_tcora[2];
    uint8_t timer8_tcorb[2];
    uint8_t timer8_tcnt[2];
    unsigned timer8_prescale_accum[2];

    uint8_t port6_drive;

    uint16_t adc_value[8];
    uint8_t adc_adcsr;
    uint8_t adc_adcr;
    uint8_t adc_pending_control;
    unsigned adc_delay_steps;
};

static unsigned bank_count(size_t size) {
    unsigned banks = (unsigned)(size / BDM_CART_BANK_SIZE);
    if ((size % BDM_CART_BANK_SIZE) != 0) ++banks;
    return banks ? banks : 1u;
}

static unsigned timer16_step_divisor(uint8_t tcr) {
    /* H8/3334 TCR low clock-select bits are approximated in instruction steps.
       The supplied carts set TCR=0x02 and OCRA=0x09c4; a /4 instruction-step
       divisor matches the observed UI pacing without the old artificial
       interval counter. */
    switch (tcr & 0x07u) {
    case 0x00u: return 1u;
    case 0x01u: return 2u;
    case 0x02u: return 4u;
    case 0x03u: return 8u;
    case 0x04u: return 16u;
    case 0x05u: return 32u;
    case 0x06u: return 64u;
    default: return 128u;
    }
}

static unsigned timer8_step_divisor(unsigned channel, uint8_t tcr) {
    static const unsigned div0[8] = { 8u, 2u, 64u, 32u, 1024u, 256u, 1024u, 1024u };
    static const unsigned div1[8] = { 8u, 2u, 64u, 128u, 1024u, 2048u, 2048u, 2048u };
    return channel ? div1[tcr & 0x07u] : div0[tcr & 0x07u];
}

static uint16_t word_from_io(const bdm_core_t *c, uint16_t address) {
    return (uint16_t)(((uint16_t)c->io[address - BDM_IO_BASE] << 8) |
                      c->io[(uint16_t)(address + 1u) - BDM_IO_BASE]);
}

static void word_to_io(bdm_core_t *c, uint16_t address, uint16_t value) {
    c->io[address - BDM_IO_BASE] = (uint8_t)(value >> 8);
    c->io[(uint16_t)(address + 1u) - BDM_IO_BASE] = (uint8_t)value;
}

static uint8_t core_read8(void *opaque, uint16_t address) {
    bdm_core_t *c = (bdm_core_t *)opaque;
    if (address < 0x8000u) {
        const uint8_t *rom = NULL;
        size_t rom_size = 0;
        unsigned bank = 0;

        if (c->media_selected && c->media_loaded && c->media_size) {
            rom = c->media_cart;
            rom_size = c->media_size;
            bank = c->media_bank;
        } else if (c->cart_loaded && c->cart_size) {
            rom = c->cart;
            rom_size = c->cart_size;
            bank = c->cart_bank;
        }

        if (rom && rom_size) {
            size_t base = (size_t)bank * BDM_CART_BANK_SIZE;
            size_t off = base + address;
            if (off < rom_size) return rom[off];
            if (address < rom_size) return rom[address];
            return 0xffu;
        }
        if (c->bios_loaded && address < c->bios_size) return c->bios[address];
        return 0xffu;
    }

    if (address >= BDM_EXT_RAM_BASE && address < BDM_EXT_RAM_BASE + BDM_EXT_RAM_SIZE) {
        return c->ext_ram[address - BDM_EXT_RAM_BASE];
    }
    if (address >= BDM_IRAM_BASE && address < BDM_IRAM_BASE + BDM_IRAM_SIZE) {
        return c->internal_ram[address - BDM_IRAM_BASE];
    }
    if (address >= BDM_IO_BASE) {
        if (address == 0xff90u) return c->timer16_tier;
        if (address == 0xff91u) return c->timer16_tsr;
        if (address == 0xff92u) return (uint8_t)(c->timer16_tcnt >> 8);
        if (address == 0xff93u) return (uint8_t)c->timer16_tcnt;
        if (address == 0xff94u) return (uint8_t)(c->timer16_ocra >> 8);
        if (address == 0xff95u) return (uint8_t)c->timer16_ocra;
        if (address == 0xff96u) return c->timer16_tcr;
        if (address == 0xff98u) return (uint8_t)(c->timer16_ocra >> 8);
        if (address == 0xff99u) return (uint8_t)c->timer16_ocra;
        if (address >= 0xffc8u && address <= 0xffccu) {
            unsigned ch = 0;
            switch (address) {
            case 0xffc8u: return c->timer8_tcr[ch];
            case 0xffc9u: return c->timer8_tcsr[ch];
            case 0xffcau: return c->timer8_tcora[ch];
            case 0xffcbu: return c->timer8_tcorb[ch];
            case 0xffccu: return c->timer8_tcnt[ch];
            }
        }
        if (address >= 0xffd0u && address <= 0xffd4u) {
            unsigned ch = 1;
            switch (address) {
            case 0xffd0u: return c->timer8_tcr[ch];
            case 0xffd1u: return c->timer8_tcsr[ch];
            case 0xffd2u: return c->timer8_tcora[ch];
            case 0xffd3u: return c->timer8_tcorb[ch];
            case 0xffd4u: return c->timer8_tcnt[ch];
            }
        }
        if (address == 0xffbau) return bdm_input_read_panel_port(c->input);
        if (address == 0xffbeu) return bdm_input_read_port7(c->input);
        if (address == 0xff8cu || address == 0xffdcu) return 0x84u; /* SCI SSR: TDRE|TEND */
        if (address == 0xff8du || address == 0xffddu) return 0xffu; /* SCI RDR */
        if (address >= 0xffe0u && address <= 0xffe7u) {
            unsigned off = (unsigned)(address - 0xffe0u);
            uint16_t v = c->adc_value[off >> 1u] & 0x03ffu;
            return (off & 1u) ? (uint8_t)(v << 6) : (uint8_t)(v >> 2);
        }
        if (address == 0xffe8u) return c->adc_adcsr;
        if (address == 0xffe9u) return c->adc_adcr;
        return c->io[address - BDM_IO_BASE];
    }
    return 0xffu;
}

static void adc_latch_samples(bdm_core_t *c) {
    if (!c) return;

    for (unsigned i = 0; i < 8; ++i) c->adc_value[i] = 0x0000u;

    if (bdm_input_pen_down(c->input)) {
        int px = bdm_input_pen_x(c->input);
        int py = bdm_input_pen_y(c->input);
        if (px < 0) px = 0;
        if (px > 159) px = 159;
        if (py < 0) py = 0;
        if (py > 119) py = 119;

        /* The game startup calibration reads four ADC channels.  Empirically,
           channels 0/1 are the panel's vertical voltage and channels 2/3 are
           the horizontal voltage; this is why the logical X/Y order appears
           swapped if the raw values are assigned naively.  The margins model
           the analog front end rather than forcing ideal 0..1023 endpoints. */
        /* Use most of the 10-bit ADC range for the 160x120 active LCD area.
           Keep a small analog guard band while preserving fractional stylus
           positions from high-resolution host pointer input.  Do not quantize
           the coordinate in LCD-pixel space here: the real resistive panel is
           analog, and quantizing it before the cartridge's calibration math is
           what caused slow strokes to land on every other pixel. */
        int32_t px_fp = bdm_input_pen_x_fp(c->input);
        int32_t py_fp = bdm_input_pen_y_fp(c->input);
        const int32_t max_x_fp = ((int32_t)BDM_LCD_ACTIVE_WIDTH << 16) - 1;
        const int32_t max_y_fp = ((int32_t)BDM_LCD_ACTIVE_HEIGHT << 16) - 1;
        if (px_fp < 0) px_fp = 0;
        if (py_fp < 0) py_fp = 0;
        if (px_fp > max_x_fp) px_fp = max_x_fp;
        if (py_fp > max_y_fp) py_fp = max_y_fp;
        uint16_t raw_x = (uint16_t)(0x0020u + (uint32_t)(((uint64_t)(uint32_t)px_fp * 960u) / (159u << 16)));
        uint16_t raw_y = (uint16_t)(0x0020u + (uint32_t)(((uint64_t)(uint32_t)py_fp * 960u) / (119u << 16)));
        if (raw_x > 0x03ffu) raw_x = 0x03ffu;
        if (raw_y > 0x03ffu) raw_y = 0x03ffu;

        /* Port 6 (ffbb) is repeatedly driven as 0x0c/0x09/0x06 around ADC
           sampling.  Treat it as the resistive-panel electrode drive, but do
           not mirror the reported coordinate when the drive polarity changes.
           The software performs its own calibration from two screen points;
           returning inverse X/Y on alternate drive phases makes drawing appear
           at both the requested and mirrored X positions.  Channels 0/1 carry
           the vertical voltage and 2/3 carry the horizontal voltage in a stable
           raw coordinate space; complementary values are exposed only on the
           currently unused upper channels for diagnostics/future refinement. */
        uint16_t inv_x = (uint16_t)(0x03ffu - raw_x);
        uint16_t inv_y = (uint16_t)(0x03ffu - raw_y);
        c->adc_value[0] = raw_y;
        c->adc_value[1] = raw_y;
        c->adc_value[2] = raw_x;
        c->adc_value[3] = raw_x;
        c->adc_value[4] = inv_y;
        c->adc_value[5] = inv_y;
        c->adc_value[6] = inv_x;
        c->adc_value[7] = inv_x;
    }
}

static void adc_start_conversion(bdm_core_t *c, uint8_t control) {
    if (!c) return;
    c->adc_pending_control = control;
    c->adc_delay_steps = 32u;
    c->adc_adcsr = (uint8_t)(control & 0x7fu); /* clear ADF while ADST is active */
}

static void adc_tick(bdm_core_t *c) {
    if (!c || c->adc_delay_steps == 0u) return;
    if (--c->adc_delay_steps == 0u) {
        adc_latch_samples(c);
        c->adc_adcsr = (uint8_t)((c->adc_pending_control & 0x5fu) | 0x80u);
        c->io[0xffe8u - BDM_IO_BASE] = c->adc_adcsr;
    }
}

static void timer8_tick_channel(bdm_core_t *c, unsigned ch) {
    if (!c || ch >= 2u) return;
    unsigned div = timer8_step_divisor(ch, c->timer8_tcr[ch]);
    if (++c->timer8_prescale_accum[ch] < div) return;
    c->timer8_prescale_accum[ch] = 0;

    uint8_t next = (uint8_t)(c->timer8_tcnt[ch] + 1u);
    c->timer8_tcnt[ch] = next;
    if (next == c->timer8_tcora[ch]) c->timer8_tcsr[ch] |= 0x40u;
    if (next == c->timer8_tcorb[ch]) c->timer8_tcsr[ch] |= 0x20u;

    if (ch == 0) {
        c->io[0xffc9u - BDM_IO_BASE] = c->timer8_tcsr[ch];
        c->io[0xffccu - BDM_IO_BASE] = c->timer8_tcnt[ch];
    } else {
        c->io[0xffd1u - BDM_IO_BASE] = c->timer8_tcsr[ch];
        c->io[0xffd4u - BDM_IO_BASE] = c->timer8_tcnt[ch];
    }
}

static void timer16_tick(bdm_core_t *c) {
    if (!c) return;
    unsigned div = timer16_step_divisor(c->timer16_tcr);
    if (++c->timer16_prescale_accum < div) return;
    c->timer16_prescale_accum = 0;

    c->timer16_tcnt = (uint16_t)(c->timer16_tcnt + 1u);
    if (c->timer16_tcnt == c->timer16_ocra) {
        c->timer16_tsr |= 0x01u;
        word_to_io(c, 0xff92u, c->timer16_tcnt);
        c->io[0xff91u - BDM_IO_BASE] = c->timer16_tsr;
        if ((c->timer16_tier & 0x08u) && !(c->cpu.ccr & 0x80u)) {
            h8_take_interrupt(&c->cpu, 16u);
        }
        /* Clear-on-compare mode is how the carts use the timer for regular UI
           pacing.  On the real part this is controlled by TCR bits; here we
           apply it for the observed Design Master configuration. */
        c->timer16_tcnt = 0;
        word_to_io(c, 0xff92u, c->timer16_tcnt);
    }
}

static void core_write8(void *opaque, uint16_t address, uint8_t value) {
    bdm_core_t *c = (bdm_core_t *)opaque;
    if (address >= BDM_EXT_RAM_BASE && address < BDM_EXT_RAM_BASE + BDM_EXT_RAM_SIZE) {
        size_t off = (size_t)(address - BDM_EXT_RAM_BASE);
        c->ext_ram[off] = value;
        if (c->trace_io && (off < BDM_LCD_VRAM_SIZE || address >= 0x8960u)) fprintf(stderr, "W RAM %06llu %04x=%02x pc=%04x\n", (unsigned long long)c->cpu.steps, address, value, c->cpu.last_pc);
        if (off < BDM_LCD_VRAM_SIZE) bdm_video_lcd_vram_write(c->video, off, value);
        return;
    }
    if (address >= BDM_IRAM_BASE && address < BDM_IRAM_BASE + BDM_IRAM_SIZE) {
        c->internal_ram[address - BDM_IRAM_BASE] = value;
        return;
    }
    if (address >= BDM_IO_BASE) {
        size_t off = (size_t)(address - BDM_IO_BASE);
        c->io[off] = value;
        if (c->trace_io) fprintf(stderr, "W IO  %06llu %04x=%02x pc=%04x\n", (unsigned long long)c->cpu.steps, address, value, c->cpu.last_pc);
        bdm_sound_io_write(c->sound, address, value, c->cpu.steps);

        if (address == 0xff90u) {
            c->timer16_tier = value;
            c->io[off] = value;
            return;
        } else if (address == 0xff91u) {
            c->timer16_tsr = (uint8_t)(c->timer16_tsr & (uint8_t)~value);
            c->io[off] = c->timer16_tsr;
            return;
        } else if (address == 0xff92u || address == 0xff93u) {
            c->io[off] = value;
            c->timer16_tcnt = word_from_io(c, 0xff92u);
            return;
        } else if (address == 0xff94u || address == 0xff95u) {
            c->io[off] = value;
            c->timer16_ocra = word_from_io(c, 0xff94u);
            return;
        } else if (address == 0xff96u) {
            c->timer16_tcr = value;
            c->io[off] = value;
            return;
        } else if (address >= 0xffc8u && address <= 0xffccu) {
            unsigned ch = 0;
            switch (address) {
            case 0xffc8u: c->timer8_tcr[ch] = value; break;
            case 0xffc9u: c->timer8_tcsr[ch] = (uint8_t)(c->timer8_tcsr[ch] & (uint8_t)~value); c->io[off] = c->timer8_tcsr[ch]; return;
            case 0xffcau: c->timer8_tcora[ch] = value; break;
            case 0xffcbu: c->timer8_tcorb[ch] = value; break;
            case 0xffccu: c->timer8_tcnt[ch] = value; break;
            }
        } else if (address >= 0xffd0u && address <= 0xffd4u) {
            unsigned ch = 1;
            switch (address) {
            case 0xffd0u: c->timer8_tcr[ch] = value; break;
            case 0xffd1u: c->timer8_tcsr[ch] = (uint8_t)(c->timer8_tcsr[ch] & (uint8_t)~value); c->io[off] = c->timer8_tcsr[ch]; return;
            case 0xffd2u: c->timer8_tcora[ch] = value; break;
            case 0xffd3u: c->timer8_tcorb[ch] = value; break;
            case 0xffd4u: c->timer8_tcnt[ch] = value; break;
            }
        } else if (address == 0xffbbu) {
            c->port6_drive = value;
        }

        if (address == 0xff80u) {
            bdm_video_lcd_index_write(c->video, value);
        } else if (address == 0xff81u) {
            bdm_video_lcd_data_write(c->video, value);
        } else if (address == 0xffe8u) {
            if (value & 0x20u) adc_start_conversion(c, value);
            else c->adc_adcsr = (uint8_t)(value & 0x7fu);
        } else if (address == 0xffe9u) {
            c->adc_adcr = value;
        } else if (address == 0xff84u) {
            /* Observed game-cart bank latch.  The cartridges have a 74HC74
               generating ROM A15/A16.  The startup code copies a thunk to
               internal RAM, writes the requested bank value here, then JSRs
               to 0x0000; nonzero G-cart banks contain a JMP vector there.

               Bit 2 is reserved here as a tentative media-cart CE.  The
               supplied games do not exercise it during the currently reached
               boot path, but exposing it keeps the G/M cart split explicit. */
            c->media_selected = ((value & 0x04u) != 0u) && c->media_loaded;
            if (c->media_selected) {
                c->media_bank = (unsigned)(value & 0x03u) % bank_count(c->media_size);
            } else if (c->cart_size > BDM_CART_BANK_SIZE) {
                c->cart_bank = (unsigned)(value & 0x03u) % bank_count(c->cart_size);
            } else {
                c->cart_bank = 0;
            }
            if (c->trace_io) fprintf(stderr, "BANK  %06llu g=%u m=%u sel=%u pc=%04x\n",
                                     (unsigned long long)c->cpu.steps, c->cart_bank,
                                     c->media_bank, c->media_selected ? 1u : 0u, c->cpu.last_pc);
        } else if (address == 0xffc4u) {
            c->io[off] = (uint8_t)((c->io[off] & 0x08u) | (value & 0xf7u));
        }
        return;
    }
}

static void reset_memory(bdm_core_t *c) {
    memset(c->ext_ram, 0x00, sizeof(c->ext_ram));
    memset(c->internal_ram, 0x00, sizeof(c->internal_ram));
    memset(c->io, 0xff, sizeof(c->io));
    c->io[0xffc2u - BDM_IO_BASE] = 0x08u; /* WSCR */
    c->io[0xffc3u - BDM_IO_BASE] = 0x00u; /* STCR */
    c->io[0xffc4u - BDM_IO_BASE] = 0x09u; /* SYSCR */
    c->cart_bank = 0;
    c->media_bank = 0;
    c->media_selected = false;

    c->timer16_tier = 0x00u;
    c->timer16_tsr = 0x00u;
    c->timer16_tcnt = 0x0000u;
    c->timer16_ocra = 0xffffu;
    c->timer16_tcr = 0x00u;
    c->timer16_prescale_accum = 0;
    word_to_io(c, 0xff92u, c->timer16_tcnt);
    word_to_io(c, 0xff94u, c->timer16_ocra);
    c->io[0xff90u - BDM_IO_BASE] = c->timer16_tier;
    c->io[0xff91u - BDM_IO_BASE] = c->timer16_tsr;
    c->io[0xff96u - BDM_IO_BASE] = c->timer16_tcr;

    memset(c->timer8_tcr, 0, sizeof(c->timer8_tcr));
    memset(c->timer8_tcsr, 0, sizeof(c->timer8_tcsr));
    memset(c->timer8_tcora, 0xff, sizeof(c->timer8_tcora));
    memset(c->timer8_tcorb, 0xff, sizeof(c->timer8_tcorb));
    memset(c->timer8_tcnt, 0, sizeof(c->timer8_tcnt));
    memset(c->timer8_prescale_accum, 0, sizeof(c->timer8_prescale_accum));
    c->port6_drive = 0x00u;

    c->adc_adcsr = 0x80u;
    c->adc_adcr = 0x00u;
    c->adc_pending_control = 0x00u;
    c->adc_delay_steps = 0u;
    c->io[0xffe8u - BDM_IO_BASE] = c->adc_adcsr;
    c->io[0xffe9u - BDM_IO_BASE] = c->adc_adcr;
    adc_latch_samples(c);
}

bdm_core_t *bdm_core_create(const bdm_core_config_t *config) {
    bdm_core_t *c = (bdm_core_t *)calloc(1, sizeof(*c));
    if (!c) return NULL;

    if (config && config->video) c->video = config->video;
    else { c->video = bdm_video_create(); c->own_video = true; }
    if (config && config->input) c->input = config->input;
    else { c->input = bdm_input_create(); c->own_input = true; }
    if (config && config->sound) c->sound = config->sound;
    else { c->sound = bdm_sound_create(); c->own_sound = true; }

    if (!c->video || !c->input || !c->sound) {
        bdm_core_destroy(c);
        return NULL;
    }

    c->trace_io = getenv("BDM_TRACE_IO") ? 1 : 0;
    h8_init(&c->cpu, c, core_read8, core_write8);
    reset_memory(c);
    bdm_core_reset(c);
    return c;
}

void bdm_core_destroy(bdm_core_t *core) {
    if (!core) return;
    if (core->own_video) bdm_video_destroy(core->video);
    if (core->own_input) bdm_input_destroy(core->input);
    if (core->own_sound) bdm_sound_destroy(core->sound);
    free(core);
}

void bdm_core_reset(bdm_core_t *core) {
    if (!core) return;
    reset_memory(core);
    bdm_video_reset(core->video);
    bdm_input_reset(core->input);
    bdm_sound_reset(core->sound);
    h8_reset(&core->cpu);
}

bdm_status_t bdm_core_load_cart(bdm_core_t *core, const void *data, size_t size) {
    if (!core || !data || size == 0) return BDM_ERR_INVALID_ARGUMENT;
    if (size > BDM_CART_MAX) return BDM_ERR_BAD_ROM;
    memcpy(core->cart, data, size);
    if (size < BDM_CART_MAX) memset(core->cart + size, 0xff, BDM_CART_MAX - size);
    core->cart_size = size;
    core->cart_loaded = true;
    core->cart_bank = 0;
    return BDM_OK;
}

bdm_status_t bdm_core_load_media_cart(bdm_core_t *core, const void *data, size_t size) {
    if (!core || !data || size == 0) return BDM_ERR_INVALID_ARGUMENT;
    if (size > BDM_CART_MAX) return BDM_ERR_BAD_ROM;
    memcpy(core->media_cart, data, size);
    if (size < BDM_CART_MAX) memset(core->media_cart + size, 0xff, BDM_CART_MAX - size);
    core->media_size = size;
    core->media_loaded = true;
    core->media_bank = 0;
    core->media_selected = false;
    return BDM_OK;
}

bdm_status_t bdm_core_load_bios(bdm_core_t *core, const void *data, size_t size) {
    if (!core || !data || size == 0) return BDM_ERR_INVALID_ARGUMENT;
    if (size > BDM_BIOS_MAX) return BDM_ERR_BAD_ROM;
    memcpy(core->bios, data, size);
    if (size < BDM_BIOS_MAX) memset(core->bios + size, 0xff, BDM_BIOS_MAX - size);
    core->bios_size = size;
    core->bios_loaded = true;
    return BDM_OK;
}

bdm_status_t bdm_core_load_sram(bdm_core_t *core, const void *data, size_t size) {
    if (!core || !data) return BDM_ERR_INVALID_ARGUMENT;
    if (size > BDM_EXT_RAM_SIZE) return BDM_ERR_BAD_ROM;
    memcpy(core->ext_ram, data, size);
    if (size < BDM_EXT_RAM_SIZE) memset(core->ext_ram + size, 0x00, BDM_EXT_RAM_SIZE - size);
    if (core->video) {
        size_t n = BDM_LCD_VRAM_SIZE < BDM_EXT_RAM_SIZE ? BDM_LCD_VRAM_SIZE : BDM_EXT_RAM_SIZE;
        for (size_t i = 0; i < n; ++i) bdm_video_lcd_vram_write(core->video, i, core->ext_ram[i]);
    }
    return BDM_OK;
}

size_t bdm_core_save_sram(const bdm_core_t *core, void *out_data, size_t out_capacity) {
    if (!core) return 0u;
    if (out_data && out_capacity) {
        size_t n = out_capacity < BDM_EXT_RAM_SIZE ? out_capacity : BDM_EXT_RAM_SIZE;
        memcpy(out_data, core->ext_ram, n);
    }
    return BDM_EXT_RAM_SIZE;
}

size_t bdm_core_external_sram_size(const bdm_core_t *core) {
    (void)core;
    return BDM_EXT_RAM_SIZE;
}

bdm_status_t bdm_core_step(bdm_core_t *core) {
    if (!core) return BDM_ERR_INVALID_ARGUMENT;

    /* The A-E top buttons and side page buttons are real hardware controls
       surrounding the LCD.  The cartridge firmware's common input decoder
       stores decoded panel commands in fbd2: A-E become 1-5 and the side
       page buttons become 6/7.  The buttons are outside the active LCD ADC
       area, so mirror their held hardware state into that decoded latch instead
       of pretending they are normal stylus points. */
    uint8_t panel_code = bdm_input_panel_code(core->input);
    if (panel_code) {
        core->internal_ram[0xfbd2u - BDM_IRAM_BASE] = 0x00u;
        core->internal_ram[0xfbd3u - BDM_IRAM_BASE] = panel_code;
    }

    int rc = h8_step(&core->cpu);
    if (rc > 0) return BDM_ERR_UNSUPPORTED_OPCODE;

    bdm_sound_advance_steps(core->sound, 1u);
    adc_tick(core);
    timer8_tick_channel(core, 0);
    timer8_tick_channel(core, 1);
    if (!core->cpu.unsupported) timer16_tick(core);
    return BDM_OK;
}

uint64_t bdm_core_run_steps(bdm_core_t *core, uint64_t max_steps, bool stop_on_unsupported) {
    if (!core) return 0;
    uint64_t ran = 0;
    while (ran < max_steps) {
        bdm_status_t st = bdm_core_step(core);
        if (st == BDM_ERR_UNSUPPORTED_OPCODE && stop_on_unsupported) break;
        if (st != BDM_OK && st != BDM_ERR_UNSUPPORTED_OPCODE) break;
        if (st == BDM_ERR_UNSUPPORTED_OPCODE) break;
        ++ran;
    }
    return ran;
}

void bdm_core_get_state(const bdm_core_t *core, bdm_core_state_t *out_state) {
    if (!core || !out_state) return;
    memset(out_state, 0, sizeof(*out_state));
    out_state->steps = core->cpu.steps;
    out_state->pc = core->cpu.pc;
    out_state->last_pc = core->cpu.last_pc;
    out_state->last_opcode = core->cpu.last_opcode;
    memcpy(out_state->r, core->cpu.r, sizeof(out_state->r));
    out_state->ccr = core->cpu.ccr;
    out_state->stopped = core->cpu.stopped;
    out_state->unsupported = core->cpu.unsupported;
    out_state->unsupported_pc = core->cpu.unsupported_pc;
    out_state->unsupported_opcode = core->cpu.unsupported_opcode;
    out_state->cart_bank = core->cart_bank;
    out_state->media_bank = core->media_bank;
    out_state->media_selected = core->media_selected;
    out_state->timer16_counter = core->timer16_tcnt;
    out_state->timer16_compare = core->timer16_ocra;
    out_state->timer16_tier = core->timer16_tier;
    out_state->timer16_tsr = core->timer16_tsr;
    out_state->timer16_tcr = core->timer16_tcr;
    out_state->panel_drive = core->port6_drive;
    out_state->port7_value = bdm_input_read_port7(core->input);
    out_state->adc_status = core->adc_adcsr;
    out_state->adc_control = core->adc_adcr;
}

uint8_t bdm_core_bus_read8(const bdm_core_t *core, uint16_t address) {
    if (!core) return 0xff;
    return core_read8((void *)core, address);
}

uint16_t bdm_core_bus_read16(const bdm_core_t *core, uint16_t address) {
    if (!core) return 0xffff;
    address &= (uint16_t)~1u;
    return (uint16_t)(((uint16_t)bdm_core_bus_read8(core, address) << 8) |
                      bdm_core_bus_read8(core, (uint16_t)(address + 1u)));
}

/* Whole-machine save states.  ROM bytes are deliberately not embedded; the same
   BIOS/cart/media images should be loaded before calling load_state. */
#define BDM_STATE_VERSION 3u

static void st_w8(uint8_t **p, uint8_t v) { *(*p)++ = v; }
static void st_w16(uint8_t **p, uint16_t v) { uint8_t *q = *p; q[0] = (uint8_t)v; q[1] = (uint8_t)(v >> 8); *p = q + 2; }
static void st_w32(uint8_t **p, uint32_t v) { uint8_t *q = *p; q[0] = (uint8_t)v; q[1] = (uint8_t)(v >> 8); q[2] = (uint8_t)(v >> 16); q[3] = (uint8_t)(v >> 24); *p = q + 4; }
static void st_w64(uint8_t **p, uint64_t v) { uint8_t *q = *p; for (unsigned i = 0; i < 8u; ++i) q[i] = (uint8_t)(v >> (i * 8u)); *p = q + 8; }
static void st_wmem(uint8_t **p, const void *src, size_t n) { memcpy(*p, src, n); *p += n; }

static int st_need(const uint8_t *p, const uint8_t *end, size_t n) { return p <= end && (size_t)(end - p) >= n; }
static int st_r8(const uint8_t **p, const uint8_t *end, uint8_t *v) { if (!st_need(*p, end, 1u)) return 0; *v = *(*p)++; return 1; }
static int st_r16(const uint8_t **p, const uint8_t *end, uint16_t *v) { if (!st_need(*p, end, 2u)) return 0; const uint8_t *q = *p; *v = (uint16_t)((uint16_t)q[0] | ((uint16_t)q[1] << 8)); *p = q + 2; return 1; }
static int st_r32(const uint8_t **p, const uint8_t *end, uint32_t *v) { if (!st_need(*p, end, 4u)) return 0; const uint8_t *q = *p; *v = (uint32_t)q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24); *p = q + 4; return 1; }
static int st_r64(const uint8_t **p, const uint8_t *end, uint64_t *v) { if (!st_need(*p, end, 8u)) return 0; const uint8_t *q = *p; uint64_t x = 0; for (unsigned i = 0; i < 8u; ++i) x |= (uint64_t)q[i] << (i * 8u); *v = x; *p = q + 8; return 1; }
static int st_rmem(const uint8_t **p, const uint8_t *end, void *dst, size_t n) { if (!st_need(*p, end, n)) return 0; memcpy(dst, *p, n); *p += n; return 1; }

static size_t core_state_fixed_size(void) {
    return 4u + 4u + 4u +
           4u + 4u + 4u + 1u + 1u + 1u +
           4u + 4u + 1u +
           8u + 2u * 8u + 2u + 2u + 2u + 1u + 1u + 2u + 2u + 1u +
           BDM_EXT_RAM_SIZE + BDM_IRAM_SIZE + BDM_IO_SIZE +
           1u + 1u + 2u + 2u + 1u + 4u +
           2u * (1u + 1u + 1u + 1u + 1u + 4u) +
           1u + 2u * 8u + 1u + 1u + 1u + 4u +
           4u + 4u + 4u;
}

size_t bdm_core_state_size(const bdm_core_t *core) {
    if (!core) return 0u;
    return core_state_fixed_size() +
           bdm_video_state_size(core->video) +
           bdm_input_state_size(core->input) +
           bdm_sound_state_size(core->sound);
}

size_t bdm_core_save_state(const bdm_core_t *core, void *out_data, size_t out_capacity) {
    if (!core) return 0u;
    size_t video_sz = bdm_video_state_size(core->video);
    size_t input_sz = bdm_input_state_size(core->input);
    size_t sound_sz = bdm_sound_state_size(core->sound);
    size_t need = core_state_fixed_size() + video_sz + input_sz + sound_sz;
    if (!out_data || out_capacity < need) return need;

    uint8_t *p = (uint8_t *)out_data;
    memcpy(p, "BDMZ", 4); p += 4;
    st_w32(&p, BDM_STATE_VERSION);
    st_w32(&p, (uint32_t)need);

    st_w32(&p, (uint32_t)core->cart_size);
    st_w32(&p, (uint32_t)core->media_size);
    st_w32(&p, (uint32_t)core->bios_size);
    st_w8(&p, core->cart_loaded ? 1u : 0u);
    st_w8(&p, core->media_loaded ? 1u : 0u);
    st_w8(&p, core->bios_loaded ? 1u : 0u);
    st_w32(&p, core->cart_bank);
    st_w32(&p, core->media_bank);
    st_w8(&p, core->media_selected ? 1u : 0u);

    st_w64(&p, core->cpu.steps);
    for (unsigned i = 0; i < 8u; ++i) st_w16(&p, core->cpu.r[i]);
    st_w16(&p, core->cpu.pc);
    st_w16(&p, core->cpu.last_pc);
    st_w16(&p, core->cpu.last_opcode);
    st_w8(&p, core->cpu.ccr);
    st_w8(&p, core->cpu.stopped ? 1u : 0u);
    st_w16(&p, core->cpu.unsupported_pc);
    st_w16(&p, core->cpu.unsupported_opcode);
    st_w8(&p, core->cpu.unsupported ? 1u : 0u);

    st_wmem(&p, core->ext_ram, sizeof(core->ext_ram));
    st_wmem(&p, core->internal_ram, sizeof(core->internal_ram));
    st_wmem(&p, core->io, sizeof(core->io));

    st_w8(&p, core->timer16_tier);
    st_w8(&p, core->timer16_tsr);
    st_w16(&p, core->timer16_tcnt);
    st_w16(&p, core->timer16_ocra);
    st_w8(&p, core->timer16_tcr);
    st_w32(&p, core->timer16_prescale_accum);
    for (unsigned ch = 0; ch < 2u; ++ch) {
        st_w8(&p, core->timer8_tcr[ch]);
        st_w8(&p, core->timer8_tcsr[ch]);
        st_w8(&p, core->timer8_tcora[ch]);
        st_w8(&p, core->timer8_tcorb[ch]);
        st_w8(&p, core->timer8_tcnt[ch]);
        st_w32(&p, core->timer8_prescale_accum[ch]);
    }
    st_w8(&p, core->port6_drive);
    for (unsigned i = 0; i < 8u; ++i) st_w16(&p, core->adc_value[i]);
    st_w8(&p, core->adc_adcsr);
    st_w8(&p, core->adc_adcr);
    st_w8(&p, core->adc_pending_control);
    st_w32(&p, core->adc_delay_steps);

    st_w32(&p, (uint32_t)video_sz);
    bdm_video_save_state(core->video, p, video_sz); p += video_sz;
    st_w32(&p, (uint32_t)input_sz);
    bdm_input_save_state(core->input, p, input_sz); p += input_sz;
    st_w32(&p, (uint32_t)sound_sz);
    bdm_sound_save_state(core->sound, p, sound_sz); p += sound_sz;

    return need;
}

bdm_status_t bdm_core_load_state(bdm_core_t *core, const void *data, size_t size) {
    if (!core || !data) return BDM_ERR_INVALID_ARGUMENT;
    const uint8_t *p = (const uint8_t *)data;
    const uint8_t *end = p + size;
    uint32_t version = 0, total = 0;
    if (!st_need(p, end, 4u) || memcmp(p, "BDMZ", 4) != 0) return BDM_ERR_BAD_ROM;
    p += 4;
    if (!st_r32(&p, end, &version) || version != BDM_STATE_VERSION) return BDM_ERR_BAD_ROM;
    if (!st_r32(&p, end, &total) || total > size || total < core_state_fixed_size()) return BDM_ERR_BAD_ROM;

    uint32_t cart_size = 0, media_size = 0, bios_size = 0;
    uint8_t cart_loaded = 0, media_loaded = 0, bios_loaded = 0, media_selected = 0;
    uint32_t cart_bank = 0, media_bank = 0;
    if (!st_r32(&p, end, &cart_size) || !st_r32(&p, end, &media_size) || !st_r32(&p, end, &bios_size) ||
        !st_r8(&p, end, &cart_loaded) || !st_r8(&p, end, &media_loaded) || !st_r8(&p, end, &bios_loaded) ||
        !st_r32(&p, end, &cart_bank) || !st_r32(&p, end, &media_bank) || !st_r8(&p, end, &media_selected)) return BDM_ERR_BAD_ROM;

    if ((cart_loaded != 0u) != core->cart_loaded || (media_loaded != 0u) != core->media_loaded || (bios_loaded != 0u) != core->bios_loaded) return BDM_ERR_BAD_ROM;
    if (core->cart_loaded && cart_size != core->cart_size) return BDM_ERR_BAD_ROM;
    if (core->media_loaded && media_size != core->media_size) return BDM_ERR_BAD_ROM;
    if (core->bios_loaded && bios_size != core->bios_size) return BDM_ERR_BAD_ROM;

    uint64_t cpu_steps = 0;
    if (!st_r64(&p, end, &cpu_steps)) return BDM_ERR_BAD_ROM;
    core->cpu.steps = cpu_steps;
    for (unsigned i = 0; i < 8u; ++i) if (!st_r16(&p, end, &core->cpu.r[i])) return BDM_ERR_BAD_ROM;
    if (!st_r16(&p, end, &core->cpu.pc) || !st_r16(&p, end, &core->cpu.last_pc) || !st_r16(&p, end, &core->cpu.last_opcode) ||
        !st_r8(&p, end, &core->cpu.ccr)) return BDM_ERR_BAD_ROM;
    uint8_t stopped = 0, unsupported = 0;
    if (!st_r8(&p, end, &stopped) || !st_r16(&p, end, &core->cpu.unsupported_pc) ||
        !st_r16(&p, end, &core->cpu.unsupported_opcode) || !st_r8(&p, end, &unsupported)) return BDM_ERR_BAD_ROM;
    core->cpu.stopped = stopped != 0u;
    core->cpu.unsupported = unsupported != 0u;

    if (!st_rmem(&p, end, core->ext_ram, sizeof(core->ext_ram)) ||
        !st_rmem(&p, end, core->internal_ram, sizeof(core->internal_ram)) ||
        !st_rmem(&p, end, core->io, sizeof(core->io))) return BDM_ERR_BAD_ROM;

    uint32_t tmp32 = 0;
    if (!st_r8(&p, end, &core->timer16_tier) || !st_r8(&p, end, &core->timer16_tsr) ||
        !st_r16(&p, end, &core->timer16_tcnt) || !st_r16(&p, end, &core->timer16_ocra) ||
        !st_r8(&p, end, &core->timer16_tcr) || !st_r32(&p, end, &tmp32)) return BDM_ERR_BAD_ROM;
    core->timer16_prescale_accum = (unsigned)tmp32;
    for (unsigned ch = 0; ch < 2u; ++ch) {
        if (!st_r8(&p, end, &core->timer8_tcr[ch]) || !st_r8(&p, end, &core->timer8_tcsr[ch]) ||
            !st_r8(&p, end, &core->timer8_tcora[ch]) || !st_r8(&p, end, &core->timer8_tcorb[ch]) ||
            !st_r8(&p, end, &core->timer8_tcnt[ch]) || !st_r32(&p, end, &tmp32)) return BDM_ERR_BAD_ROM;
        core->timer8_prescale_accum[ch] = (unsigned)tmp32;
    }
    if (!st_r8(&p, end, &core->port6_drive)) return BDM_ERR_BAD_ROM;
    for (unsigned i = 0; i < 8u; ++i) if (!st_r16(&p, end, &core->adc_value[i])) return BDM_ERR_BAD_ROM;
    if (!st_r8(&p, end, &core->adc_adcsr) || !st_r8(&p, end, &core->adc_adcr) ||
        !st_r8(&p, end, &core->adc_pending_control) || !st_r32(&p, end, &tmp32)) return BDM_ERR_BAD_ROM;
    core->adc_delay_steps = (unsigned)tmp32;

    core->cart_bank = (unsigned)cart_bank;
    core->media_bank = (unsigned)media_bank;
    core->media_selected = media_selected != 0u;

    uint32_t video_sz = 0, input_sz = 0, sound_sz = 0;
    if (!st_r32(&p, end, &video_sz) || !st_need(p, end, video_sz) || bdm_video_load_state(core->video, p, video_sz) != 0) return BDM_ERR_BAD_ROM;
    p += video_sz;
    if (!st_r32(&p, end, &input_sz) || !st_need(p, end, input_sz) || bdm_input_load_state(core->input, p, input_sz) != 0) return BDM_ERR_BAD_ROM;
    p += input_sz;
    if (!st_r32(&p, end, &sound_sz) || !st_need(p, end, sound_sz) || bdm_sound_load_state(core->sound, p, sound_sz) != 0) return BDM_ERR_BAD_ROM;
    p += sound_sz;

    /* Keep hardware shadow registers and VRAM-derived display coherent after restore. */
    word_to_io(core, 0xff92u, core->timer16_tcnt);
    word_to_io(core, 0xff94u, core->timer16_ocra);
    core->io[0xff90u - BDM_IO_BASE] = core->timer16_tier;
    core->io[0xff91u - BDM_IO_BASE] = core->timer16_tsr;
    core->io[0xff96u - BDM_IO_BASE] = core->timer16_tcr;
    core->io[0xffe8u - BDM_IO_BASE] = core->adc_adcsr;
    core->io[0xffe9u - BDM_IO_BASE] = core->adc_adcr;
    for (size_t i = 0; i < BDM_LCD_VRAM_SIZE && i < sizeof(core->ext_ram); ++i) {
        bdm_video_lcd_vram_write(core->video, i, core->ext_ram[i]);
    }
    (void)p;
    return BDM_OK;
}
