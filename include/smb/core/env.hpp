#pragma once
#include <smb/core/cpustate.hpp>
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
    // components
    CPUState cpu; // this is temporary and only persists in FirstReset and Step, but it's here for convenience
    PPUState ppu;
    uint8_t ram[0x800] {}; // initialized to zeros

    // hold input until manually resetted
    void SetInput(uint8_t input) noexcept {
        this->input = input;
    }

    // set current state to preset instead of full-resetting and stepping
    void Reset(ResetPreset preset = ResetPreset::TitleScreen) noexcept {
        *this = ResetPresets()[static_cast<size_t>(preset)];
    }

    void Step() noexcept; // call NMI once

    // calculate a frame of the current env state
    void MakeFrame(uint8_t* outputBuffer, RenderMode renderMode) const noexcept;

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

    static constexpr uint8_t GetLevelArea(ResetPreset preset) noexcept {
        // accounts for sub-levels and cutscenes
        switch (preset) {
            case ResetPreset::W1L1: return 0x00;
            case ResetPreset::W1L2: return 0x02; // 0x01 is a cutscene
            case ResetPreset::W1L3: return 0x03;
            case ResetPreset::W1L4: return 0x04;

            case ResetPreset::W2L1: return 0x05;
            case ResetPreset::W2L2: return 0x07; // 0x06 is a cutscene
            case ResetPreset::W2L3: return 0x08;
            case ResetPreset::W2L4: return 0x09;

            case ResetPreset::W3L1: return 0x0A;
            case ResetPreset::W3L2: return 0x0B;
            case ResetPreset::W3L3: return 0x0C;
            case ResetPreset::W3L4: return 0x0D;

            case ResetPreset::W4L1: return 0x0E;
            case ResetPreset::W4L2: return 0x10; // 0x0F is a cutscene
            case ResetPreset::W4L3: return 0x11;
            case ResetPreset::W4L4: return 0x12;

            case ResetPreset::W5L1: return 0x13;
            case ResetPreset::W5L2: return 0x14;
            case ResetPreset::W5L3: return 0x15;
            case ResetPreset::W5L4: return 0x16;

            case ResetPreset::W6L1: return 0x17;
            case ResetPreset::W6L2: return 0x18;
            case ResetPreset::W6L3: return 0x19;
            case ResetPreset::W6L4: return 0x1A;

            case ResetPreset::W7L1: return 0x1B;
            case ResetPreset::W7L2: return 0x1D; // 0x1C is a cutscene
            case ResetPreset::W7L3: return 0x1E;
            case ResetPreset::W7L4: return 0x1F;

            case ResetPreset::W8L1: return 0x20;
            case ResetPreset::W8L2: return 0x21;
            case ResetPreset::W8L3: return 0x22;
            case ResetPreset::W8L4: return 0x23;

            default: return 0x00;
        }
    }

    inline static const std::array<SMBEnv, PresetsCount>& ResetPresets() noexcept {
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
            for (int i = 0; i < 28; i++)
                titleScreen.Step();

            // TODO this is incomplete, all levels actually start at 1-1
            // levels: start from title screen as early as possible and step
            // until the level starts fully (like gym-super-mario-bros does)
            for (uint8_t i = 2; i < 34; ++i) { // iterate through the 32 levels
                auto& level = p[i];
                level = titleScreen.Copy();
                
                // load requested level
                level.ram[AreaNumber] = static_cast<uint8_t>(
                    // get level beginning (no cutscenes)
                    GetLevelArea(static_cast<ResetPreset>(i))
                );

                // at 27th frame START can be pressed for 1 frame to begin the game as early as possible
                // in this case levels come from "titleScreen", so this is 29th frame, but it doesn't matter
                level.SetInput(Input::START);
                level.Step();
                level.SetInput(Input::NONE);

                // x and y of text "World x-y" (must be done after AreaNumber is set and level starts, or it will cause issues)
                level.ram[WorldNumber] = static_cast<uint8_t>((i - 2) / 4); // from 0 to 7 (1 to 8 in text)
                level.ram[LevelNumber] = static_cast<uint8_t>((i - 2) % 4); // from 0 to 3 (1 to 4 in text)
                
                // step until the level starts (the next input will have effect)
                for (int j = 0; j < 161; j++)
                    level.Step();
            }

            return p;
        }();
        return presets;
    }

    // called once in ResetPresets() init
    void FirstReset() noexcept;

    // controller input
    uint8_t input = 0;

    // simplified joypad reading
    void ReadJoypads() noexcept {
        // 4th bit is START and 5th is SELECT
        uint8_t inp_start = input & 0b00110000;
        uint8_t& SJB = ram[SavedJoypadBits];
        uint8_t& JBM = ram[JoypadBitMask];

        SJB = input;

        if (inp_start & JBM)
            SJB = inp_start;
        else
            JBM = input;

        // adjusting registers to match original values
        cpu.X = 1;
        cpu.Y = 0;
    }

    // defined in instructions.cpp or inlined
    // load value or ram[addr] to register
    void LDQ_imm(uint8_t& reg, uint8_t val) noexcept;
    void LDQ_zp(uint8_t& reg, uint8_t addr) noexcept;
    void LDQ_zp_xy(uint8_t& reg, uint8_t addr, uint8_t& offsetReg) noexcept;
    void LDQ_abs(uint8_t& reg, uint16_t addr) noexcept;
    void LDQ_PRGTable(uint8_t& reg, const uint8_t* table, uint8_t regAddr) noexcept;
    void LDQ_abs_xy(uint8_t& reg, uint16_t addr, uint8_t& offsetReg) noexcept;
    void LDA_ind_x(uint8_t addr) noexcept;
    void LDA_ind_y(uint8_t addr) noexcept;
    void LDA_PRG_ind_y(uint8_t addr) noexcept;
    void LDA_Dyn_ind_y(uint8_t addr) noexcept;

    // store register value to ram[addr]
    void STQ_zp(uint8_t reg, uint8_t addr) noexcept;
    void STQ_zp_xy(uint8_t reg, uint8_t addr, uint8_t& offsetReg) noexcept;
    void STQ_abs(uint8_t reg, uint16_t addr) noexcept;
    void STA_abs_xy(uint16_t addr, uint8_t& offsetReg) noexcept;
    void STA_ind_x(uint8_t addr) noexcept;
    void STA_ind_y(uint8_t addr) noexcept;

    // compare register value to val
    void CMPQ_imm(uint8_t reg, uint8_t val) noexcept;
    void CMPQ_zp(uint8_t reg, uint8_t addr) noexcept;
    void CMPQ_abs(uint8_t reg, uint16_t addr) noexcept;
    void CMPA_zp_xy(uint8_t addr, uint8_t& offsetReg) noexcept;
    void CMPA_abs_xy(uint16_t addr, uint8_t& offsetReg) noexcept;
    void CMPA_PRGTable(const uint8_t* table, uint8_t regAddr) noexcept;
    void CMPA_ind_x(uint8_t addr) noexcept;
    void CMPA_ind_y(uint8_t addr) noexcept;

    // branch if
    bool BEQ() noexcept { return cpu.isZero(); }
    bool BNE() noexcept { return !cpu.isZero(); }
    bool BCS() noexcept { return cpu.flag_C; }
    bool BCC() noexcept { return !cpu.flag_C; }
    bool BMI() noexcept { return cpu.isNegative(); }
    bool BPL() noexcept { return !cpu.isNegative(); }

    // copy (first letter reg) to (second letter reg)
    void CopyYA() noexcept { cpu.setZN(cpu.A = cpu.Y); }
    void CopyAY() noexcept { cpu.setZN(cpu.Y = cpu.A); }
    void CopyXA() noexcept { cpu.setZN(cpu.A = cpu.X); }
    void CopyAX() noexcept { cpu.setZN(cpu.X = cpu.A); }

    // decrement/increment register or ram[addr]
    void DEX() noexcept { cpu.setZN(--cpu.X); }
    void DEY() noexcept { cpu.setZN(--cpu.Y); }
    void INX() noexcept { cpu.setZN(++cpu.X); }
    void INY() noexcept { cpu.setZN(++cpu.Y); }
    void INC_zp(uint8_t addr) noexcept;
    void DEC_zp(uint8_t addr) noexcept;
    void INC_zp_x(uint8_t addr) noexcept;
    void DEC_zp_x(uint8_t addr) noexcept;
    void INC_abs(uint16_t addr) noexcept;
    void DEC_abs(uint16_t addr) noexcept;
    void INC_abs_x(uint16_t addr) noexcept;
    void DEC_abs_x(uint16_t addr) noexcept;

    // BIT (.db 0x2c, used to optimize)
    void BIT_zp(uint8_t addr) noexcept;
    void BIT_abs(uint16_t addr) noexcept;

    // ORA
    void ORA_imm(uint8_t val) noexcept;
    void ORA_zp(uint8_t addr) noexcept;
    void ORA_zp_xy(uint8_t addr, uint8_t& offsetReg) noexcept;
    void ORA_abs(uint16_t addr) noexcept;
    void ORA_abs_xy(uint16_t addr, uint8_t& offsetReg) noexcept;
    void ORA_PRGTable(const uint8_t* table, uint8_t regAddr) noexcept;
    void ORA_ind_x(uint8_t addr) noexcept;
    void ORA_ind_y(uint8_t addr) noexcept;

    // AND
    void AND_imm(uint8_t val) noexcept;
    void AND_zp(uint8_t addr) noexcept;
    void AND_zp_xy(uint8_t addr, uint8_t& offsetReg) noexcept;
    void AND_abs(uint16_t addr) noexcept;
    void AND_abs_xy(uint16_t addr, uint8_t& offsetReg) noexcept;
    void AND_PRGTable(const uint8_t* table, uint8_t regAddr) noexcept;
    void AND_ind_x(uint8_t addr) noexcept;
    void AND_ind_y(uint8_t addr) noexcept;

    // EOR
    void EOR_imm(uint8_t val) noexcept;
    void EOR_zp(uint8_t addr) noexcept;
    void EOR_zp_xy(uint8_t addr, uint8_t& offsetReg) noexcept;
    void EOR_abs(uint16_t addr) noexcept;
    void EOR_abs_xy(uint16_t addr, uint8_t& offsetReg) noexcept;
    void EOR_ind_x(uint8_t addr) noexcept;
    void EOR_ind_y(uint8_t addr) noexcept;

    // PHA and PLA
    void PHA() noexcept;
    void PLA() noexcept;

    // ASL
    void ASL_acc() noexcept;
    void ASL_zp(uint8_t addr) noexcept;
    void ASL_zp_x(uint8_t addr) noexcept;
    void ASL_abs(uint16_t addr) noexcept;
    void ASL_abs_x(uint16_t addr) noexcept;

    // LSR
    void LSR_acc() noexcept;
    void LSR_zp(uint8_t addr) noexcept;
    void LSR_zp_x(uint8_t addr) noexcept;
    void LSR_abs(uint16_t addr) noexcept;
    void LSR_abs_x(uint16_t addr) noexcept;

    // ROL
    void ROL_acc() noexcept;
    void ROL_zp(uint8_t addr) noexcept;
    void ROL_zp_x(uint8_t addr) noexcept;
    void ROL_abs(uint16_t addr) noexcept;
    void ROL_abs_x(uint16_t addr) noexcept;

    // ROR
    void ROR_acc() noexcept;
    void ROR_zp(uint8_t addr) noexcept;
    void ROR_zp_x(uint8_t addr) noexcept;
    void ROR_abs(uint16_t addr) noexcept;
    void ROR_abs_x(uint16_t addr) noexcept;

    // SEC and CLC
    void SEC() noexcept { cpu.flag_C = true; }
    void CLC() noexcept { cpu.flag_C = false; }

    // ADC
    void ADC_imm(uint8_t val) noexcept;
    void ADC_zp(uint8_t addr) noexcept;
    void ADC_zp_x(uint8_t addr) noexcept;
    void ADC_abs(uint16_t addr) noexcept;
    void ADC_abs_x(uint16_t addr) noexcept;
    void ADC_abs_y(uint16_t addr) noexcept;
    void ADC_PRGTable(const uint8_t* table, uint8_t regAddr) noexcept;
    void ADC_ind_x(uint8_t addr) noexcept;
    void ADC_ind_y(uint8_t addr) noexcept;

    // SBC
    void SBC_imm(uint8_t val) noexcept;
    void SBC_zp(uint8_t addr) noexcept;
    void SBC_zp_x(uint8_t addr) noexcept;
    void SBC_abs(uint16_t addr) noexcept;
    void SBC_abs_x(uint16_t addr) noexcept;
    void SBC_abs_y(uint16_t addr) noexcept;
    void SBC_PRGTable(const uint8_t* table, uint8_t regAddr) noexcept;
    void SBC_ind_x(uint8_t addr) noexcept;
    void SBC_ind_y(uint8_t addr) noexcept;
};
