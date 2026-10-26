#pragma once
#include <smb/core/renderer.hpp>
#include <cstdint>

// TODO perhaps use template on Step and split PPU in two parts
// one used for rendering (written to in the step before the rendering)
// and one used for necessary write/reads such as the flags, sprite0hit and such
// this would grant higher performance on headless execution but introduce boilerplate
struct PPUState {
    uint8_t ctrl = 0; // 0x2000
    uint8_t mask = 0; // 0x2001
    uint8_t oamaddr = 0; // 0x2003

    bool sprite0Hit = false; // replacement for 'status' as only its bit 6 is used
    uint8_t x_scroll = 0;
    uint16_t vram_addr = 0;
    uint8_t read_buffer = 0;

    uint8_t x_scroll_top = 0;
    uint8_t x_scroll_bottom = 0;
    uint8_t ctrl_top = 0;
    uint8_t ctrl_bottom = 0;

    uint8_t vram[0x800] {};
    uint8_t oam[256] {};

    uint8_t mutablePalette[32] {}; // the current 32 indices in use (4 bg x 4 colors + 4 sprites x 4 colors)

    // specific read/write to PPU, defined in ppustate.cpp
    uint8_t& _ppuMem(uint16_t a) noexcept; // util
    void WritePPU_Ctrl(uint8_t val) noexcept; // PPU_CTRL_REG1
    void WritePPU_Mask(uint8_t val) noexcept; // PPU_CTRL_REG2
    void WritePPU_Scroll(uint8_t val) noexcept;
    void WritePPU_Data(uint8_t val) noexcept;
    void ReadPPU_Data(uint8_t& reg) noexcept;
    // some read/write methods were omitted because they are dummy reads (used to affect the latch or vblank flag)

    // rendering-related:
    Sprite GetSpriteAt(int firstSlot, int count) const noexcept;
    void RenderSpritesRGBA(int firstSlot, int count, const Sprite& s, uint8_t* outRGBA) const noexcept;

    template <RenderMode M>
    void RenderFrame(uint8_t* outputBuffer) const noexcept;
};