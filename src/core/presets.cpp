#include <smb/core/env.hpp>

namespace {

constexpr std::array<uint8_t, 32> LevelAreas = {
    0x00, 0x02, 0x03, 0x04, // w1 (0x01 is a cutscene)
    0x05, 0x07, 0x08, 0x09, // w2 (0x06 is a cutscene)
    0x0A, 0x0B, 0x0C, 0x0D, // w3
    0x0E, 0x10, 0x11, 0x12, // w4 (0x0F is a cutscene)
    0x13, 0x14, 0x15, 0x16, // w5
    0x17, 0x18, 0x19, 0x1A, // w6
    0x1B, 0x1D, 0x1E, 0x1F, // w7 (0x1C is a cutscene)
    0x20, 0x21, 0x22, 0x23, // w8
};

} // namespace

const std::array<SMBEnv, SMBEnv::PresetsCount>& SMBEnv::ResetPresets() noexcept {
    static const std::array<SMBEnv, PresetsCount> presets = [] {
        std::array<SMBEnv, PresetsCount> p{};

        auto& coldBoot = p[0];
        auto& titleScreen = p[1];

        // coldBoot: (only reset)
        coldBoot.FirstReset();
        
        // at 23rd frame the background becomes sky-blue
        // at 28th frame the whole title screen appears
        
        // titleScreen: reset and step 28 times until title screen is fully visible
        titleScreen = coldBoot.Copy();

        // step until the title screen is fully visible
        titleScreen.Step(0, 28);

        // TODO there still are bugs, like when mario dies the level corrupts!
        
        // levels: start from title screen as early as possible and step
        // until the level starts fully (like gym-super-mario-bros does)
        for (uint8_t i = 2; i < 34; ++i) { // iterate through the 32 levels
            auto& level = p[i];
            level = titleScreen.Copy();
            
            // load level beginning (no cutscenes)
            level.ram[AreaNumber] = LevelAreas[i - 2];

            // at 27th frame START can be pressed for 1 frame to begin the game as early as possible
            // in this case levels come from "titleScreen", so this is 29th frame, but it doesn't matter
            level.Step(Input::START);

            // x and y of text "World x-y" (must be done after AreaNumber is set and level starts, or it will cause issues)
            level.ram[WorldNumber] = static_cast<uint8_t>((i - 2) / 4); // from 0 to 7 (1 to 8 in text)
            level.ram[LevelNumber] = static_cast<uint8_t>((i - 2) % 4); // from 0 to 3 (1 to 4 in text)
            
            // step until the level starts (the next input will have effect)
            level.Step(0, 161);
        }

        return p;
    }();
    return presets;
}