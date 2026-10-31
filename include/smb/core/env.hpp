#pragma once
#include <smb/core/ppustate.hpp>
#include <smb/data/constants.hpp>
#include <smb/core/renderer.hpp>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <array>

// high-level aliases
namespace Input {
    enum Type : uint8_t {
        NONE   = 0,
        RIGHT  = 1 << 0,
        LEFT   = 1 << 1,
        DOWN   = 1 << 2,
        UP     = 1 << 3,
        START  = 1 << 4,
        SELECT = 1 << 5,
        B      = 1 << 6,
        A      = 1 << 7
    };
}

enum class ResetPreset : uint8_t {
    ColdBoot, TitleScreen,
    W1L1, W1L2, W1L3, W1L4,
    W2L1, W2L2, W2L3, W2L4,
    W3L1, W3L2, W3L3, W3L4,
    W4L1, W4L2, W4L3, W4L4,
    W5L1, W5L2, W5L3, W5L4,
    W6L1, W6L2, W6L3, W6L4,
    W7L1, W7L2, W7L3, W7L4,
    W8L1, W8L2, W8L3, W8L4,
    Count
};

struct alignas(64) SMBEnv {
    // persistent components
    PPUState ppu;
    uint8_t ram[0x800] {}; // initialized to zeros

    // set current state to preset instead of full-resetting and stepping
    void Reset(ResetPreset preset = ResetPreset::TitleScreen) noexcept {
        *this = ResetPresets()[static_cast<size_t>(preset)];
    }

    // call NMI 'times' times, holds 'input' for the entire duration
    void Step(uint8_t input = 0, int times = 1) noexcept {
        while (times-- > 0)
            NMI(input);
    }

    // remove left+right and up+down impossible combinations (hardware-like behavior)
    static constexpr uint8_t ValidateInput(uint8_t input) noexcept {
        constexpr uint8_t LR = Input::LEFT | Input::RIGHT;
        constexpr uint8_t UD = Input::UP | Input::DOWN;

        if ((input & LR) == LR) input &= static_cast<uint8_t>(~LR);
        if ((input & UD) == UD) input &= static_cast<uint8_t>(~UD);

        return input;
    }

    // calculate a frame of the current env state, defined in renderer.cpp
    void RenderFrame(uint8_t* buffer, RenderMode renderMode) const noexcept;

    Sprite RenderSprite(uint8_t* buffer, int firstSlot, int count) const noexcept {
        const Sprite sprite = ppu.GetSpriteAt(firstSlot, count);

        if (sprite.exists())
            ppu.RenderSpritesRGBA(firstSlot, count, sprite, buffer);

        return sprite;
    }

    Sprite RenderSpriteMario(uint8_t* buffer) const noexcept {
        return RenderSprite(buffer, _GetMarioFirstSlot(), 8);
    }

    // slot of mario's first sprite in OAM
    int _GetMarioFirstSlot() const noexcept {
        return ram[Player_SprDataOffset] >> 2;
    }

    // bounds only, used by python for size buffers
    Sprite _GetSprite(int firstSlot, int count) const noexcept {
        return ppu.GetSpriteAt(firstSlot, count);
    }

    Sprite _GetSpriteMario() const noexcept {
        return _GetSprite(_GetMarioFirstSlot(), 8);
    }

    // utilities used for RL and such:
    int GetMarioState() const noexcept {
        /*
            0x00 : Leftmost of screen
            0x01 : Climbing vine
            0x02 : Entering reversed-L pipe
            0x03 : Going down a pipe
            0x04 : Auto-walk from flagpole/axe
            0x05 : Some otheer auto-walk
            0x06 : Dead
            0x07 : Entering area
            0x08 : Normal
            0x09 : Cannot move
            0x0B : Dying
            0x0C : Palette cycling, can't move
        */
        return ram[0x000e];
    }

    bool IsDeathState() const noexcept {
        auto state = GetMarioState();
        return state == 0x0B || state == 0x06 || GetViewportY() > 1;
    }

    bool IsWinState() const noexcept {
        return GetMarioState() == 0x04;
    }

    bool GetTerminated() const noexcept {
        return IsDeathState() || IsWinState();
    }

    int GetLives() const noexcept {
        return ram[NumberofLives];
    }

    int GetCoins() const noexcept {
        return ram[CoinTally]; // 0-99
    }

    int GetTime() const noexcept {
        return ReadRangeRAM(GameTimerDisplay, 3);
    }

    int GetScore() const noexcept {
        return ReadRangeRAM(PlayerScoreDisplay, 6);
    }
    
    int GetHighScore() const noexcept {
        return ReadRangeRAM(TopScoreDisplay, 6);
    }

    int GetViewportY() const noexcept {
        // 1 = in viewport, 0 = above viewport, >1 = below viewport
        return ram[Player_Y_HighPos];
    }

    // ScreenY would be practically equivalent
    int GetAbsY() const noexcept {
        return ram[Player_Y_Position];
    }

    int GetAbsX() const noexcept {
        return (ram[Player_PageLoc] << 8) | ram[Player_X_Position];
    }
    
    int GetScreenX() const noexcept {
        // relative to screen's left edge
        return ram[Player_Rel_XPos];
    }

    // function used to copy the current env state to another instance
    // this is equivalent to C++ assignment copy operator, but implemented
    // as a function for clarity and for python usage
    SMBEnv Copy() const noexcept {
        return *this;
    }

    SMBEnv() noexcept = default;

    bool operator==(const SMBEnv& other) const noexcept {
        return std::memcmp(ram, other.ram, sizeof(ram)) == 0;
    }

private:
    inline static constexpr size_t PresetsCount = static_cast<size_t>(ResetPreset::Count);

    // defined in presets.cpp
    static const std::array<SMBEnv, PresetsCount>& ResetPresets() noexcept;

    // dummy call to initialize the reset presets and avoid Reset() being slow when first called
    static void InitResetPresets() noexcept {
        (void)ResetPresets();
    }
    
    // private utilities:
    int ReadRangeRAM(uint16_t startAddress, uint16_t length) const {
        int value = 0;
        
        for (uint16_t i = 0; i < length; ++i) {
            value = (value * 10) + ram[startAddress + i];
        }
        
        return value;
    }

    // wrappers, defined in game.cpp
    void FirstReset() noexcept; // called once in ResetPresets() init
    void NMI(uint8_t input) noexcept; // used by Step()
};
