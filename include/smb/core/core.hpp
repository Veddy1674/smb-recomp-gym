#pragma once
#include <smb/core/cpustate.hpp>
#include <smb/data/game_data.hpp>
#include <smb/core/env.hpp>
#include <cstdint>

struct SMBCore {
    CPUState cpu {}; // CPU only persists during FirstReset/NMI (where SMBCore is created)
    PPUState& ppu; // reference to PPUState in SMBEnv
    uint8_t (&ram)[0x800]; // reference to RAM in SMBEnv

    explicit SMBCore(SMBEnv& e) noexcept : ppu(e.ppu), ram(e.ram) {}

    // defined in game.cpp
    void NMI(uint8_t input) noexcept;
    void FirstReset() noexcept;

    // simplified joypad reading
    void ReadJoypads(uint8_t input) noexcept {
        uint8_t& SJB = ram[SavedJoypadBits];
        uint8_t& JBM = ram[JoypadBitMask];

        SJB = input;

        // 4th bit is START and 5th is SELECT
        uint8_t inp_start = SJB & 0b00110000;

        if (inp_start & JBM)
            SJB = inp_start;
        else
            JBM = SJB;

        // adjusting registers to match original values
        cpu.X = 1;
        cpu.Y = 0;
    }


    // impl source: https://www.nesdev.org/obelisk-6502-guide/reference.html

    // the letter Q was used as a placeholder for "any register"
    // (e.g. LDQ_imm is used for LDA, LDX and LDY in immediate mode)

    // load value or ram[addr] to register
    void LDQ_imm(uint8_t& reg, uint8_t val) noexcept {
        cpu.setZN(reg = val);
    }

    void LDQ_zp(uint8_t& reg, uint8_t addr) noexcept {
        cpu.setZN(reg = ram[addr]);
    }

    void LDQ_zp_xy(uint8_t& reg, uint8_t addr, uint8_t& offsetReg) noexcept {
        uint8_t res_addr = (addr + offsetReg) & 0xFF; // wraps in zero page
        cpu.setZN(reg = ram[res_addr]);
    }

    void LDQ_abs(uint8_t& reg, uint16_t addr) noexcept {
        cpu.setZN(reg = ram[addr]);
    }

    void LDQ_PRGTable(uint8_t& reg, const uint8_t* table, uint8_t regAddr) noexcept {
        cpu.setZN(reg = table[regAddr]);
    }

    void LDQ_abs_xy(uint8_t& reg, uint16_t addr, uint8_t& offsetReg) noexcept {
        uint16_t res_addr = addr + offsetReg;
        cpu.setZN(reg = ram[res_addr]);
    }

    void LDA_ind_x(uint8_t addr) noexcept {
        uint8_t ptr = (addr + cpu.X) & 0xFF; // wraps in zero page
        uint16_t res_addr = ram[ptr] | (ram[(ptr + 1) & 0xFF] << 8);

        cpu.setZN(cpu.A = ram[res_addr]);
    }

    void LDA_ind_y(uint8_t addr) noexcept {
        uint16_t base = ram[addr] | (ram[(addr + 1) & 0xFF] << 8);
        uint16_t res_addr = base + cpu.Y;

        cpu.setZN(cpu.A = ram[res_addr]);
    }

    // old implementation that only reads from PRG_ROM_DATA, it could work in cases where LDA_Dyn_ind_y
    // exclusively reads from ROM DATA and not RAM in the same instruction (and vice versa with LDA_ind_y)
    // void LDA_PRG_ind_y(uint8_t addr) noexcept {
    //     uint16_t base = ram[addr] | (ram[(addr + 1) & 0xFF] << 8);
    //     uint16_t res_addr = base + cpu.Y;

    //     cpu.setZN(cpu.A = PRG_ROM_DATA[res_addr - 0x8000]);
    // }

    // read RAM or PRG dynamically
    void LDA_Dyn_ind_y(uint8_t addr) noexcept {
        uint16_t base = ram[addr] | (ram[(addr + 1) & 0xFF] << 8);
        uint16_t res_addr = base + cpu.Y;

        uint8_t val = (res_addr >= 0x8000)
            ? PRG_ROM_DATA[res_addr - 0x8000]
            : ram[res_addr & 0x7FF];
        
        cpu.setZN(cpu.A = val);
    }

    // store register value to ram[addr]
    void STQ_zp(uint8_t reg, uint8_t addr) noexcept {
        ram[addr] = reg;
    }

    void STQ_zp_xy(uint8_t reg, uint8_t addr, uint8_t& offsetReg) noexcept {
        uint8_t res_addr = (addr + offsetReg) & 0xFF; // wraps in zero page
        ram[res_addr] = reg;
    }

    void STQ_abs(uint8_t reg, uint16_t addr) noexcept {
        ram[addr] = reg;
    }

    void STA_abs_xy(uint16_t addr, uint8_t& offsetReg) noexcept {
        uint16_t res_addr = addr + offsetReg;
        ram[res_addr] = cpu.A;
    }

    void STA_ind_x(uint8_t addr) noexcept {
        uint8_t ptr = (addr + cpu.X) & 0xFF; // wraps in zero page
        uint16_t res_addr = ram[ptr] | (ram[(ptr + 1) & 0xFF] << 8);

        ram[res_addr] = cpu.A;
    }

    void STA_ind_y(uint8_t addr) noexcept {
        uint16_t base = ram[addr] | (ram[(addr + 1) & 0xFF] << 8);
        uint16_t res_addr = base + cpu.Y;

        ram[res_addr] = cpu.A;
    }

    // compare register value to val
    void CMPQ_imm(uint8_t reg, uint8_t val) noexcept {
        uint8_t res = reg - val; // avoid int casting
        cpu.setZN(res);
        cpu.setC(reg, val);
    }

    void CMPQ_zp(uint8_t reg, uint8_t addr) noexcept {
        uint8_t val = ram[addr];
        uint8_t res = reg - val; // avoid int casting
        cpu.setZN(res);
        cpu.setC(reg, val);
    }

    void CMPQ_abs(uint8_t reg, uint16_t addr) noexcept {
        uint8_t val = ram[addr];
        uint8_t res = reg - val; // avoid int casting
        cpu.setZN(res);
        cpu.setC(reg, val);
    }

    void CMPA_zp_xy(uint8_t addr, uint8_t& offsetReg) noexcept {
        uint8_t val = ram[(addr + offsetReg) & 0xFF];
        uint8_t res = cpu.A - val; // avoid int casting
        cpu.setZN(res);
        cpu.setC(cpu.A, val);
    }

    void CMPA_abs_xy(uint16_t addr, uint8_t& offsetReg) noexcept {
        uint16_t res_addr = addr + offsetReg;

        uint8_t val = ram[res_addr];
        uint8_t res = cpu.A - val; // avoid int casting
        cpu.setZN(res);
        cpu.setC(cpu.A, val);
    }

    void CMPA_PRGTable(const uint8_t* table, uint8_t regAddr) noexcept {
        uint8_t val = table[regAddr];
        uint8_t res = cpu.A - val; // avoid int casting
        cpu.setZN(res);
        cpu.setC(cpu.A, val);
    }

    void CMPA_ind_x(uint8_t addr) noexcept {
        uint8_t ptr = (addr + cpu.X) & 0xFF; // wraps in zero page
        uint16_t res_addr = ram[ptr] | (ram[(ptr + 1) & 0xFF] << 8);

        uint8_t val = ram[res_addr];
        uint8_t res = cpu.A - val; // avoid int casting
        cpu.setZN(res);
        cpu.setC(cpu.A, val);
    }

    void CMPA_ind_y(uint8_t addr) noexcept {
        uint16_t base = ram[addr] | (ram[(addr + 1) & 0xFF] << 8);
        uint16_t res_addr = base + cpu.Y;

        uint8_t val = ram[res_addr];
        uint8_t res = cpu.A - val; // avoid int casting
        cpu.setZN(res);
        cpu.setC(cpu.A, val);
    }

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

    void INC_zp(uint8_t addr) noexcept {
        cpu.setZN(++ram[addr]);
    }

    void DEC_zp(uint8_t addr) noexcept {
        cpu.setZN(--ram[addr]);
    }

    void INC_zp_x(uint8_t addr) noexcept {
        uint8_t res_addr = (addr + cpu.X) & 0xFF;
        cpu.setZN(++ram[res_addr]);
    }

    void DEC_zp_x(uint8_t addr) noexcept {
        uint8_t res_addr = (addr + cpu.X) & 0xFF;
        cpu.setZN(--ram[res_addr]);
    }

    void INC_abs(uint16_t addr) noexcept {
        cpu.setZN(++ram[addr]);
    }

    void DEC_abs(uint16_t addr) noexcept {
        cpu.setZN(--ram[addr]);
    }

    void INC_abs_x(uint16_t addr) noexcept {
        uint16_t res_addr = addr + cpu.X;
        cpu.setZN(++ram[res_addr]);
    }

    void DEC_abs_x(uint16_t addr) noexcept {
        uint16_t res_addr = addr + cpu.X;
        cpu.setZN(--ram[res_addr]);
    }

    // BIT (.db 0x2c, used to optimize)
    void BIT_zp(uint8_t addr) noexcept {
        cpu.setZN_BIT(ram[addr]);
    }

    void BIT_abs(uint16_t addr) noexcept {
        cpu.setZN_BIT(ram[addr]);
    }

    // ORA
    void ORA_imm(uint8_t val) noexcept {
        cpu.setZN(cpu.A |= val);
    }

    void ORA_zp(uint8_t addr) noexcept {
        cpu.setZN(cpu.A |= ram[addr]);
    }

    void ORA_zp_xy(uint8_t addr, uint8_t& offsetReg) noexcept {
        uint8_t res_addr = (addr + offsetReg) & 0xFF; // wraps in zero page
        cpu.setZN(cpu.A |= ram[res_addr]);
    }

    void ORA_abs(uint16_t addr) noexcept {
        cpu.setZN(cpu.A |= ram[addr]);
    }

    void ORA_abs_xy(uint16_t addr, uint8_t& offsetReg) noexcept {
        uint16_t res_addr = addr + offsetReg;
        cpu.setZN(cpu.A |= ram[res_addr]);
    }

    void ORA_PRGTable(const uint8_t* table, uint8_t regAddr) noexcept {
        cpu.setZN(cpu.A |= table[regAddr]);
    }

    void ORA_ind_x(uint8_t addr) noexcept {
        uint8_t ptr = (addr + cpu.X) & 0xFF; // wraps in zero page
        uint16_t res_addr = ram[ptr] | (ram[(ptr + 1) & 0xFF] << 8);

        cpu.setZN(cpu.A |= ram[res_addr]);
    }

    void ORA_ind_y(uint8_t addr) noexcept {
        uint16_t base = ram[addr] | (ram[(addr + 1) & 0xFF] << 8);
        uint16_t res_addr = base + cpu.Y;

        cpu.setZN(cpu.A |= ram[res_addr]);
    }

    // AND
    void AND_imm(uint8_t val) noexcept {
        cpu.setZN(cpu.A &= val);
    }

    void AND_zp(uint8_t addr) noexcept {
        cpu.setZN(cpu.A &= ram[addr]);
    }

    void AND_zp_xy(uint8_t addr, uint8_t& offsetReg) noexcept {
        uint8_t res_addr = (addr + offsetReg) & 0xFF; // wraps in zero page
        cpu.setZN(cpu.A &= ram[res_addr]);
    }

    void AND_abs(uint16_t addr) noexcept {
        cpu.setZN(cpu.A &= ram[addr]);
    }

    void AND_abs_xy(uint16_t addr, uint8_t& offsetReg) noexcept {
        uint16_t res_addr = addr + offsetReg;
        cpu.setZN(cpu.A &= ram[res_addr]);
    }

    void AND_PRGTable(const uint8_t* table, uint8_t regAddr) noexcept {
        uint8_t val = table[regAddr];
        cpu.setZN(cpu.A &= val);
    }

    void AND_ind_x(uint8_t addr) noexcept {
        uint8_t ptr = (addr + cpu.X) & 0xFF; // wraps in zero page
        uint16_t res_addr = ram[ptr] | (ram[(ptr + 1) & 0xFF] << 8);

        cpu.setZN(cpu.A &= ram[res_addr]);
    }

    void AND_ind_y(uint8_t addr) noexcept {
        uint16_t base = ram[addr] | (ram[(addr + 1) & 0xFF] << 8);
        uint16_t res_addr = base + cpu.Y;

        cpu.setZN(cpu.A &= ram[res_addr]);
    }

    // EOR
    void EOR_imm(uint8_t val) noexcept {
        cpu.setZN(cpu.A ^= val);
    }

    void EOR_zp(uint8_t addr) noexcept {
        cpu.setZN(cpu.A ^= ram[addr]);
    }

    void EOR_zp_xy(uint8_t addr, uint8_t& offsetReg) noexcept {
        uint8_t res_addr = (addr + offsetReg) & 0xFF; // wraps in zero page
        cpu.setZN(cpu.A ^= ram[res_addr]);
    }

    void EOR_abs(uint16_t addr) noexcept {
        cpu.setZN(cpu.A ^= ram[addr]);
    }

    void EOR_abs_xy(uint16_t addr, uint8_t& offsetReg) noexcept {
        uint16_t res_addr = addr + offsetReg;
        cpu.setZN(cpu.A ^= ram[res_addr]);
    }

    void EOR_ind_x(uint8_t addr) noexcept {
        uint8_t ptr = (addr + cpu.X) & 0xFF; // wraps in zero page
        uint16_t res_addr = ram[ptr] | (ram[(ptr + 1) & 0xFF] << 8);

        cpu.setZN(cpu.A ^= ram[res_addr]);
    }

    void EOR_ind_y(uint8_t addr) noexcept {
        uint16_t base = ram[addr] | (ram[(addr + 1) & 0xFF] << 8);
        uint16_t res_addr = base + cpu.Y;

        cpu.setZN(cpu.A ^= ram[res_addr]);
    }

    // PHA and PLA
    void PHA() noexcept {
        ram[0x100 + cpu.SP--] = cpu.A;
    }

    void PLA() noexcept {
        cpu.setZN(cpu.A = ram[0x100 + ++cpu.SP]);
    }

    // ASL
    void ASL_acc() noexcept {
        cpu.flag_C = (cpu.A >> 7) & 1;
        cpu.setZN(cpu.A <<= 1);
    }

    void ASL_zp(uint8_t addr) noexcept {
        cpu.flag_C = (ram[addr] >> 7) & 1;
        cpu.setZN(ram[addr] <<= 1);
    }

    void ASL_zp_x(uint8_t addr) noexcept {
        uint8_t res_addr = (addr + cpu.X) & 0xFF;
        cpu.flag_C = (ram[res_addr] >> 7) & 1;
        cpu.setZN(ram[res_addr] <<= 1);
    }

    void ASL_abs(uint16_t addr) noexcept {
        cpu.flag_C = (ram[addr] >> 7) & 1;
        cpu.setZN(ram[addr] <<= 1);
    }

    void ASL_abs_x(uint16_t addr) noexcept {
        uint16_t res_addr = addr + cpu.X;
        cpu.flag_C = (ram[res_addr] >> 7) & 1;
        cpu.setZN(ram[res_addr] <<= 1);
    }

    // LSR
    void LSR_acc() noexcept {
        cpu.flag_C = cpu.A & 1;
        cpu.setZN(cpu.A >>= 1);
    }

    void LSR_zp(uint8_t addr) noexcept {
        cpu.flag_C = ram[addr] & 1;
        cpu.setZN(ram[addr] >>= 1);
    }

    void LSR_zp_x(uint8_t addr) noexcept {
        uint8_t res_addr = (addr + cpu.X) & 0xFF;
        cpu.flag_C = ram[res_addr] & 1;
        cpu.setZN(ram[res_addr] >>= 1);
    }

    void LSR_abs(uint16_t addr) noexcept {
        cpu.flag_C = ram[addr] & 1;
        cpu.setZN(ram[addr] >>= 1);
    }

    void LSR_abs_x(uint16_t addr) noexcept {
        uint16_t res_addr = addr + cpu.X;
        cpu.flag_C = ram[res_addr] & 1;
        cpu.setZN(ram[res_addr] >>= 1);
    }

    // ROL
    void ROL_acc() noexcept {
        bool oldCarry = cpu.flag_C;

        cpu.flag_C = (cpu.A >> 7) & 1;
        cpu.A = (cpu.A << 1) | oldCarry;
        cpu.setZN(cpu.A);
    }

    void ROL_zp(uint8_t addr) noexcept {
        bool oldCarry = cpu.flag_C;

        cpu.flag_C = (ram[addr] >> 7) & 1;
        ram[addr] = (ram[addr] << 1) | oldCarry;
        cpu.setZN(ram[addr]);
    }

    void ROL_zp_x(uint8_t addr) noexcept {
        uint8_t res_addr = (addr + cpu.X) & 0xFF;
        bool oldCarry = cpu.flag_C;

        cpu.flag_C = (ram[res_addr] >> 7) & 1;
        ram[res_addr] = (ram[res_addr] << 1) | oldCarry;
        cpu.setZN(ram[res_addr]);
    }

    void ROL_abs(uint16_t addr) noexcept {
        bool oldCarry = cpu.flag_C;

        cpu.flag_C = (ram[addr] >> 7) & 1;
        ram[addr] = (ram[addr] << 1) | oldCarry;
        cpu.setZN(ram[addr]);
    }

    void ROL_abs_x(uint16_t addr) noexcept {
        uint16_t res_addr = addr + cpu.X;
        bool oldCarry = cpu.flag_C;

        cpu.flag_C = (ram[res_addr] >> 7) & 1;
        ram[res_addr] = (ram[res_addr] << 1) | oldCarry;
        cpu.setZN(ram[res_addr]);
    }

    // ROR
    void ROR_acc() noexcept {
        bool oldCarry = cpu.flag_C;

        cpu.flag_C = cpu.A & 1;
        cpu.A = (cpu.A >> 1) | (oldCarry << 7);
        cpu.setZN(cpu.A);
    }

    void ROR_zp(uint8_t addr) noexcept {
        bool oldCarry = cpu.flag_C;

        cpu.flag_C = ram[addr] & 1;
        ram[addr] = (ram[addr] >> 1) | (oldCarry << 7);
        cpu.setZN(ram[addr]);
    }

    void ROR_zp_x(uint8_t addr) noexcept {
        uint8_t res_addr = (addr + cpu.X) & 0xFF;
        bool oldCarry = cpu.flag_C;

        cpu.flag_C = ram[res_addr] & 1;
        ram[res_addr] = (ram[res_addr] >> 1) | (oldCarry << 7);
        cpu.setZN(ram[res_addr]);
    }

    void ROR_abs(uint16_t addr) noexcept {
        bool oldCarry = cpu.flag_C;

        cpu.flag_C = ram[addr] & 1;
        ram[addr] = (ram[addr] >> 1) | (oldCarry << 7);
        cpu.setZN(ram[addr]);
    }

    void ROR_abs_x(uint16_t addr) noexcept {
        uint16_t res_addr = addr + cpu.X;
        bool oldCarry = cpu.flag_C;

        cpu.flag_C = ram[res_addr] & 1;
        ram[res_addr] = (ram[res_addr] >> 1) | (oldCarry << 7);
        cpu.setZN(ram[res_addr]);
    }

    // SEC and CLC
    void SEC() noexcept { cpu.flag_C = true; }
    void CLC() noexcept { cpu.flag_C = false; }

    // ADC
    void ADC_imm(uint8_t val) noexcept {
        uint16_t sum = cpu.A + val + cpu.flag_C;

        cpu.flag_C = sum > 0xFF;
        cpu.A = sum & 0xFF;
        cpu.setZN(cpu.A);
    }

    void ADC_zp(uint8_t addr) noexcept {
        uint16_t sum = cpu.A + ram[addr] + cpu.flag_C;

        cpu.flag_C = sum > 0xFF;
        cpu.A = sum & 0xFF;
        cpu.setZN(cpu.A);
    }

    void ADC_zp_x(uint8_t addr) noexcept {
        uint8_t res_addr = (addr + cpu.X) & 0xFF;
        uint16_t sum = cpu.A + ram[res_addr] + cpu.flag_C;

        cpu.flag_C = sum > 0xFF;
        cpu.A = sum & 0xFF;
        cpu.setZN(cpu.A);
    }

    void ADC_abs(uint16_t addr) noexcept {
        uint16_t sum = cpu.A + ram[addr] + cpu.flag_C;

        cpu.flag_C = sum > 0xFF;
        cpu.A = sum & 0xFF;
        cpu.setZN(cpu.A);
    }

    void ADC_abs_x(uint16_t addr) noexcept {
        uint16_t res_addr = addr + cpu.X;
        uint16_t sum = cpu.A + ram[res_addr] + cpu.flag_C;

        cpu.flag_C = sum > 0xFF;
        cpu.A = sum & 0xFF;
        cpu.setZN(cpu.A);
    }

    void ADC_abs_y(uint16_t addr) noexcept {
        uint16_t res_addr = addr + cpu.Y;
        uint16_t sum = cpu.A + ram[res_addr] + cpu.flag_C;

        cpu.flag_C = sum > 0xFF;
        cpu.A = sum & 0xFF;
        cpu.setZN(cpu.A);
    }

    void ADC_PRGTable(const uint8_t* table, uint8_t regAddr) noexcept {
        uint16_t sum = cpu.A + table[regAddr] + cpu.flag_C;

        cpu.flag_C = sum > 0xFF;
        cpu.A = sum & 0xFF;
        cpu.setZN(cpu.A);
    }

    void ADC_ind_x(uint8_t addr) noexcept {
        uint8_t ptr = (addr + cpu.X) & 0xFF;
        uint16_t res_addr = ram[ptr] | (ram[(ptr + 1) & 0xFF] << 8);
        uint16_t sum = cpu.A + ram[res_addr] + cpu.flag_C;

        cpu.flag_C = sum > 0xFF;
        cpu.A = sum & 0xFF;
        cpu.setZN(cpu.A);
    }

    void ADC_ind_y(uint8_t addr) noexcept {
        uint16_t base = ram[addr] | (ram[(addr + 1) & 0xFF] << 8);
        uint16_t res_addr = base + cpu.Y;
        uint16_t sum = cpu.A + ram[res_addr] + cpu.flag_C;

        cpu.flag_C = sum > 0xFF;
        cpu.A = sum & 0xFF;
        cpu.setZN(cpu.A);
    }

    // SBC
    void SBC_imm(uint8_t val) noexcept {
        uint16_t sum = cpu.A + static_cast<uint8_t>(~val) + cpu.flag_C;

        cpu.flag_C = sum > 0xFF;
        cpu.A = sum & 0xFF;
        cpu.setZN(cpu.A);
    }

    void SBC_zp(uint8_t addr) noexcept {
        uint16_t sum = cpu.A + static_cast<uint8_t>(~ram[addr]) + cpu.flag_C;

        cpu.flag_C = sum > 0xFF;
        cpu.A = sum & 0xFF;
        cpu.setZN(cpu.A);
    }

    void SBC_zp_x(uint8_t addr) noexcept {
        uint8_t res_addr = (addr + cpu.X) & 0xFF;
        uint16_t sum = cpu.A + static_cast<uint8_t>(~ram[res_addr]) + cpu.flag_C;

        cpu.flag_C = sum > 0xFF;
        cpu.A = sum & 0xFF;
        cpu.setZN(cpu.A);
    }

    void SBC_abs(uint16_t addr) noexcept {
        uint16_t sum = cpu.A + static_cast<uint8_t>(~ram[addr]) + cpu.flag_C;

        cpu.flag_C = sum > 0xFF;
        cpu.A = sum & 0xFF;
        cpu.setZN(cpu.A);
    }

    void SBC_abs_x(uint16_t addr) noexcept {
        uint16_t res_addr = addr + cpu.X;
        uint16_t sum = cpu.A + static_cast<uint8_t>(~ram[res_addr]) + cpu.flag_C;

        cpu.flag_C = sum > 0xFF;
        cpu.A = sum & 0xFF;
        cpu.setZN(cpu.A);
    }

    void SBC_abs_y(uint16_t addr) noexcept {
        uint16_t res_addr = addr + cpu.Y;
        uint16_t sum = cpu.A + static_cast<uint8_t>(~ram[res_addr]) + cpu.flag_C;

        cpu.flag_C = sum > 0xFF;
        cpu.A = sum & 0xFF;
        cpu.setZN(cpu.A);
    }

    void SBC_PRGTable(const uint8_t* table, uint8_t regAddr) noexcept {
        uint8_t val = table[regAddr];
        uint16_t sum = cpu.A + static_cast<uint8_t>(~val) + cpu.flag_C;

        cpu.flag_C = sum > 0xFF;
        cpu.A = sum & 0xFF;
        cpu.setZN(cpu.A);
    }

    void SBC_ind_x(uint8_t addr) noexcept {
        uint8_t ptr = (addr + cpu.X) & 0xFF;
        uint16_t res_addr = ram[ptr] | (ram[(ptr + 1) & 0xFF] << 8);
        uint16_t sum = cpu.A + static_cast<uint8_t>(~ram[res_addr]) + cpu.flag_C;

        cpu.flag_C = sum > 0xFF;
        cpu.A = sum & 0xFF;
        cpu.setZN(cpu.A);
    }

    void SBC_ind_y(uint8_t addr) noexcept {
        uint16_t base = ram[addr] | (ram[(addr + 1) & 0xFF] << 8);
        uint16_t res_addr = base + cpu.Y;
        uint16_t sum = cpu.A + static_cast<uint8_t>(~ram[res_addr]) + cpu.flag_C;

        cpu.flag_C = sum > 0xFF;
        cpu.A = sum & 0xFF;
        cpu.setZN(cpu.A);
    }
};