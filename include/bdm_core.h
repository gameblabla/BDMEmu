#ifndef BDM_CORE_H
#define BDM_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bdm_input.h"
#include "bdm_sound.h"
#include "bdm_video.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bdm_core bdm_core_t;

typedef enum bdm_status {
    BDM_OK = 0,
    BDM_ERR_INVALID_ARGUMENT = -1,
    BDM_ERR_OUT_OF_MEMORY = -2,
    BDM_ERR_BAD_ROM = -3,
    BDM_ERR_UNSUPPORTED_OPCODE = 1
} bdm_status_t;

typedef struct bdm_core_config {
    bdm_video_t *video;
    bdm_input_t *input;
    bdm_sound_t *sound;
} bdm_core_config_t;

typedef struct bdm_core_state {
    uint64_t steps;
    uint16_t pc;
    uint16_t last_pc;
    uint16_t last_opcode;
    uint16_t r[8];
    uint8_t ccr;
    bool stopped;
    bool unsupported;
    uint16_t unsupported_pc;
    uint16_t unsupported_opcode;
    unsigned cart_bank;
    unsigned media_bank;
    bool media_selected;
    uint16_t timer16_counter;
    uint16_t timer16_compare;
    uint8_t timer16_tier;
    uint8_t timer16_tsr;
    uint8_t timer16_tcr;
    uint8_t panel_drive;
    uint8_t port7_value;
    uint8_t adc_status;
    uint8_t adc_control;
} bdm_core_state_t;

bdm_core_t *bdm_core_create(const bdm_core_config_t *config);
void bdm_core_destroy(bdm_core_t *core);
void bdm_core_reset(bdm_core_t *core);

bdm_status_t bdm_core_load_cart(bdm_core_t *core, const void *data, size_t size);
bdm_status_t bdm_core_load_media_cart(bdm_core_t *core, const void *data, size_t size);
bdm_status_t bdm_core_load_bios(bdm_core_t *core, const void *data, size_t size);

/* External HM62256 SRAM.  The top 0x480 bytes are hidden by H8 internal RAM/I/O,
   so the exposed external window is 0x8000-0xfb7f. */
bdm_status_t bdm_core_load_sram(bdm_core_t *core, const void *data, size_t size);
size_t bdm_core_save_sram(const bdm_core_t *core, void *out_data, size_t out_capacity);
size_t bdm_core_external_sram_size(const bdm_core_t *core);

/* Whole-machine save state.  ROM images are not embedded; load the same BIOS,
   game cart, and media cart before loading a state. */
size_t bdm_core_state_size(const bdm_core_t *core);
size_t bdm_core_save_state(const bdm_core_t *core, void *out_data, size_t out_capacity);
bdm_status_t bdm_core_load_state(bdm_core_t *core, const void *data, size_t size);

bdm_status_t bdm_core_step(bdm_core_t *core);
uint64_t bdm_core_run_steps(bdm_core_t *core, uint64_t max_steps, bool stop_on_unsupported);
void bdm_core_get_state(const bdm_core_t *core, bdm_core_state_t *out_state);

uint8_t bdm_core_bus_read8(const bdm_core_t *core, uint16_t address);
uint16_t bdm_core_bus_read16(const bdm_core_t *core, uint16_t address);

#ifdef __cplusplus
}
#endif

#endif
