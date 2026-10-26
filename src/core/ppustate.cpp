#include <smb/core/ppustate.hpp>
#include <smb/data/game_data.hpp>
#include <cstdint>

uint8_t& PPUState::_ppuMem(uint16_t addr) noexcept {
    addr &= 0x3FFF;
    if (addr >= 0x3F00) [[unlikely]] { // is palette
        uint8_t i = addr & 0x1F;

        if ((i & 0x13) == 0x10)
            i &= ~0x10; // $3F10/14/18/1C mirrors $3F00/04/08/0C

        return mutablePalette[i];
    }
    return vram[addr & 0x7FF]; // nametables, vertical mirroring
}

void PPUState::WritePPU_Ctrl(uint8_t val) noexcept {
    ctrl = val;
    
    // instead of status & 0x40
    (sprite0Hit ? ctrl_bottom : ctrl_top) = val;
}

void PPUState::WritePPU_Mask(uint8_t val) noexcept {
    mask = val;
}

void PPUState::WritePPU_Scroll(uint8_t val) noexcept {
    // y scroll is completely unused and WritePPU_Scroll is called in 
    // groups of two (reduced to one) so latch can be omitted completely
    x_scroll = val;

    // instead of status & 0x40
    (sprite0Hit ? x_scroll_bottom : x_scroll_top) = val;
}

void PPUState::WritePPU_Data(uint8_t val) noexcept {
    uint16_t a = vram_addr & 0x3FFF;
    
    if (a >= 0x2000)
        _ppuMem(a) = val; // below $2000 is CHR-ROM, which is read-only
    
    vram_addr = (vram_addr + ((ctrl & 0x04) ? 32 : 1)) & 0x3FFF; // &0x04 returns third bit of ppuctrl
}

void PPUState::ReadPPU_Data(uint8_t& reg) noexcept {
    uint16_t a = vram_addr & 0x3FFF;
    if (a >= 0x3F00) {
        // range palette: read instantly
        reg = _ppuMem(a);
        read_buffer = _ppuMem(a - 0x1000); // shadow nametable byte
    } else {
        // return old buffer and update it
        reg = read_buffer;
        read_buffer = (a < 0x2000) ? CHR_ROM_DATA[a] : _ppuMem(a);
    }
    vram_addr = (vram_addr + ((ctrl & 0x04) ? 32 : 1)) & 0x3FFF; // &0x04 returns third bit of ppuctrl
}