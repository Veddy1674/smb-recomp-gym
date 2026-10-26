#pragma once
#include <cstddef>

inline constexpr int SCREEN_WIDTH = 256, SCREEN_HEIGHT = 240;
inline constexpr int TOPBAR_HEIGHT = 32; // status bar height in pixels (not affected by x scroll)

enum class RenderMode {
    Indexed, RGB, Grayscale
};

struct Sprite {
    int x = 0, y = 0;
    int width = 0, height = 0;

    inline bool exists() const noexcept {
        return width; // 0 width means no sprite (not rendered or offscreen)
    }
};

constexpr int ChannelsOf(RenderMode m) noexcept {
    switch (m) {
        case RenderMode::RGB: return 3;
        default: return 1; // indexed or grayscale
    }
}

constexpr size_t FrameBytesOf(RenderMode m) noexcept {
    return size_t(SCREEN_WIDTH) * SCREEN_HEIGHT * ChannelsOf(m);
}