#include <smb/core/environment.hpp>
#include <smb/data/game_data.hpp>
#include <cstdint>

// source: https://www.nesdev.org/obelisk-6502-guide/reference.html

// the letter Q was used as a placeholder for "any register or memory address",
// (e.g. LDQ_imm is used for LDA, LDX and LDY in immediate mode)
void SMBEnv::LDQ_imm(uint8_t& reg, uint8_t val) noexcept {
    cpu.setZN(reg = val);
}

void SMBEnv::LDQ_zp(uint8_t& reg, uint8_t addr) noexcept {
    cpu.setZN(reg = ram[addr]);
}

void SMBEnv::LDQ_zp_xy(uint8_t& reg, uint8_t addr, uint8_t& offsetReg) noexcept {
    uint8_t res_addr = (addr + offsetReg) & 0xFF; // wraps in zero page
    cpu.setZN(reg = ram[res_addr]);
}

void SMBEnv::LDQ_abs(uint8_t& reg, uint16_t addr) noexcept {
    cpu.setZN(reg = ram[addr]);
}

void SMBEnv::LDQ_PRGTable(uint8_t& reg, const uint8_t* table, uint8_t regAddr) noexcept {
    cpu.setZN(reg = table[regAddr]);
}

void SMBEnv::LDQ_abs_xy(uint8_t& reg, uint16_t addr, uint8_t& offsetReg) noexcept {
    uint16_t res_addr = addr + offsetReg;
    cpu.setZN(reg = ram[res_addr]);
}

void SMBEnv::LDA_ind_x(uint8_t addr) noexcept {
    uint8_t ptr = (addr + cpu.X) & 0xFF; // wraps in zero page
    uint16_t res_addr = ram[ptr] | (ram[(ptr + 1) & 0xFF] << 8);

    cpu.setZN(cpu.A = ram[res_addr]);
}

void SMBEnv::LDA_ind_y(uint8_t addr) noexcept {
    uint16_t base = ram[addr] | (ram[(addr + 1) & 0xFF] << 8);
    uint16_t res_addr = base + cpu.Y;

    cpu.setZN(cpu.A = ram[res_addr]);
}

// old implementation that only reads from PRG_ROM_DATA, it could work in cases where LDA_Dyn_ind_y
// exclusively reads from ROM DATA and not RAM in the same instruction (and viceversa with LDA_ind_y)
// void SMBEnv::LDA_PRG_ind_y(uint8_t addr) noexcept {
//     uint16_t base = ram[addr] | (ram[(addr + 1) & 0xFF] << 8);
//     uint16_t res_addr = base + cpu.Y;

//     cpu.setZN(cpu.A = PRG_ROM_DATA[res_addr - 0x8000]);
// }

// read RAM or PRG dynamically
void SMBEnv::LDA_Dyn_ind_y(uint8_t addr) noexcept {
    uint16_t base = ram[addr] | (ram[(addr + 1) & 0xFF] << 8);
    uint16_t res_addr = base + cpu.Y;

    uint8_t val = (res_addr >= 0x8000)
        ? PRG_ROM_DATA[res_addr - 0x8000]
        : ram[res_addr & 0x7FF];
    
    cpu.setZN(cpu.A = val);
}

void SMBEnv::STQ_zp(uint8_t reg, uint8_t addr) noexcept {
    ram[addr] = reg;
}

void SMBEnv::STQ_zp_xy(uint8_t reg, uint8_t addr, uint8_t& offsetReg) noexcept {
    uint8_t res_addr = (addr + offsetReg) & 0xFF; // wraps in zero page
    ram[res_addr] = reg;
}

void SMBEnv::STQ_abs(uint8_t reg, uint16_t addr) noexcept {
    ram[addr] = reg;
}

void SMBEnv::STA_abs_xy(uint16_t addr, uint8_t& offsetReg) noexcept {
    uint16_t res_addr = addr + offsetReg;
    ram[res_addr] = cpu.A;
}

void SMBEnv::STA_ind_x(uint8_t addr) noexcept {
    uint8_t ptr = (addr + cpu.X) & 0xFF; // wraps in zero page
    uint16_t res_addr = ram[ptr] | (ram[(ptr + 1) & 0xFF] << 8);

    ram[res_addr] = cpu.A;
}

void SMBEnv::STA_ind_y(uint8_t addr) noexcept {
    uint16_t base = ram[addr] | (ram[(addr + 1) & 0xFF] << 8);
    uint16_t res_addr = base + cpu.Y;

    ram[res_addr] = cpu.A;
}

void SMBEnv::CMPQ_imm(uint8_t reg, uint8_t val) noexcept {
    uint8_t res = reg - val; // avoid int casting
    cpu.setZN(res);
    cpu.setC(reg, val);
}

void SMBEnv::CMPQ_zp(uint8_t reg, uint8_t addr) noexcept {
    uint8_t val = ram[addr];
    uint8_t res = reg - val; // avoid int casting
    cpu.setZN(res);
    cpu.setC(reg, val);
}

void SMBEnv::CMPQ_abs(uint8_t reg, uint16_t addr) noexcept {
    uint8_t val = ram[addr];
    uint8_t res = reg - val; // avoid int casting
    cpu.setZN(res);
    cpu.setC(reg, val);
}

void SMBEnv::CMPA_zp_xy(uint8_t addr, uint8_t& offsetReg) noexcept {
    uint8_t val = ram[(addr + offsetReg) & 0xFF];
    uint8_t res = cpu.A - val; // avoid int casting
    cpu.setZN(res);
    cpu.setC(cpu.A, val);
}

void SMBEnv::CMPA_abs_xy(uint16_t addr, uint8_t& offsetReg) noexcept {
    uint16_t res_addr = addr + offsetReg;

    uint8_t val = ram[res_addr];
    uint8_t res = cpu.A - val; // avoid int casting
    cpu.setZN(res);
    cpu.setC(cpu.A, val);
}

void SMBEnv::CMPA_PRGTable(const uint8_t* table, uint8_t regAddr) noexcept {
    uint8_t val = table[regAddr];
    uint8_t res = cpu.A - val; // avoid int casting
    cpu.setZN(res);
    cpu.setC(cpu.A, val);
}

void SMBEnv::CMPA_ind_x(uint8_t addr) noexcept {
    uint8_t ptr = (addr + cpu.X) & 0xFF; // wraps in zero page
    uint16_t res_addr = ram[ptr] | (ram[(ptr + 1) & 0xFF] << 8);

    uint8_t val = ram[res_addr];
    uint8_t res = cpu.A - val; // avoid int casting
    cpu.setZN(res);
    cpu.setC(cpu.A, val);
}

void SMBEnv::CMPA_ind_y(uint8_t addr) noexcept {
    uint16_t base = ram[addr] | (ram[(addr + 1) & 0xFF] << 8);
    uint16_t res_addr = base + cpu.Y;

    uint8_t val = ram[res_addr];
    uint8_t res = cpu.A - val; // avoid int casting
    cpu.setZN(res);
    cpu.setC(cpu.A, val);
}

void SMBEnv::INC_zp(uint8_t addr) noexcept {
    cpu.setZN(++ram[addr]);
}

void SMBEnv::DEC_zp(uint8_t addr) noexcept {
    cpu.setZN(--ram[addr]);
}

void SMBEnv::INC_zp_x(uint8_t addr) noexcept {
    uint8_t res_addr = (addr + cpu.X) & 0xFF;
    cpu.setZN(++ram[res_addr]);
}

void SMBEnv::DEC_zp_x(uint8_t addr) noexcept {
    uint8_t res_addr = (addr + cpu.X) & 0xFF;
    cpu.setZN(--ram[res_addr]);
}

void SMBEnv::INC_abs(uint16_t addr) noexcept {
    cpu.setZN(++ram[addr]);
}

void SMBEnv::DEC_abs(uint16_t addr) noexcept {
    cpu.setZN(--ram[addr]);
}

void SMBEnv::INC_abs_x(uint16_t addr) noexcept {
    uint16_t res_addr = addr + cpu.X;
    cpu.setZN(++ram[res_addr]);
}

void SMBEnv::DEC_abs_x(uint16_t addr) noexcept {
    uint16_t res_addr = addr + cpu.X;
    cpu.setZN(--ram[res_addr]);
}

void SMBEnv::BIT_zp(uint8_t addr) noexcept {
    cpu.setZN_BIT(ram[addr]);
}

void SMBEnv::BIT_abs(uint16_t addr) noexcept {
    cpu.setZN_BIT(ram[addr]);
}

void SMBEnv::ORA_imm(uint8_t val) noexcept {
    cpu.setZN(cpu.A |= val);
}

void SMBEnv::ORA_zp(uint8_t addr) noexcept {
    cpu.setZN(cpu.A |= ram[addr]);
}

void SMBEnv::ORA_zp_xy(uint8_t addr, uint8_t& offsetReg) noexcept {
    uint8_t res_addr = (addr + offsetReg) & 0xFF; // wraps in zero page
    cpu.setZN(cpu.A |= ram[res_addr]);
}

void SMBEnv::ORA_abs(uint16_t addr) noexcept {
    cpu.setZN(cpu.A |= ram[addr]);
}

void SMBEnv::ORA_abs_xy(uint16_t addr, uint8_t& offsetReg) noexcept {
    uint16_t res_addr = addr + offsetReg;
    cpu.setZN(cpu.A |= ram[res_addr]);
}

void SMBEnv::ORA_PRGTable(const uint8_t* table, uint8_t regAddr) noexcept {
    cpu.setZN(cpu.A |= table[regAddr]);
}

void SMBEnv::ORA_ind_x(uint8_t addr) noexcept {
    uint8_t ptr = (addr + cpu.X) & 0xFF; // wraps in zero page
    uint16_t res_addr = ram[ptr] | (ram[(ptr + 1) & 0xFF] << 8);

    cpu.setZN(cpu.A |= ram[res_addr]);
}

void SMBEnv::ORA_ind_y(uint8_t addr) noexcept {
    uint16_t base = ram[addr] | (ram[(addr + 1) & 0xFF] << 8);
    uint16_t res_addr = base + cpu.Y;

    cpu.setZN(cpu.A |= ram[res_addr]);
}

void SMBEnv::AND_imm(uint8_t val) noexcept {
    cpu.setZN(cpu.A &= val);
}

void SMBEnv::AND_zp(uint8_t addr) noexcept {
    cpu.setZN(cpu.A &= ram[addr]);
}

void SMBEnv::AND_zp_xy(uint8_t addr, uint8_t& offsetReg) noexcept {
    uint8_t res_addr = (addr + offsetReg) & 0xFF; // wraps in zero page
    cpu.setZN(cpu.A &= ram[res_addr]);
}

void SMBEnv::AND_abs(uint16_t addr) noexcept {
    cpu.setZN(cpu.A &= ram[addr]);
}

void SMBEnv::AND_abs_xy(uint16_t addr, uint8_t& offsetReg) noexcept {
    uint16_t res_addr = addr + offsetReg;
    cpu.setZN(cpu.A &= ram[res_addr]);
}

void SMBEnv::AND_PRGTable(const uint8_t* table, uint8_t regAddr) noexcept {
    uint8_t val = table[regAddr];
    cpu.setZN(cpu.A &= val);
}

void SMBEnv::AND_ind_x(uint8_t addr) noexcept {
    uint8_t ptr = (addr + cpu.X) & 0xFF; // wraps in zero page
    uint16_t res_addr = ram[ptr] | (ram[(ptr + 1) & 0xFF] << 8);

    cpu.setZN(cpu.A &= ram[res_addr]);
}

void SMBEnv::AND_ind_y(uint8_t addr) noexcept {
    uint16_t base = ram[addr] | (ram[(addr + 1) & 0xFF] << 8);
    uint16_t res_addr = base + cpu.Y;

    cpu.setZN(cpu.A &= ram[res_addr]);
}

void SMBEnv::EOR_imm(uint8_t val) noexcept {
    cpu.setZN(cpu.A ^= val);
}

void SMBEnv::EOR_zp(uint8_t addr) noexcept {
    cpu.setZN(cpu.A ^= ram[addr]);
}

void SMBEnv::EOR_zp_xy(uint8_t addr, uint8_t& offsetReg) noexcept {
    uint8_t res_addr = (addr + offsetReg) & 0xFF; // wraps in zero page
    cpu.setZN(cpu.A ^= ram[res_addr]);
}

void SMBEnv::EOR_abs(uint16_t addr) noexcept {
    cpu.setZN(cpu.A ^= ram[addr]);
}

void SMBEnv::EOR_abs_xy(uint16_t addr, uint8_t& offsetReg) noexcept {
    uint16_t res_addr = addr + offsetReg;
    cpu.setZN(cpu.A ^= ram[res_addr]);
}

void SMBEnv::EOR_ind_x(uint8_t addr) noexcept {
    uint8_t ptr = (addr + cpu.X) & 0xFF; // wraps in zero page
    uint16_t res_addr = ram[ptr] | (ram[(ptr + 1) & 0xFF] << 8);

    cpu.setZN(cpu.A ^= ram[res_addr]);
}

void SMBEnv::EOR_ind_y(uint8_t addr) noexcept {
    uint16_t base = ram[addr] | (ram[(addr + 1) & 0xFF] << 8);
    uint16_t res_addr = base + cpu.Y;

    cpu.setZN(cpu.A ^= ram[res_addr]);
}

void SMBEnv::PHA() noexcept {
    ram[0x100 + cpu.SP--] = cpu.A;
}

void SMBEnv::PLA() noexcept {
    cpu.setZN(cpu.A = ram[0x100 + ++cpu.SP]);
}

void SMBEnv::ASL_acc() noexcept {
    cpu.flag_C = (cpu.A >> 7) & 1;
    cpu.setZN(cpu.A <<= 1);
}

void SMBEnv::ASL_zp(uint8_t addr) noexcept {
    cpu.flag_C = (ram[addr] >> 7) & 1;
    cpu.setZN(ram[addr] <<= 1);
}

void SMBEnv::ASL_zp_x(uint8_t addr) noexcept {
    uint8_t res_addr = (addr + cpu.X) & 0xFF;
    cpu.flag_C = (ram[res_addr] >> 7) & 1;
    cpu.setZN(ram[res_addr] <<= 1);
}

void SMBEnv::ASL_abs(uint16_t addr) noexcept {
    cpu.flag_C = (ram[addr] >> 7) & 1;
    cpu.setZN(ram[addr] <<= 1);
}

void SMBEnv::ASL_abs_x(uint16_t addr) noexcept {
    uint16_t res_addr = addr + cpu.X;
    cpu.flag_C = (ram[res_addr] >> 7) & 1;
    cpu.setZN(ram[res_addr] <<= 1);
}

void SMBEnv::LSR_acc() noexcept {
    cpu.flag_C = cpu.A & 1;
    cpu.setZN(cpu.A >>= 1);
}

void SMBEnv::LSR_zp(uint8_t addr) noexcept {
    cpu.flag_C = ram[addr] & 1;
    cpu.setZN(ram[addr] >>= 1);
}

void SMBEnv::LSR_zp_x(uint8_t addr) noexcept {
    uint8_t res_addr = (addr + cpu.X) & 0xFF;
    cpu.flag_C = ram[res_addr] & 1;
    cpu.setZN(ram[res_addr] >>= 1);
}

void SMBEnv::LSR_abs(uint16_t addr) noexcept {
    cpu.flag_C = ram[addr] & 1;
    cpu.setZN(ram[addr] >>= 1);
}

void SMBEnv::LSR_abs_x(uint16_t addr) noexcept {
    uint16_t res_addr = addr + cpu.X;
    cpu.flag_C = ram[res_addr] & 1;
    cpu.setZN(ram[res_addr] >>= 1);
}

void SMBEnv::ROL_acc() noexcept {
    bool oldCarry = cpu.flag_C;

    cpu.flag_C = (cpu.A >> 7) & 1;
    cpu.A = (cpu.A << 1) | oldCarry;
    cpu.setZN(cpu.A);
}

void SMBEnv::ROL_zp(uint8_t addr) noexcept {
    bool oldCarry = cpu.flag_C;

    cpu.flag_C = (ram[addr] >> 7) & 1;
    ram[addr] = (ram[addr] << 1) | oldCarry;
    cpu.setZN(ram[addr]);
}

void SMBEnv::ROL_zp_x(uint8_t addr) noexcept {
    uint8_t res_addr = (addr + cpu.X) & 0xFF;
    bool oldCarry = cpu.flag_C;

    cpu.flag_C = (ram[res_addr] >> 7) & 1;
    ram[res_addr] = (ram[res_addr] << 1) | oldCarry;
    cpu.setZN(ram[res_addr]);
}

void SMBEnv::ROL_abs(uint16_t addr) noexcept {
    bool oldCarry = cpu.flag_C;

    cpu.flag_C = (ram[addr] >> 7) & 1;
    ram[addr] = (ram[addr] << 1) | oldCarry;
    cpu.setZN(ram[addr]);
}

void SMBEnv::ROL_abs_x(uint16_t addr) noexcept {
    uint16_t res_addr = addr + cpu.X;
    bool oldCarry = cpu.flag_C;

    cpu.flag_C = (ram[res_addr] >> 7) & 1;
    ram[res_addr] = (ram[res_addr] << 1) | oldCarry;
    cpu.setZN(ram[res_addr]);
}

void SMBEnv::ROR_acc() noexcept {
    bool oldCarry = cpu.flag_C;

    cpu.flag_C = cpu.A & 1;
    cpu.A = (cpu.A >> 1) | (oldCarry << 7);
    cpu.setZN(cpu.A);
}

void SMBEnv::ROR_zp(uint8_t addr) noexcept {
    bool oldCarry = cpu.flag_C;

    cpu.flag_C = ram[addr] & 1;
    ram[addr] = (ram[addr] >> 1) | (oldCarry << 7);
    cpu.setZN(ram[addr]);
}

void SMBEnv::ROR_zp_x(uint8_t addr) noexcept {
    uint8_t res_addr = (addr + cpu.X) & 0xFF;
    bool oldCarry = cpu.flag_C;

    cpu.flag_C = ram[res_addr] & 1;
    ram[res_addr] = (ram[res_addr] >> 1) | (oldCarry << 7);
    cpu.setZN(ram[res_addr]);
}

void SMBEnv::ROR_abs(uint16_t addr) noexcept {
    bool oldCarry = cpu.flag_C;

    cpu.flag_C = ram[addr] & 1;
    ram[addr] = (ram[addr] >> 1) | (oldCarry << 7);
    cpu.setZN(ram[addr]);
}

void SMBEnv::ROR_abs_x(uint16_t addr) noexcept {
    uint16_t res_addr = addr + cpu.X;
    bool oldCarry = cpu.flag_C;

    cpu.flag_C = ram[res_addr] & 1;
    ram[res_addr] = (ram[res_addr] >> 1) | (oldCarry << 7);
    cpu.setZN(ram[res_addr]);
}

void SMBEnv::ADC_imm(uint8_t val) noexcept {
    uint16_t sum = cpu.A + val + cpu.flag_C;

    cpu.flag_C = sum > 0xFF;
    cpu.A = sum & 0xFF;
    cpu.setZN(cpu.A);
}

void SMBEnv::ADC_zp(uint8_t addr) noexcept {
    uint16_t sum = cpu.A + ram[addr] + cpu.flag_C;

    cpu.flag_C = sum > 0xFF;
    cpu.A = sum & 0xFF;
    cpu.setZN(cpu.A);
}

void SMBEnv::ADC_zp_x(uint8_t addr) noexcept {
    uint8_t res_addr = (addr + cpu.X) & 0xFF;
    uint16_t sum = cpu.A + ram[res_addr] + cpu.flag_C;

    cpu.flag_C = sum > 0xFF;
    cpu.A = sum & 0xFF;
    cpu.setZN(cpu.A);
}

void SMBEnv::ADC_abs(uint16_t addr) noexcept {
    uint16_t sum = cpu.A + ram[addr] + cpu.flag_C;

    cpu.flag_C = sum > 0xFF;
    cpu.A = sum & 0xFF;
    cpu.setZN(cpu.A);
}

void SMBEnv::ADC_abs_x(uint16_t addr) noexcept {
    uint16_t res_addr = addr + cpu.X;
    uint16_t sum = cpu.A + ram[res_addr] + cpu.flag_C;

    cpu.flag_C = sum > 0xFF;
    cpu.A = sum & 0xFF;
    cpu.setZN(cpu.A);
}

void SMBEnv::ADC_abs_y(uint16_t addr) noexcept {
    uint16_t res_addr = addr + cpu.Y;
    uint16_t sum = cpu.A + ram[res_addr] + cpu.flag_C;

    cpu.flag_C = sum > 0xFF;
    cpu.A = sum & 0xFF;
    cpu.setZN(cpu.A);
}

void SMBEnv::ADC_PRGTable(const uint8_t* table, uint8_t regAddr) noexcept {
    uint16_t sum = cpu.A + table[regAddr] + cpu.flag_C;

    cpu.flag_C = sum > 0xFF;
    cpu.A = sum & 0xFF;
    cpu.setZN(cpu.A);
}

void SMBEnv::ADC_ind_x(uint8_t addr) noexcept {
    uint8_t ptr = (addr + cpu.X) & 0xFF;
    uint16_t res_addr = ram[ptr] | (ram[(ptr + 1) & 0xFF] << 8);
    uint16_t sum = cpu.A + ram[res_addr] + cpu.flag_C;

    cpu.flag_C = sum > 0xFF;
    cpu.A = sum & 0xFF;
    cpu.setZN(cpu.A);
}

void SMBEnv::ADC_ind_y(uint8_t addr) noexcept {
    uint16_t base = ram[addr] | (ram[(addr + 1) & 0xFF] << 8);
    uint16_t res_addr = base + cpu.Y;
    uint16_t sum = cpu.A + ram[res_addr] + cpu.flag_C;

    cpu.flag_C = sum > 0xFF;
    cpu.A = sum & 0xFF;
    cpu.setZN(cpu.A);
}

void SMBEnv::SBC_imm(uint8_t val) noexcept {
    uint16_t sum = cpu.A + static_cast<uint8_t>(~val) + cpu.flag_C;

    cpu.flag_C = sum > 0xFF;
    cpu.A = sum & 0xFF;
    cpu.setZN(cpu.A);
}

void SMBEnv::SBC_zp(uint8_t addr) noexcept {
    uint16_t sum = cpu.A + static_cast<uint8_t>(~ram[addr]) + cpu.flag_C;

    cpu.flag_C = sum > 0xFF;
    cpu.A = sum & 0xFF;
    cpu.setZN(cpu.A);
}

void SMBEnv::SBC_zp_x(uint8_t addr) noexcept {
    uint8_t res_addr = (addr + cpu.X) & 0xFF;
    uint16_t sum = cpu.A + static_cast<uint8_t>(~ram[res_addr]) + cpu.flag_C;

    cpu.flag_C = sum > 0xFF;
    cpu.A = sum & 0xFF;
    cpu.setZN(cpu.A);
}

void SMBEnv::SBC_abs(uint16_t addr) noexcept {
    uint16_t sum = cpu.A + static_cast<uint8_t>(~ram[addr]) + cpu.flag_C;

    cpu.flag_C = sum > 0xFF;
    cpu.A = sum & 0xFF;
    cpu.setZN(cpu.A);
}

void SMBEnv::SBC_abs_x(uint16_t addr) noexcept {
    uint16_t res_addr = addr + cpu.X;
    uint16_t sum = cpu.A + static_cast<uint8_t>(~ram[res_addr]) + cpu.flag_C;

    cpu.flag_C = sum > 0xFF;
    cpu.A = sum & 0xFF;
    cpu.setZN(cpu.A);
}

void SMBEnv::SBC_abs_y(uint16_t addr) noexcept {
    uint16_t res_addr = addr + cpu.Y;
    uint16_t sum = cpu.A + static_cast<uint8_t>(~ram[res_addr]) + cpu.flag_C;

    cpu.flag_C = sum > 0xFF;
    cpu.A = sum & 0xFF;
    cpu.setZN(cpu.A);
}

void SMBEnv::SBC_PRGTable(const uint8_t* table, uint8_t regAddr) noexcept {
    uint8_t val = table[regAddr];
    uint16_t sum = cpu.A + static_cast<uint8_t>(~val) + cpu.flag_C;

    cpu.flag_C = sum > 0xFF;
    cpu.A = sum & 0xFF;
    cpu.setZN(cpu.A);
}

void SMBEnv::SBC_ind_x(uint8_t addr) noexcept {
    uint8_t ptr = (addr + cpu.X) & 0xFF;
    uint16_t res_addr = ram[ptr] | (ram[(ptr + 1) & 0xFF] << 8);
    uint16_t sum = cpu.A + static_cast<uint8_t>(~ram[res_addr]) + cpu.flag_C;

    cpu.flag_C = sum > 0xFF;
    cpu.A = sum & 0xFF;
    cpu.setZN(cpu.A);
}

void SMBEnv::SBC_ind_y(uint8_t addr) noexcept {
    uint16_t base = ram[addr] | (ram[(addr + 1) & 0xFF] << 8);
    uint16_t res_addr = base + cpu.Y;
    uint16_t sum = cpu.A + static_cast<uint8_t>(~ram[res_addr]) + cpu.flag_C;

    cpu.flag_C = sum > 0xFF;
    cpu.A = sum & 0xFF;
    cpu.setZN(cpu.A);
}