#include "h8.h"

#include <stddef.h>
#include <string.h>

#define F_I  0x80u
#define F_UI 0x40u
#define F_H  0x20u
#define F_U  0x10u
#define F_N  0x08u
#define F_Z  0x04u
#define F_V  0x02u
#define F_C  0x01u

static int8_t sx8(uint8_t v) { return (int8_t)v; }
static int16_t sx16(uint16_t v) { return (int16_t)v; }

static uint8_t rb(h8_cpu_t *cpu, uint16_t address) {
    return cpu->read8(cpu->bus_opaque, address);
}

static void wb(h8_cpu_t *cpu, uint16_t address, uint8_t value) {
    cpu->write8(cpu->bus_opaque, address, value);
}

uint16_t h8_read16(h8_cpu_t *cpu, uint16_t address) {
    address &= (uint16_t)~1u;
    return (uint16_t)(((uint16_t)rb(cpu, address) << 8) | rb(cpu, (uint16_t)(address + 1u)));
}

static void ww(h8_cpu_t *cpu, uint16_t address, uint16_t value) {
    address &= (uint16_t)~1u;
    wb(cpu, address, (uint8_t)(value >> 8));
    wb(cpu, (uint16_t)(address + 1u), (uint8_t)value);
}

static uint16_t fetch16(h8_cpu_t *cpu) {
    uint16_t value = h8_read16(cpu, cpu->pc);
    cpu->pc = (uint16_t)(cpu->pc + 2u);
    return value;
}

static uint8_t r8(const h8_cpu_t *cpu, unsigned reg) {
    reg &= 0x0fu;
    if (reg & 8u) return (uint8_t)(cpu->r[reg & 7u] & 0xffu);
    return (uint8_t)(cpu->r[reg & 7u] >> 8);
}

static void w8(h8_cpu_t *cpu, unsigned reg, uint8_t value) {
    reg &= 0x0fu;
    if (reg & 8u) cpu->r[reg & 7u] = (uint16_t)((cpu->r[reg & 7u] & 0xff00u) | value);
    else cpu->r[reg & 7u] = (uint16_t)((cpu->r[reg & 7u] & 0x00ffu) | ((uint16_t)value << 8));
}

static uint16_t r16(const h8_cpu_t *cpu, unsigned reg) {
    return cpu->r[reg & 7u];
}

static void w16(h8_cpu_t *cpu, unsigned reg, uint16_t value) {
    cpu->r[reg & 7u] = value;
}

static void set_nzv8(h8_cpu_t *cpu, uint8_t value) {
    cpu->ccr &= (uint8_t)~(F_N | F_Z | F_V);
    if (!value) cpu->ccr |= F_Z;
    if (value & 0x80u) cpu->ccr |= F_N;
}

static void set_nzv16(h8_cpu_t *cpu, uint16_t value) {
    cpu->ccr &= (uint8_t)~(F_N | F_Z | F_V);
    if (!value) cpu->ccr |= F_Z;
    if (value & 0x8000u) cpu->ccr |= F_N;
}

static uint8_t do_add8(h8_cpu_t *cpu, uint8_t a, uint8_t b) {
    unsigned res = (unsigned)a + (unsigned)b;
    uint8_t r = (uint8_t)res;
    cpu->ccr &= (uint8_t)~(F_H | F_N | F_Z | F_V | F_C);
    if (((a & 0x0fu) + (b & 0x0fu)) & 0x10u) cpu->ccr |= F_H;
    if (r & 0x80u) cpu->ccr |= F_N;
    if (!r) cpu->ccr |= F_Z;
    if ((uint8_t)(~(a ^ b) & (a ^ r) & 0x80u)) cpu->ccr |= F_V;
    if (res & 0x100u) cpu->ccr |= F_C;
    return r;
}

static uint16_t do_add16(h8_cpu_t *cpu, uint16_t a, uint16_t b) {
    unsigned res = (unsigned)a + (unsigned)b;
    uint16_t r = (uint16_t)res;
    cpu->ccr &= (uint8_t)~(F_H | F_N | F_Z | F_V | F_C);
    if (((a & 0x0fffu) + (b & 0x0fffu)) & 0x1000u) cpu->ccr |= F_H;
    if (r & 0x8000u) cpu->ccr |= F_N;
    if (!r) cpu->ccr |= F_Z;
    if ((uint16_t)(~(a ^ b) & (a ^ r) & 0x8000u)) cpu->ccr |= F_V;
    if (res & 0x10000u) cpu->ccr |= F_C;
    return r;
}

static uint8_t do_sub8(h8_cpu_t *cpu, uint8_t a, uint8_t b) {
    int res = (int)a - (int)b;
    uint8_t r = (uint8_t)res;
    cpu->ccr &= (uint8_t)~(F_H | F_N | F_Z | F_V | F_C);
    if (((a & 0x0fu) - (b & 0x0fu)) & 0x10u) cpu->ccr |= F_H;
    if (r & 0x80u) cpu->ccr |= F_N;
    if (!r) cpu->ccr |= F_Z;
    if ((uint8_t)((a ^ b) & (a ^ r) & 0x80u)) cpu->ccr |= F_V;
    if (res < 0) cpu->ccr |= F_C;
    return r;
}

static uint16_t do_sub16(h8_cpu_t *cpu, uint16_t a, uint16_t b) {
    int res = (int)a - (int)b;
    uint16_t r = (uint16_t)res;
    cpu->ccr &= (uint8_t)~(F_H | F_N | F_Z | F_V | F_C);
    if (((a & 0x0fffu) - (b & 0x0fffu)) & 0x1000u) cpu->ccr |= F_H;
    if (r & 0x8000u) cpu->ccr |= F_N;
    if (!r) cpu->ccr |= F_Z;
    if ((uint16_t)((a ^ b) & (a ^ r) & 0x8000u)) cpu->ccr |= F_V;
    if (res < 0) cpu->ccr |= F_C;
    return r;
}

static uint8_t do_addx8(h8_cpu_t *cpu, uint8_t a, uint8_t b) {
    uint8_t c = (cpu->ccr & F_C) ? 1u : 0u;
    unsigned res = (unsigned)a + (unsigned)b + c;
    uint8_t r = (uint8_t)res;
    cpu->ccr &= (uint8_t)~(F_N | F_V | F_C);
    if (((a & 0x0fu) + (b & 0x0fu) + c) & 0x10u) cpu->ccr |= F_H;
    else cpu->ccr &= (uint8_t)~F_H;
    if (r) cpu->ccr &= (uint8_t)~F_Z;
    if (r & 0x80u) cpu->ccr |= F_N;
    if ((uint8_t)(~(a ^ b) & (a ^ r) & 0x80u)) cpu->ccr |= F_V;
    if (res & 0x100u) cpu->ccr |= F_C;
    return r;
}

static uint8_t do_subx8(h8_cpu_t *cpu, uint8_t a, uint8_t b) {
    uint8_t c = (cpu->ccr & F_C) ? 1u : 0u;
    int res = (int)a - (int)b - (int)c;
    uint8_t r = (uint8_t)res;
    cpu->ccr &= (uint8_t)~(F_N | F_V | F_C);
    if (((a & 0x0fu) - (b & 0x0fu) - c) & 0x10u) cpu->ccr |= F_H;
    else cpu->ccr &= (uint8_t)~F_H;
    if (r) cpu->ccr &= (uint8_t)~F_Z;
    if (r & 0x80u) cpu->ccr |= F_N;
    if ((uint8_t)((a ^ b) & (a ^ r) & 0x80u)) cpu->ccr |= F_V;
    if (res < 0) cpu->ccr |= F_C;
    return r;
}


static void set_shift_nz8(h8_cpu_t *cpu, uint8_t value) {
    cpu->ccr &= (uint8_t)~(F_N | F_Z | F_V);
    if (!value) cpu->ccr |= F_Z;
    if (value & 0x80u) cpu->ccr |= F_N;
}

static unsigned shift_kind8(uint16_t op) {
    switch (op & 0xfff0u) {
    case 0x1000u: return 0x00u; /* SHLL */
    case 0x1080u: return 0x80u; /* SHAL */
    case 0x1100u: return 0x10u; /* SHLR */
    case 0x1180u: return 0x90u; /* SHAR */
    case 0x1200u: return 0x20u; /* ROTXL */
    case 0x1280u: return 0xa0u; /* ROTL */
    case 0x1300u: return 0x30u; /* ROTXR */
    case 0x1380u: return 0xb0u; /* ROTR */
    default: return 0xffu;
    }
}

static uint8_t do_shift8(h8_cpu_t *cpu, uint8_t value, unsigned kind) {
    uint8_t old_c = (cpu->ccr & F_C) ? 1u : 0u;
    uint8_t new_c = 0u;
    uint8_t r = value;
    switch (kind & 0xf0u) {
    case 0x00u: /* SHLL */
    case 0x80u: /* SHAL */
        new_c = (uint8_t)((value >> 7) & 1u);
        r = (uint8_t)(value << 1);
        break;
    case 0x10u: /* SHLR */
        new_c = (uint8_t)(value & 1u);
        r = (uint8_t)(value >> 1);
        break;
    case 0x90u: /* SHAR */
        new_c = (uint8_t)(value & 1u);
        r = (uint8_t)((value >> 1) | (value & 0x80u));
        break;
    case 0x20u: /* ROTXL */
        new_c = (uint8_t)((value >> 7) & 1u);
        r = (uint8_t)((value << 1) | old_c);
        break;
    case 0xa0u: /* ROTL */
        new_c = (uint8_t)((value >> 7) & 1u);
        r = (uint8_t)((value << 1) | new_c);
        break;
    case 0x30u: /* ROTXR */
        new_c = (uint8_t)(value & 1u);
        r = (uint8_t)((value >> 1) | (old_c << 7));
        break;
    case 0xb0u: /* ROTR */
        new_c = (uint8_t)(value & 1u);
        r = (uint8_t)((value >> 1) | (new_c << 7));
        break;
    default:
        break;
    }
    set_shift_nz8(cpu, r);
    if (new_c) cpu->ccr |= F_C;
    else cpu->ccr &= (uint8_t)~F_C;
    return r;
}

static uint8_t do_inc8(h8_cpu_t *cpu, uint8_t a, uint8_t b) {
    uint8_t r = (uint8_t)(a + b);
    cpu->ccr &= (uint8_t)~(F_N | F_Z | F_V);
    if (!r) cpu->ccr |= F_Z;
    else if (r & 0x80u) cpu->ccr |= F_N;
    if ((uint8_t)(~(a ^ b) & (a ^ r) & 0x80u)) cpu->ccr |= F_V;
    return r;
}

static uint8_t do_dec8(h8_cpu_t *cpu, uint8_t a, uint8_t b) {
    uint8_t r = (uint8_t)(a - b);
    cpu->ccr &= (uint8_t)~(F_N | F_Z | F_V);
    if (!r) cpu->ccr |= F_Z;
    else if (r & 0x80u) cpu->ccr |= F_N;
    if ((uint8_t)((a ^ b) & (a ^ r) & 0x80u)) cpu->ccr |= F_V;
    return r;
}

static void push16(h8_cpu_t *cpu, uint16_t value) {
    cpu->r[7] = (uint16_t)(cpu->r[7] - 2u);
    ww(cpu, cpu->r[7], value);
}

static uint16_t pop16(h8_cpu_t *cpu) {
    uint16_t v = h8_read16(cpu, cpu->r[7]);
    cpu->r[7] = (uint16_t)(cpu->r[7] + 2u);
    return v;
}

static bool branch_condition(const h8_cpu_t *cpu, unsigned code) {
    uint8_t c = cpu->ccr;
    switch (code & 0x0fu) {
    case 0x0: return true;
    case 0x1: return false;
    case 0x2: return !(c & (F_C | F_Z));
    case 0x3: return !!(c & (F_C | F_Z));
    case 0x4: return !(c & F_C);
    case 0x5: return !!(c & F_C);
    case 0x6: return !(c & F_Z);
    case 0x7: return !!(c & F_Z);
    case 0x8: return !(c & F_V);
    case 0x9: return !!(c & F_V);
    case 0xa: return !(c & F_N);
    case 0xb: return !!(c & F_N);
    case 0xc: return !(((c & (F_N | F_V)) == F_N) || ((c & (F_N | F_V)) == F_V));
    case 0xd: return  (((c & (F_N | F_V)) == F_N) || ((c & (F_N | F_V)) == F_V));
    case 0xe: return !((c & F_Z) || ((c & (F_N | F_V)) == F_N) || ((c & (F_N | F_V)) == F_V));
    default:  return  ((c & F_Z) || ((c & (F_N | F_V)) == F_N) || ((c & (F_N | F_V)) == F_V));
    }
}

static bool bit_op_apply(h8_cpu_t *cpu, uint16_t op, uint8_t *value, bool allow_write) {
    unsigned kind;
    unsigned bit;
    if ((op & 0xff00u) >= 0x6000u && (op & 0xff00u) <= 0x6300u) {
        kind = op & 0xff00u;
        bit = r8(cpu, (op >> 4) & 0x0fu) & 7u;
    } else {
        kind = op & 0xff80u;
        bit = (op >> 4) & 7u;
    }
    uint8_t mask = (uint8_t)(1u << bit);
    bool b = (*value & mask) != 0;
    bool c = (cpu->ccr & F_C) != 0;

    switch (kind) {
    case 0x6000u: if (!allow_write) return false; *value |= mask; break;
    case 0x6100u: if (!allow_write) return false; *value ^= mask; break;
    case 0x6200u: if (!allow_write) return false; *value &= (uint8_t)~mask; break;
    case 0x6300u: if (b) cpu->ccr &= (uint8_t)~F_Z; else cpu->ccr |= F_Z; break;
    case 0x6700u: if (!allow_write) return false; if (c) *value |= mask; else *value &= (uint8_t)~mask; break;
    case 0x6780u: if (!allow_write) return false; if (!c) *value |= mask; else *value &= (uint8_t)~mask; break;
    case 0x7000u: if (!allow_write) return false; *value |= mask; break;
    case 0x7100u: if (!allow_write) return false; *value ^= mask; break;
    case 0x7200u: if (!allow_write) return false; *value &= (uint8_t)~mask; break;
    case 0x7300u: if (b) cpu->ccr &= (uint8_t)~F_Z; else cpu->ccr |= F_Z; break;
    case 0x7400u: if (c || b) cpu->ccr |= F_C; else cpu->ccr &= (uint8_t)~F_C; break;
    case 0x7480u: if (c || !b) cpu->ccr |= F_C; else cpu->ccr &= (uint8_t)~F_C; break;
    case 0x7500u: if (c ^ b) cpu->ccr |= F_C; else cpu->ccr &= (uint8_t)~F_C; break;
    case 0x7580u: if (c ^ !b) cpu->ccr |= F_C; else cpu->ccr &= (uint8_t)~F_C; break;
    case 0x7600u: if (c && b) cpu->ccr |= F_C; else cpu->ccr &= (uint8_t)~F_C; break;
    case 0x7680u: if (c && !b) cpu->ccr |= F_C; else cpu->ccr &= (uint8_t)~F_C; break;
    case 0x7700u: if (b) cpu->ccr |= F_C; else cpu->ccr &= (uint8_t)~F_C; break;
    case 0x7780u: if (!b) cpu->ccr |= F_C; else cpu->ccr &= (uint8_t)~F_C; break;
    default: return false;
    }
    return true;
}

static void bit_op_reg(h8_cpu_t *cpu, uint16_t op, uint8_t *value, bool write_back) {
    uint8_t old = *value;
    if (!bit_op_apply(cpu, op, value, true)) {
        cpu->unsupported = true;
        cpu->unsupported_pc = cpu->last_pc;
        cpu->unsupported_opcode = op;
        return;
    }
    if (write_back && *value != old) w8(cpu, op & 0x0fu, *value);
}

static void mark_unsupported(h8_cpu_t *cpu, uint16_t pc, uint16_t opcode) {
    cpu->unsupported = true;
    cpu->unsupported_pc = pc;
    cpu->unsupported_opcode = opcode;
}

void h8_init(h8_cpu_t *cpu, void *bus_opaque, h8_read8_fn read8, h8_write8_fn write8) {
    if (!cpu) return;
    memset(cpu, 0, sizeof(*cpu));
    cpu->bus_opaque = bus_opaque;
    cpu->read8 = read8;
    cpu->write8 = write8;
}

void h8_reset(h8_cpu_t *cpu) {
    if (!cpu) return;
    memset(cpu->r, 0, sizeof(cpu->r));
    cpu->ccr = F_I;
    cpu->pc = h8_read16(cpu, 0);
    cpu->last_pc = cpu->pc;
    cpu->last_opcode = 0;
    cpu->steps = 0;
    cpu->stopped = false;
    cpu->unsupported = false;
    cpu->unsupported_pc = 0;
    cpu->unsupported_opcode = 0;
}


void h8_take_interrupt(h8_cpu_t *cpu, unsigned vector) {
    if (!cpu || cpu->stopped || cpu->unsupported) return;
    push16(cpu, cpu->pc);
    push16(cpu, (uint16_t)((uint16_t)cpu->ccr << 8));
    cpu->ccr |= F_I;
    cpu->pc = h8_read16(cpu, (uint16_t)(2u * (vector & 0x7fu)));
}

int h8_step(h8_cpu_t *cpu) {
    if (!cpu || cpu->stopped || cpu->unsupported) return -1;

    uint16_t pc0 = cpu->pc;
    uint16_t op = fetch16(cpu);
    cpu->last_pc = pc0;
    cpu->last_opcode = op;
    cpu->steps++;

    if (op == 0x0000u) {
        return 0; /* nop */
    } else if ((op & 0xfff0u) == 0x0200u) {
        w8(cpu, op & 0x0fu, cpu->ccr);
        return 0;
    } else if ((op & 0xfff0u) == 0x0300u) {
        cpu->ccr = r8(cpu, op & 0x0fu);
        return 0;
    } else if ((op & 0xff00u) == 0x0400u) {
        cpu->ccr |= (uint8_t)op;
        return 0;
    } else if ((op & 0xff00u) == 0x0500u) {
        cpu->ccr ^= (uint8_t)op;
        return 0;
    } else if ((op & 0xff00u) == 0x0600u) {
        cpu->ccr &= (uint8_t)op;
        return 0;
    } else if ((op & 0xff00u) == 0x0700u) {
        cpu->ccr = (uint8_t)op;
        return 0;
    } else if ((op & 0xf000u) == 0x2000u) {
        uint8_t v = rb(cpu, (uint16_t)(0xff00u | (op & 0xffu)));
        set_nzv8(cpu, v);
        w8(cpu, (op >> 8) & 0x0fu, v);
        return 0;
    } else if ((op & 0xf000u) == 0x3000u) {
        uint8_t v = r8(cpu, (op >> 8) & 0x0fu);
        set_nzv8(cpu, v);
        wb(cpu, (uint16_t)(0xff00u | (op & 0xffu)), v);
        return 0;
    } else if ((op & 0xff00u) >= 0x4000u && (op & 0xff00u) <= 0x4f00u) {
        if (branch_condition(cpu, (op >> 8) & 0x0fu)) {
            cpu->pc = (uint16_t)(cpu->pc + sx8((uint8_t)op));
        }
        return 0;
    } else if ((op & 0xfff0u) == 0x7900u) {
        uint16_t imm = fetch16(cpu);
        w16(cpu, op & 7u, imm);
        set_nzv16(cpu, imm);
        return 0;
    } else if ((op & 0xfff0u) == 0x7910u) {
        unsigned r = op & 7u;
        w16(cpu, r, do_add16(cpu, r16(cpu, r), fetch16(cpu)));
        return 0;
    } else if ((op & 0xfff0u) == 0x7920u) {
        unsigned r = op & 7u;
        do_sub16(cpu, r16(cpu, r), fetch16(cpu));
        return 0;
    } else if ((op & 0xfff0u) == 0x7930u) {
        unsigned r = op & 7u;
        w16(cpu, r, do_sub16(cpu, r16(cpu, r), fetch16(cpu)));
        return 0;
    } else if ((op & 0xfff0u) == 0x7940u) {
        unsigned r = op & 7u;
        uint16_t v = (uint16_t)(r16(cpu, r) | fetch16(cpu));
        w16(cpu, r, v); set_nzv16(cpu, v);
        return 0;
    } else if ((op & 0xfff0u) == 0x7950u) {
        unsigned r = op & 7u;
        uint16_t v = (uint16_t)(r16(cpu, r) ^ fetch16(cpu));
        w16(cpu, r, v); set_nzv16(cpu, v);
        return 0;
    } else if ((op & 0xfff0u) == 0x7960u) {
        unsigned r = op & 7u;
        uint16_t v = (uint16_t)(r16(cpu, r) & fetch16(cpu));
        w16(cpu, r, v); set_nzv16(cpu, v);
        return 0;
    } else if ((op & 0xff00u) >= 0x6000u && (op & 0xff00u) <= 0x6300u) {
        uint8_t v = r8(cpu, op & 0x0fu);
        bit_op_reg(cpu, op, &v, true);
        return cpu->unsupported ? 1 : 0;
    } else if ((op & 0xfff0u) == 0x6a00u) {
        uint16_t addr = fetch16(cpu);
        uint8_t v = rb(cpu, addr);
        set_nzv8(cpu, v);
        w8(cpu, op & 0x0fu, v);
        return 0;
    } else if ((op & 0xfff0u) == 0x6a80u) {
        uint16_t addr = fetch16(cpu);
        uint8_t v = r8(cpu, op & 0x0fu);
        set_nzv8(cpu, v);
        wb(cpu, addr, v);
        return 0;
    } else if ((op & 0xfff0u) == 0x6b00u) {
        uint16_t addr = fetch16(cpu);
        uint16_t v = h8_read16(cpu, addr);
        set_nzv16(cpu, v);
        w16(cpu, op & 7u, v);
        return 0;
    } else if ((op & 0xfff0u) == 0x6b80u) {
        uint16_t addr = fetch16(cpu);
        uint16_t v = r16(cpu, op & 7u);
        set_nzv16(cpu, v);
        ww(cpu, addr, v);
        return 0;
    } else if ((op & 0xff80u) == 0x6800u) {
        uint8_t v = rb(cpu, r16(cpu, (op >> 4) & 7u));
        set_nzv8(cpu, v);
        w8(cpu, op & 0x0fu, v);
        return 0;
    } else if ((op & 0xff80u) == 0x6880u) {
        uint8_t v = r8(cpu, op & 0x0fu);
        set_nzv8(cpu, v);
        wb(cpu, r16(cpu, (op >> 4) & 7u), v);
        return 0;
    } else if ((op & 0xff88u) == 0x6900u) {
        uint16_t v = h8_read16(cpu, r16(cpu, (op >> 4) & 7u));
        set_nzv16(cpu, v);
        w16(cpu, op & 7u, v);
        return 0;
    } else if ((op & 0xff88u) == 0x6980u) {
        uint16_t v = r16(cpu, op & 7u);
        set_nzv16(cpu, v);
        ww(cpu, r16(cpu, (op >> 4) & 7u), v);
        return 0;
    } else if ((op & 0xff80u) == 0x6c00u) {
        unsigned sr = (op >> 4) & 7u;
        uint16_t addr = r16(cpu, sr);
        uint8_t v = rb(cpu, addr);
        w16(cpu, sr, (uint16_t)(addr + 1u));
        set_nzv8(cpu, v);
        w8(cpu, op & 0x0fu, v);
        return 0;
    } else if ((op & 0xff80u) == 0x6c80u) {
        unsigned dr = (op >> 4) & 7u;
        uint16_t addr = (uint16_t)(r16(cpu, dr) - 1u);
        uint8_t v = r8(cpu, op & 0x0fu);
        w16(cpu, dr, addr);
        set_nzv8(cpu, v);
        wb(cpu, addr, v);
        return 0;
    } else if ((op & 0xff88u) == 0x6d00u) {
        unsigned sr = (op >> 4) & 7u;
        uint16_t addr = r16(cpu, sr);
        uint16_t v = h8_read16(cpu, addr);
        w16(cpu, sr, (uint16_t)(addr + 2u));
        set_nzv16(cpu, v);
        w16(cpu, op & 7u, v);
        return 0;
    } else if ((op & 0xff88u) == 0x6d80u) {
        unsigned dr = (op >> 4) & 7u;
        uint16_t addr = (uint16_t)(r16(cpu, dr) - 2u);
        uint16_t v = r16(cpu, op & 7u);
        w16(cpu, dr, addr);
        set_nzv16(cpu, v);
        ww(cpu, addr, v);
        return 0;
    } else if ((op & 0xff80u) == 0x6e00u) {
        uint16_t addr = (uint16_t)(r16(cpu, (op >> 4) & 7u) + sx16(fetch16(cpu)));
        uint8_t v = rb(cpu, addr);
        set_nzv8(cpu, v);
        w8(cpu, op & 0x0fu, v);
        return 0;
    } else if ((op & 0xff80u) == 0x6e80u) {
        uint16_t addr = (uint16_t)(r16(cpu, (op >> 4) & 7u) + sx16(fetch16(cpu)));
        uint8_t v = r8(cpu, op & 0x0fu);
        set_nzv8(cpu, v);
        wb(cpu, addr, v);
        return 0;
    } else if ((op & 0xff80u) == 0x6f00u) {
        uint16_t addr = (uint16_t)(r16(cpu, (op >> 4) & 7u) + sx16(fetch16(cpu)));
        uint16_t v = h8_read16(cpu, addr);
        set_nzv16(cpu, v);
        w16(cpu, op & 7u, v);
        return 0;
    } else if ((op & 0xff80u) == 0x6f80u) {
        uint16_t addr = (uint16_t)(r16(cpu, (op >> 4) & 7u) + sx16(fetch16(cpu)));
        uint16_t v = r16(cpu, op & 7u);
        set_nzv16(cpu, v);
        ww(cpu, addr, v);
        return 0;
    } else if (((op & 0xfff0u) == 0x1000u) || ((op & 0xfff0u) == 0x1080u) ||
               ((op & 0xfff0u) == 0x1100u) || ((op & 0xfff0u) == 0x1180u) ||
               ((op & 0xfff0u) == 0x1200u) || ((op & 0xfff0u) == 0x1280u) ||
               ((op & 0xfff0u) == 0x1300u) || ((op & 0xfff0u) == 0x1380u)) {
        unsigned d = op & 0x0fu;
        unsigned kind = shift_kind8(op);
        if (kind == 0xffu) { mark_unsupported(cpu, pc0, op); return 1; }
        w8(cpu, d, do_shift8(cpu, r8(cpu, d), kind));
        return 0;
    } else if ((op & 0xff00u) == 0x0c00u) {
        uint8_t v = r8(cpu, (op >> 4) & 0x0fu);
        set_nzv8(cpu, v);
        w8(cpu, op & 0x0fu, v);
        return 0;
    } else if ((op & 0xff00u) == 0x0d00u) {
        uint16_t v = r16(cpu, (op >> 4) & 7u);
        set_nzv16(cpu, v);
        w16(cpu, op & 7u, v);
        return 0;
    } else if ((op & 0xff00u) == 0x0800u) {
        unsigned d = op & 0x0fu;
        w8(cpu, d, do_add8(cpu, r8(cpu, d), r8(cpu, (op >> 4) & 0x0fu)));
        return 0;
    } else if ((op & 0xff00u) == 0x0900u) {
        unsigned d = op & 7u;
        w16(cpu, d, do_add16(cpu, r16(cpu, d), r16(cpu, (op >> 4) & 7u)));
        return 0;
    } else if ((op & 0xfff0u) == 0x0a00u) {
        unsigned d = op & 0x0fu;
        w8(cpu, d, do_inc8(cpu, r8(cpu, d), 1));
        return 0;
    } else if ((op & 0xfff8u) == 0x0b00u) {
        unsigned d = op & 7u; w16(cpu, d, (uint16_t)(r16(cpu, d) + 1u)); return 0;
    } else if ((op & 0xfff8u) == 0x0b80u) {
        unsigned d = op & 7u; w16(cpu, d, (uint16_t)(r16(cpu, d) + 2u)); return 0;
    } else if ((op & 0xfff8u) == 0x0b90u) {
        unsigned d = op & 7u; w16(cpu, d, (uint16_t)(r16(cpu, d) + 4u)); return 0;
    } else if ((op & 0xff00u) == 0x1400u) {
        unsigned d = op & 0x0fu;
        uint8_t v = (uint8_t)(r8(cpu, d) | r8(cpu, (op >> 4) & 0x0fu));
        w8(cpu, d, v); set_nzv8(cpu, v); return 0;
    } else if ((op & 0xff00u) == 0x1500u) {
        unsigned d = op & 0x0fu;
        uint8_t v = (uint8_t)(r8(cpu, d) ^ r8(cpu, (op >> 4) & 0x0fu));
        w8(cpu, d, v); set_nzv8(cpu, v); return 0;
    } else if ((op & 0xff00u) == 0x1600u) {
        unsigned d = op & 0x0fu;
        uint8_t v = (uint8_t)(r8(cpu, d) & r8(cpu, (op >> 4) & 0x0fu));
        w8(cpu, d, v); set_nzv8(cpu, v); return 0;
    } else if ((op & 0xfff0u) == 0x1700u) {
        unsigned d = op & 0x0fu;
        uint8_t v = (uint8_t)~r8(cpu, d);
        w8(cpu, d, v); set_nzv8(cpu, v); return 0;
    } else if ((op & 0xff00u) == 0x1800u) {
        unsigned d = op & 0x0fu;
        w8(cpu, d, do_sub8(cpu, r8(cpu, d), r8(cpu, (op >> 4) & 0x0fu)));
        return 0;
    } else if ((op & 0xff00u) == 0x1900u) {
        unsigned d = op & 7u;
        w16(cpu, d, do_sub16(cpu, r16(cpu, d), r16(cpu, (op >> 4) & 7u)));
        return 0;
    } else if ((op & 0xfff0u) == 0x1a00u) {
        unsigned d = op & 0x0fu;
        w8(cpu, d, do_dec8(cpu, r8(cpu, d), 1));
        return 0;
    } else if ((op & 0xfff8u) == 0x1b00u) {
        unsigned d = op & 7u; w16(cpu, d, (uint16_t)(r16(cpu, d) - 1u)); return 0;
    } else if ((op & 0xfff8u) == 0x1b80u) {
        unsigned d = op & 7u; w16(cpu, d, (uint16_t)(r16(cpu, d) - 2u)); return 0;
    } else if ((op & 0xfff8u) == 0x1b90u) {
        unsigned d = op & 7u; w16(cpu, d, (uint16_t)(r16(cpu, d) - 4u)); return 0;
    } else if ((op & 0xff00u) == 0x1c00u) {
        do_sub8(cpu, r8(cpu, op & 0x0fu), r8(cpu, (op >> 4) & 0x0fu)); return 0;
    } else if ((op & 0xff00u) == 0x1d00u) {
        do_sub16(cpu, r16(cpu, op & 7u), r16(cpu, (op >> 4) & 7u)); return 0;
    } else if ((op & 0xff00u) == 0x0e00u) {
        unsigned d = op & 0x0fu;
        w8(cpu, d, do_addx8(cpu, r8(cpu, d), r8(cpu, (op >> 4) & 0x0fu)));
        return 0;
    } else if ((op & 0xff00u) == 0x1e00u) {
        unsigned d = op & 0x0fu;
        w8(cpu, d, do_subx8(cpu, r8(cpu, d), r8(cpu, (op >> 4) & 0x0fu)));
        return 0;
    } else if ((op & 0xff00u) == 0x5000u) {
        unsigned d = op & 7u;
        w16(cpu, d, (uint16_t)((unsigned)(r16(cpu, d) & 0xffu) * (unsigned)r8(cpu, (op >> 4) & 0x0fu)));
        return 0;
    } else if ((op & 0xff00u) == 0x5100u) {
        unsigned d = op & 7u;
        uint8_t divisor = r8(cpu, (op >> 4) & 0x0fu);
        uint16_t dividend = r16(cpu, d);
        cpu->ccr &= (uint8_t)~(F_N | F_Z | F_V | F_C);
        if (!divisor) {
            cpu->ccr |= F_V;
        } else {
            unsigned q = (unsigned)dividend / (unsigned)divisor;
            unsigned rem = (unsigned)dividend % (unsigned)divisor;
            if (q > 0xffu) {
                cpu->ccr |= F_V;
            } else {
                uint8_t qb = (uint8_t)q;
                if (!qb) cpu->ccr |= F_Z;
                if (qb & 0x80u) cpu->ccr |= F_N;
                w16(cpu, d, (uint16_t)((rem << 8) | qb));
            }
        }
        return 0;
    } else if (op == 0x5470u) {
        cpu->pc = pop16(cpu);
        return 0;
    } else if (op == 0x5670u) {
        uint16_t frame = pop16(cpu);
        cpu->ccr = (uint8_t)(frame >> 8);
        cpu->pc = pop16(cpu);
        return 0;
    } else if ((op & 0xff00u) == 0x5500u) {
        push16(cpu, cpu->pc);
        cpu->pc = (uint16_t)(cpu->pc + sx8((uint8_t)op));
        return 0;
    } else if ((op & 0xff8fu) == 0x5900u) {
        cpu->pc = r16(cpu, (op >> 4) & 7u);
        return 0;
    } else if ((op & 0xff8fu) == 0x5d00u) {
        uint16_t target = r16(cpu, (op >> 4) & 7u);
        push16(cpu, cpu->pc);
        cpu->pc = target;
        return 0;
    } else if (op == 0x5a00u) {
        cpu->pc = fetch16(cpu);
        return 0;
    } else if ((op & 0xff00u) == 0x5b00u) {
        cpu->pc = h8_read16(cpu, (uint16_t)(op & 0xffu));
        return 0;
    } else if (op == 0x5e00u) {
        uint16_t target = fetch16(cpu);
        push16(cpu, cpu->pc);
        cpu->pc = target;
        return 0;
    } else if ((op & 0xff80u) >= 0x7000u && (op & 0xff80u) <= 0x7780u) {
        uint8_t v = r8(cpu, op & 0x0fu);
        bit_op_reg(cpu, op, &v, true);
        return cpu->unsupported ? 1 : 0;
    } else if ((op & 0xff8fu) == 0x7c00u) {
        uint16_t ext = fetch16(cpu);
        uint16_t addr = r16(cpu, (op >> 4) & 7u);
        uint8_t v = rb(cpu, addr);
        if (!bit_op_apply(cpu, ext, &v, false)) { mark_unsupported(cpu, pc0, ext); return 1; }
        return 0;
    } else if ((op & 0xff8fu) == 0x7d00u) {
        uint16_t ext = fetch16(cpu);
        uint16_t addr = r16(cpu, (op >> 4) & 7u);
        uint8_t v = rb(cpu, addr);
        uint8_t oldv = v;
        if (!bit_op_apply(cpu, ext, &v, true)) { mark_unsupported(cpu, pc0, ext); return 1; }
        if (v != oldv) wb(cpu, addr, v);
        return 0;
    } else if ((op & 0xff00u) == 0x7e00u) {
        uint16_t ext = fetch16(cpu);
        uint16_t addr = (uint16_t)(0xff00u | (op & 0xffu));
        uint8_t v = rb(cpu, addr);
        if (!bit_op_apply(cpu, ext, &v, false)) { mark_unsupported(cpu, pc0, ext); return 1; }
        return 0;
    } else if ((op & 0xff00u) == 0x7f00u) {
        uint16_t ext = fetch16(cpu);
        uint16_t addr = (uint16_t)(0xff00u | (op & 0xffu));
        uint8_t v = rb(cpu, addr);
        uint8_t oldv = v;
        if (!bit_op_apply(cpu, ext, &v, true)) { mark_unsupported(cpu, pc0, ext); return 1; }
        if (v != oldv) wb(cpu, addr, v);
        return 0;
    } else if ((op & 0xf000u) == 0x8000u) {
        unsigned d = (op >> 8) & 0x0fu;
        w8(cpu, d, do_add8(cpu, r8(cpu, d), (uint8_t)op)); return 0;
    } else if ((op & 0xf000u) == 0x9000u) {
        unsigned d = (op >> 8) & 0x0fu;
        w8(cpu, d, do_addx8(cpu, r8(cpu, d), (uint8_t)op)); return 0;
    } else if ((op & 0xf000u) == 0xa000u) {
        do_sub8(cpu, r8(cpu, (op >> 8) & 0x0fu), (uint8_t)op); return 0;
    } else if ((op & 0xf000u) == 0xb000u) {
        unsigned d = (op >> 8) & 0x0fu;
        w8(cpu, d, do_subx8(cpu, r8(cpu, d), (uint8_t)op)); return 0;
    } else if ((op & 0xf000u) == 0xc000u) {
        unsigned d = (op >> 8) & 0x0fu;
        uint8_t v = (uint8_t)(r8(cpu, d) | (uint8_t)op);
        set_nzv8(cpu, v); w8(cpu, d, v); return 0;
    } else if ((op & 0xf000u) == 0xd000u) {
        unsigned d = (op >> 8) & 0x0fu;
        uint8_t v = (uint8_t)(r8(cpu, d) ^ (uint8_t)op);
        set_nzv8(cpu, v); w8(cpu, d, v); return 0;
    } else if ((op & 0xf000u) == 0xe000u) {
        unsigned d = (op >> 8) & 0x0fu;
        uint8_t v = (uint8_t)(r8(cpu, d) & (uint8_t)op);
        set_nzv8(cpu, v); w8(cpu, d, v); return 0;
    } else if ((op & 0xf000u) == 0xf000u) {
        unsigned d = (op >> 8) & 0x0fu;
        uint8_t v = (uint8_t)op;
        set_nzv8(cpu, v); w8(cpu, d, v); return 0;
    }

    mark_unsupported(cpu, pc0, op);
    return 1;
}
