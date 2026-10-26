#pragma once
#include <cstdint>

struct alignas(8) CPUState {
    // CPU 8-bit registers
    uint8_t A = 0;
    uint8_t X = 0;
    uint8_t Y = 0;

    uint8_t SP = 0xff; // stack pointer (for PHA and PLA)

    bool flag_C = false; // Carry Flag (used for add/sub and bit shifts)
    
    // lazy flags, Z and N are computed only when a branch reads them
    uint8_t zSource = 1; // Z is set if this is 0 (starts as Z = false)
    uint8_t nSource = 0; // N is bit 7 of this (starts as N = false)

    // update flags
    void setZN(uint8_t val) noexcept {
        zSource = val;
        nSource = val;
    }

    void setZN_BIT(uint8_t val) noexcept {
        zSource = A & val;
        nSource = val;
    }

    void setC(uint8_t reg, uint8_t val) noexcept {
        flag_C = reg >= val;
    }

    bool isZero() const noexcept {
        return zSource == 0;
    }

    bool isNegative() const noexcept {
        return (nSource & 0x80) != 0;
    }
};