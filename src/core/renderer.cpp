#include <smb/core/environment.hpp>
#include <smb/data/game_data.hpp>
#include <smb/core/renderer.hpp>
#include <cstring>
#include <cstdint>
#include <array>
#include <bit>

// enable SIMD instructions for SSE up to AVX-512
#if defined(__SSSE3__)
#include <immintrin.h>
#endif

namespace {

// NOTE: This is the classic FCEUX palette, it can be replaced with any other palette
constexpr uint8_t PALETTE[64 * 3] = { // flat array (64 colors * 3 bytes per color)
    0x74, 0x74, 0x74, 0x24, 0x18, 0x8c, 0x00, 0x00, 0xa8, 0x44, 0x00, 0x9c,
    0x8c, 0x00, 0x74, 0xa8, 0x00, 0x10, 0xa4, 0x00, 0x00, 0x7c, 0x08, 0x00,
    0x40, 0x2c, 0x00, 0x00, 0x44, 0x00, 0x00, 0x50, 0x00, 0x00, 0x3c, 0x14,
    0x18, 0x3c, 0x5c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xbc, 0xbc, 0xbc, 0x00, 0x70, 0xec, 0x20, 0x38, 0xec, 0x80, 0x00, 0xf0,
    0xbc, 0x00, 0xbc, 0xe4, 0x00, 0x58, 0xd8, 0x28, 0x00, 0xc8, 0x4c, 0x0c,
    0x88, 0x70, 0x00, 0x00, 0x94, 0x00, 0x00, 0xa8, 0x00, 0x00, 0x90, 0x38,
    0x00, 0x80, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xfc, 0xfc, 0xfc, 0x3c, 0xbc, 0xfc, 0x5c, 0x94, 0xfc, 0xcc, 0x88, 0xfc,
    0xf4, 0x78, 0xfc, 0xfc, 0x74, 0xb4, 0xfc, 0x74, 0x60, 0xfc, 0x98, 0x38,
    0xf0, 0xbc, 0x3c, 0x80, 0xd0, 0x10, 0x4c, 0xdc, 0x48, 0x58, 0xf8, 0x98,
    0x00, 0xe8, 0xd8, 0x78, 0x78, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xfc, 0xfc, 0xfc, 0xa8, 0xe4, 0xfc, 0xc4, 0xd4, 0xfc, 0xd4, 0xc8, 0xfc,
    0xfc, 0xc4, 0xfc, 0xfc, 0xc4, 0xd8, 0xfc, 0xbc, 0xb0, 0xfc, 0xd8, 0xa8,
    0xfc, 0xe4, 0xa0, 0xe0, 0xfc, 0xa0, 0xa8, 0xf0, 0xbc, 0xb0, 0xfc, 0xcc,
    0x9c, 0xfc, 0xf0, 0xc4, 0xc4, 0xc4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// pre-decoded CHR: [tile 0..511][row 0..7] -> 8 pixels (0..3 idx)
constexpr std::array<uint64_t, 512 * 8> MakeCHRRowPixels() noexcept {
    std::array<uint64_t, 512 * 8> table {};

    for (int tile = 0; tile < 512; tile++)
        for (int row = 0; row < 8; row++) {
            const uint8_t low = CHR_ROM_DATA[tile * 16 + row];
            const uint8_t high = CHR_ROM_DATA[tile * 16 + row + 8];

            uint64_t val = 0;

            for (int px = 0; px < 8; px++) {
                const uint64_t c = ((low >> (7 - px)) & 1) | (((high >> (7 - px)) & 1) << 1);
                val |= c << (px * 8);
            }

            table[tile * 8 + row] = val;
        }
    return table;
}

constexpr std::array<uint8_t, 64> MakePaletteGrayscale() noexcept {
    std::array<uint8_t, 64> table{};

    for (int i = 0; i < 64; i++) {
        table[i] = (PALETTE[i * 3] * 77 + PALETTE[i * 3 + 1] * 151 + PALETTE[i * 3 + 2] * 28) >> 8;
    }

    return table;
}

constexpr std::array<uint32_t, 64> MakePalette32() noexcept {
    std::array<uint32_t, 64> table{};
    for (int i = 0; i < 64; i++)
        table[i] = uint32_t(PALETTE[i * 3]) | (uint32_t(PALETTE[i * 3 + 1]) << 8) | (uint32_t(PALETTE[i * 3 + 2]) << 16) | 0xFF000000u;
    return table;
}

static_assert(std::endian::native == std::endian::little);

constexpr auto CHR_ROW_PIXELS = MakeCHRRowPixels();
constexpr auto PALETTE32 = MakePalette32();
constexpr auto PALETTE_GRAYSCALE = MakePaletteGrayscale();

template <RenderMode M>
void EmitLine(uint8_t* dest, const uint8_t* src) noexcept {
    // copy as-is
    if constexpr (M != RenderMode::RGB) {
        // indexed and grayscale (already mapped while drawing)
        std::memcpy(dest, src, SCREEN_WIDTH);

    } else { // RGB
        // 4-byte stores (the extra byte is overwritten by the next pixel)
        // last pixel needs an exact 3-byte copy
        for (int x = 0; x < SCREEN_WIDTH - 1; x++)
            std::memcpy(dest + x * 3, &PALETTE32[src[x]], 4);

        std::memcpy(dest + (SCREEN_WIDTH - 1) * 3, &PALETTE[src[SCREEN_WIDTH - 1] * 3], 3);
    }
}

} // namespace closure

Sprite PPUState::GetSpriteAt(int firstSlot, int count) const noexcept {
    int x0 = SCREEN_WIDTH;
    int y0 = SCREEN_HEIGHT;

    int x1 = 0, y1 = 0;

    const int end = std::min(firstSlot + count, 64);

    for (int i = std::max(firstSlot, 0); i < end; i++) {
        const int top = oam[i * 4] + 1;

        if (top >= SCREEN_HEIGHT) continue; // off-screen

        const int left = oam[i * 4 + 3];
        x0 = std::min(x0, left);
        y0 = std::min(y0, top);
        x1 = std::max(x1, std::min(left + 8, SCREEN_WIDTH));
        y1 = std::max(y1, std::min(top + 8, SCREEN_HEIGHT));
    }

    if (x1 <= x0 || y1 <= y0)
        return {};

    return {
        .x = x0,
        .y = y0,
        .width = x1 - x0,
        .height = y1 - y0
    };
}

void PPUState::RenderSpritesRGBA(int firstSlot, int count, const Sprite& s, uint8_t* outRGBA) const noexcept {
    std::memset(outRGBA, 0, size_t(s.width) * s.height * 4); // set all to transparent

    const int sprBase = (ctrl & 0x08) ? 256 : 0;
    const int end = std::min(firstSlot + count, 64);

    // lowest OAM index has priority if overlap, so iterate in reverse
    for (int i = end - 1; i >= std::max(firstSlot, 0); i--) {
        const uint8_t* o = &oam[i * 4];
        const int top = o[0] + 1, left = o[3];
        const uint8_t attr = o[2];

        if (top >= SCREEN_HEIGHT) continue;

        const uint8_t* pal = &mutablePalette[16 + (attr & 3) * 4];

        for (int r = 0; r < 8; r++) {
            const int y = top + r - s.y;
            if (y < 0 || y >= s.height) continue;

            uint64_t pixels = CHR_ROW_PIXELS[(sprBase + o[1]) * 8 + ((attr & 0x80) ? 7 - r : r)];

            if (attr & 0x40)
                pixels = __builtin_bswap64(pixels); // flip horizontally

            for (int p = 0; p < 8; p++) {
                const int x = left + p - s.x;

                if (x < 0 || x >= s.width)
                    continue;

                const uint8_t c = (pixels >> (p * 8)) & 3;

                if (c == 0)
                    continue;

                uint8_t* px = outRGBA + (size_t(y) * s.width + x) * 4;
                std::memcpy(px, &PALETTE[(pal[c] & 0x3F) * 3], 3);

                px[3] = 255; // restore alpha
            }
        }
    }
}

template <RenderMode M>
void PPUState::RenderFrame(uint8_t* outputBuffer) const noexcept {
    constexpr size_t bytesPerPixel = ChannelsOf(M);

    const bool bgOn = mask & 0x08; // background enabled
    const bool sprOn = mask & 0x10; // sprites enabled
    const bool bgLeftOn = mask & 0x02; // show BG in leftmost 8px
    const bool sprLeftOn = mask & 0x04; // show sprites in leftmost 8px

    // grayscale is a per-color mapping, so it is applied once per palette entry here instead of once per pixel
    // (indexed and rgb keep the nes color index, rgb is expanded in EmitLine)
    auto map = [](uint8_t c) noexcept -> uint8_t {
        if constexpr (M == RenderMode::Grayscale)
            return PALETTE_GRAYSCALE[c & 0x3F];
        
        return c;
    };

    const uint8_t backdrop = map(mutablePalette[0]); // the "fallback" color (usually blue or black depending on the level type)
    const int sprBase = (ctrl & 0x08) ? 256 : 0;

    // sprites visible on each scanline, in ascending oam index
    uint8_t spriteCount[SCREEN_HEIGHT] = {};
    uint8_t spriteList[SCREEN_HEIGHT * 64];

    if (sprOn) {
        for (int i = 0; i < 64; i++) {
            const int top = oam[i * 4] + 1;
            const int rows = std::min(8, SCREEN_HEIGHT - top); // <= 0 if off screen

            for (int r = 0; r < rows; r++) {
                const int y = top + r;
                spriteList[(y << 6) + spriteCount[y]++] = static_cast<uint8_t>(i);
            }
        }
    }

    // bg palettes as 4-byte lookup tables [color 0..3] -> nes color idx (color 0 is always the backdrop)
    uint32_t bgLut[4];
    for (int p = 0; p < 4; p++)
        bgLut[p] = uint32_t(backdrop) | (uint32_t(map(mutablePalette[p * 4 + 1])) << 8)
                 | (uint32_t(map(mutablePalette[p * 4 + 2])) << 16) | (uint32_t(map(mutablePalette[p * 4 + 3])) << 24);

    uint8_t sprPal[16]; // sprite palettes, already mapped
    for (int i = 0; i < 16; i++)
        sprPal[i] = map(mutablePalette[16 + i]);

    alignas(64) uint8_t bgPixBuf[SCREEN_WIDTH + 8]; // raw color 0..3, used for priority
    alignas(64) uint8_t bgColorBuf[SCREEN_WIDTH + 8]; // nes color idx, 33 tiles
    alignas(64) uint8_t taken[SCREEN_WIDTH]; // pixels occupied by a sprite

    // drawing tile row by tile row: the scroll registers can only change at the top bar's edge, which is
    // tile-aligned, so nametable/attribute lookups are done once per 8 scanlines instead of once per scanline
    for (int ty = 0; ty < SCREEN_HEIGHT / 8; ty++) {
        uint16_t tileRow[33]; // first CHR_ROW_PIXELS index of each tile (pattern + tile) * 8
        uint8_t palIdx[33];
        int fine = 0;

        if (bgOn) {
            // tiles above top bar ignore camera x scroll
            const bool topBar = ty * 8 < TOPBAR_HEIGHT;

            const uint8_t scroll = topBar ? x_scroll_top : x_scroll_bottom;
            const uint8_t ctrlReg = topBar ? ctrl_top : ctrl_bottom;

            const int patternBase = (ctrlReg & 0x10) ? 256 : 0; // in tiles
            const int firstTx = scroll >> 3; // tile (x / 8)
            fine = scroll & 7;

            for (int t = 0; t < 33; t++) {
                const int worldTx = (firstTx + t) & 63;
                const int tx = worldTx & 31;

                const uint8_t* nametable = &vram[((ctrlReg & 1) ^ (worldTx >> 5)) * 0x400];

                const uint8_t tile = nametable[ty * 32 + tx];
                const uint8_t attr = nametable[0x3C0 + ((ty >> 2) << 3) + (tx >> 2)];

                tileRow[t] = static_cast<uint16_t>((patternBase + tile) * 8);
                palIdx[t] = (attr >> (((ty & 2) << 1) | (tx & 2))) & 3;
            }
        }

        for (int py = 0; py < 8; py++) {
            const int y = ty * 8 + py;

            // background (line/bgPix point inside the buffers so fine scrolling needs no copy)
            uint8_t* line;
            uint8_t* bgPix;

            if (!bgOn) {
                std::memset(bgColorBuf, backdrop, SCREEN_WIDTH);
                std::memset(bgPixBuf, 0, SCREEN_WIDTH);
                line = bgColorBuf;
                bgPix = bgPixBuf;
            }
            else {
                for (int t = 0; t < 33; t++) {
                    const uint64_t pixels = CHR_ROW_PIXELS[tileRow[t] + py];
                    std::memcpy(bgPixBuf + t * 8, &pixels, 8);
#if defined(__SSSE3__)
                    // one byte shuffle maps 8 raw colors through the 4-entry palette
                    const __m128i lut = _mm_cvtsi32_si128(static_cast<int>(bgLut[palIdx[t]]));

                    _mm_storel_epi64(
                        reinterpret_cast<__m128i*>(bgColorBuf + t * 8),
                        _mm_shuffle_epi8(lut, _mm_cvtsi64_si128(static_cast<long long>(pixels)))
                    );
#else
                    const uint32_t lut = bgLut[palIdx[t]];
                    for (int i = 0; i < 8; i++)
                        bgColorBuf[t * 8 + i] = static_cast<uint8_t>(lut >> (((pixels >> (i * 8)) & 3) * 8));
#endif
                }

                line = bgColorBuf + fine;
                bgPix = bgPixBuf + fine;

                if (!bgLeftOn) {
                    std::memset(line, backdrop, 8);
                    std::memset(bgPix, 0, 8);
                }
            }

            // sprites, lowest oam index first
            if (spriteCount[y] > 0) {
                std::memset(taken, 0, sizeof taken);

                for (int k = 0; k < spriteCount[y]; k++) {
                    const uint8_t* o = &oam[spriteList[(y << 6) + k] * 4];
                    const uint8_t attr = o[2];

                    int row = y - (o[0] + 1);

                    if (attr & 0x80)
                        row = 7 - row; // flip vertically

                    uint64_t pixels = CHR_ROW_PIXELS[(sprBase + o[1]) * 8 + row];

                    if (attr & 0x40)
                        pixels = __builtin_bswap64(pixels); // flip horizontally

                    const uint8_t* pal = &sprPal[(attr & 3) * 4];
                    const bool behindBg = attr & 0x20;
                    const int x = o[3];

                    for (int p = 0; p < 8; p++) {
                        const int sx = x + p;

                        if (sx >= SCREEN_WIDTH)
                            break;

                        if (sx < 8 && !sprLeftOn)
                            continue;

                        const uint8_t c = (pixels >> (p * 8)) & 3;

                        if (c == 0 || taken[sx])
                            continue;

                        // sprite 'owns' the pixel even if bg ends up hiding it
                        taken[sx] = 1;

                        if (behindBg && bgPix[sx] != 0)
                            continue;

                        line[sx] = pal[c];
                    }
                }
            }

            EmitLine<M>(outputBuffer + size_t(y) * SCREEN_WIDTH * bytesPerPixel, line);
        }
    }
}

void SMBEnv::MakeFrame(uint8_t* outputBuffer, RenderMode mode) const noexcept {

    if (!outputBuffer) return;

    switch (mode) {
        case RenderMode::Indexed:
            ppu.RenderFrame<RenderMode::Indexed>(outputBuffer);
            break;

        case RenderMode::RGB:
            ppu.RenderFrame<RenderMode::RGB>(outputBuffer);
            break;

        case RenderMode::Grayscale:
            ppu.RenderFrame<RenderMode::Grayscale>(outputBuffer);
            break;
    }
}
