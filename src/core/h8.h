#ifndef BDM_H8_H
#define BDM_H8_H

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t (*h8_read8_fn)(void *opaque, uint16_t address);
typedef void (*h8_write8_fn)(void *opaque, uint16_t address, uint8_t value);

typedef struct h8_cpu {
    uint16_t r[8];
    uint16_t pc;
    uint8_t ccr;
    uint16_t last_pc;
    uint16_t last_opcode;
    uint64_t steps;
    bool stopped;
    bool unsupported;
    uint16_t unsupported_pc;
    uint16_t unsupported_opcode;
    void *bus_opaque;
    h8_read8_fn read8;
    h8_write8_fn write8;
} h8_cpu_t;

void h8_init(h8_cpu_t *cpu, void *bus_opaque, h8_read8_fn read8, h8_write8_fn write8);
void h8_reset(h8_cpu_t *cpu);
int h8_step(h8_cpu_t *cpu);
uint16_t h8_read16(h8_cpu_t *cpu, uint16_t address);
void h8_take_interrupt(h8_cpu_t *cpu, unsigned vector);

#endif
