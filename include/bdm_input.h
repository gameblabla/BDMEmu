#ifndef BDM_INPUT_H
#define BDM_INPUT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bdm_input bdm_input_t;

typedef enum bdm_button {
    BDM_BUTTON_PEN = 0,
    BDM_BUTTON_A,
    BDM_BUTTON_B,
    BDM_BUTTON_START,
    BDM_BUTTON_SELECT,
    BDM_BUTTON_COUNT
} bdm_button_t;

bdm_input_t *bdm_input_create(void);
void bdm_input_destroy(bdm_input_t *input);
void bdm_input_reset(bdm_input_t *input);
void bdm_input_set_button(bdm_input_t *input, bdm_button_t button, bool pressed);
void bdm_input_set_pen(bdm_input_t *input, int x, int y, bool down);
void bdm_input_set_pen_fp(bdm_input_t *input, int32_t x_fp, int32_t y_fp, bool down);
void bdm_input_set_port7_override(bdm_input_t *input, uint8_t value);
void bdm_input_clear_port7_override(bdm_input_t *input);
int bdm_input_pen_x(const bdm_input_t *input);
int bdm_input_pen_y(const bdm_input_t *input);
int32_t bdm_input_pen_x_fp(const bdm_input_t *input);
int32_t bdm_input_pen_y_fp(const bdm_input_t *input);
bool bdm_input_pen_down(const bdm_input_t *input);
uint8_t bdm_input_read_port7(const bdm_input_t *input);

/* Opaque subsystem snapshot.  Used by bdm_core save states. */
size_t bdm_input_state_size(const bdm_input_t *input);
size_t bdm_input_save_state(const bdm_input_t *input, void *out_data, size_t out_capacity);
int bdm_input_load_state(bdm_input_t *input, const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
