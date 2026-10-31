#include <smb/core/environment.hpp>
#include <smb/data/constants.hpp>
#include <smb/data/game_data.hpp>
#include <cstddef>
#include <cstring>

// utils
#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b) CONCAT_IMPL(a, b)

// generate a return point label automatically for a JSR call
#define CALL(target) \
    cpu.SP -= 2; \
    returnStack[retID++] = &&CONCAT(ret_label_, __LINE__); \
    goto target; \
    CONCAT(ret_label_, __LINE__):

// goto is used when the CALL is inside a switch-case
// the SP would otherwise mismatch the real SP and PHA and PLA would read wrong addresses

// return to code after a JSR call
#define RET() \
    do { \
        cpu.SP += 2; \
        goto *returnStack[--retID]; \
    } while (0)

// Credits to the authors of the disassembly: https://gist.github.com/1wErt3r/4048722

// reset env (aka "Start")
// this is only called once in ResetPresets() init
void SMBEnv::FirstReset() noexcept {
    void* returnStack[2]; // for JSR calls
    size_t retID = 0;

    LDQ_imm(cpu.A, 0b00010000);
    ppu.WritePPU_Ctrl(cpu.A); // activates BG at $1000
    LDQ_imm(cpu.X, 0xff);
    cpu.SP = 0xff; // reset stack pointer

    // VBlanks are omitted
    // ppu.status |= 0x80; // set vblank flag forcefully (unused)

    LDQ_imm(cpu.Y, ColdBootOffset);
    LDQ_imm(cpu.X, 0x05);

WBootCheck:
    LDQ_abs_xy(cpu.A, TopScoreDisplay, cpu.X);
    CMPQ_imm(cpu.A, 0x0a);

    if (BCS()) goto ColdBoot;

    DEX();
    if (BPL()) goto WBootCheck;

    LDQ_abs(cpu.A, WarmBootValidation);
    CMPQ_imm(cpu.A, 0xa5);
    if (BNE()) goto ColdBoot; // always true if RAM is zeroed

    LDQ_imm(cpu.Y, WarmBootOffset); // just in case

ColdBoot:
    CALL(InitializeMemory);

    STQ_abs(cpu.A, OperMode);
    LDQ_imm(cpu.A, 0xa5);
    STQ_abs(cpu.A, WarmBootValidation);
    STQ_abs(cpu.A, PseudoRandomBitReg);
    LDQ_imm(cpu.A, 0b00001111);

    LDQ_imm(cpu.A, 0b00000110);
    ppu.WritePPU_Mask(cpu.A); // activates BG Mask and Sprite Mask

    CALL(MoveAllSpritesOffscreen);
    CALL(InitializeNameTables);

    INC_abs(DisableScreenFlag);
    LDQ_abs(cpu.A, Mirror_PPU_CTRL_REG1);
    ORA_imm(0b10000000);

    CALL(WritePPUReg1);

    return; // reset ends here!

InitializeMemory:
    LDQ_imm(cpu.X, 0x07);
    LDQ_imm(cpu.A, 0x00);
    STQ_zp(cpu.A, 0x06);

InitPageLoop:
    STQ_zp(cpu.X, 0x07);

InitByteLoop:
    CMPQ_imm(cpu.X, 0x01);
    if (BNE()) goto InitByte;

    CMPQ_imm(cpu.Y, 0x60);
    if (BCS()) goto SkipByte;

InitByte:
    STA_ind_y(0x06);

SkipByte:
    DEY();
    CMPQ_imm(cpu.Y, 0xff);
    if (BNE()) goto InitByteLoop;

    DEX();
    if (BPL()) goto InitPageLoop;

    RET();

MoveAllSpritesOffscreen:
    LDQ_imm(cpu.Y, 0x00);
    LDQ_imm(cpu.A, 0xf8);

SprInitLoop:
    STA_abs_xy(Sprite_Y_Position, cpu.Y);
    INY();
    INY();
    INY();
    INY();
    if (BNE()) goto SprInitLoop;

    RET();

InitializeNameTables:
    // ppu.ReadPPU_Status(cpu.A);
    LDQ_abs(cpu.A, Mirror_PPU_CTRL_REG1);
    ORA_imm(0b00010000);
    AND_imm(0b11110000);
    CALL(WritePPUReg1);
    // LDQ_imm(cpu.A, 0x24);

    // WriteNTAddr logic simplified:
    ppu.vram_addr = 0x2000;

    LDQ_imm(cpu.X, 0x04);
    LDQ_imm(cpu.Y, 0xc0);
    LDQ_imm(cpu.A, 0x24);

InitNTLoop:
    ppu.WritePPU_Data(cpu.A);
    DEY();
    if (BNE()) goto InitNTLoop;

    DEX();
    if (BNE()) goto InitNTLoop;

    LDQ_imm(cpu.Y, 64);
    CopyXA();
    STQ_abs(cpu.A, VRAM_Buffer1_Offset);
    STQ_abs(cpu.A, VRAM_Buffer1);

InitATLoop:
    ppu.WritePPU_Data(cpu.A);
    DEY();
    if (BNE()) goto InitATLoop;

    STQ_abs(cpu.A, HorizontalScroll);
    // STQ_abs(cpu.A, VerticalScroll); // unused so it can be omitted, but causes a RAM mismatch!
    goto InitScroll;

InitScroll:
    ppu.WritePPU_Scroll(cpu.A);
    // ppu.WritePPU_Scroll(cpu.A);
    RET();

WritePPUReg1:
    ppu.WritePPU_Ctrl(cpu.A);
    STQ_abs(cpu.A, Mirror_PPU_CTRL_REG1);
    RET();
}

// step env (aka "NonMaskableInterrupt")
void SMBEnv::Step() noexcept {
    void* returnStack[50]; // for JSR calls - TODO: reduce this
    size_t retID = 0;
    bool bitFlag = false; // used to simulate .db 0x2c skips

    cpu.SP = 0xfc; // simulate the 3 automatic pushes each NMI does

    LDQ_abs(cpu.A, Mirror_PPU_CTRL_REG1);
    AND_imm(0b01111111);
    STQ_abs(cpu.A, Mirror_PPU_CTRL_REG1);
    AND_imm(0b01111110);
    ppu.WritePPU_Ctrl(cpu.A);
    LDQ_abs(cpu.A, Mirror_PPU_CTRL_REG2);
    AND_imm(0b11100110);
    LDQ_abs(cpu.Y, DisableScreenFlag);
    if (BNE()) goto ScreenOff;
    LDQ_abs(cpu.A, Mirror_PPU_CTRL_REG2);
    ORA_imm(0b00011110);
ScreenOff:
    STQ_abs(cpu.A, Mirror_PPU_CTRL_REG2);
    AND_imm(0b11100111);
    ppu.WritePPU_Mask(cpu.A);
    // ppu.ReadPPU_Status(cpu.X);
    LDQ_imm(cpu.A, 0x00);
    CALL(InitScroll);

    ppu.oamaddr = cpu.A;

    // LDQ_imm(cpu.A, 0x02);

    // write all sprites to OAM:
    // 0x200 is the result of cpu.A << 8
    std::memcpy(ppu.oam, &ram[0x0200], 256);

    LDQ_abs(cpu.X, VRAM_Buffer_AddrCtrl);
    LDQ_PRGTable(cpu.A, VRAM_AddrTable_Low, cpu.X);
    STQ_zp(cpu.A, 0x00);
    LDQ_PRGTable(cpu.A, VRAM_AddrTable_High, cpu.X);
    STQ_zp(cpu.A, 0x01);
    CALL(UpdateScreen);
    LDQ_imm(cpu.Y, 0x00);
    LDQ_abs(cpu.X, VRAM_Buffer_AddrCtrl);
    CMPQ_imm(cpu.X, 0x06);
    if (BNE()) goto InitBuffer;
    INY();
InitBuffer:
    LDQ_PRGTable(cpu.X, VRAM_Buffer_Offset, cpu.Y);
    LDQ_imm(cpu.A, 0x00);
    STA_abs_xy(VRAM_Buffer1_Offset, cpu.X);
    STA_abs_xy(VRAM_Buffer1, cpu.X);
    STQ_abs(cpu.A, VRAM_Buffer_AddrCtrl);
    LDQ_abs(cpu.A, Mirror_PPU_CTRL_REG2);
    ppu.WritePPU_Mask(cpu.A);
    
    // CALL(SoundEngine); // sfx and music is completely skipped
    ReadJoypads(); // read input from inputs
    CALL(PauseRoutine);
    CALL(UpdateTopScore);

    LDQ_abs(cpu.A, GamePauseStatus);
    LSR_acc();
    if (BCS()) goto PauseSkip;
    LDQ_abs(cpu.A, TimerControl);
    if (BEQ()) goto DecTimers;
    DEC_abs(TimerControl);
    if (BNE()) goto NoDecTimers;

DecTimers:
    LDQ_imm(cpu.X, 0x14);
    DEC_abs(IntervalTimerControl);
    if (BPL()) goto DecTimersLoop;
    LDQ_imm(cpu.A, 0x14);
    STQ_abs(cpu.A, IntervalTimerControl);
    LDQ_imm(cpu.X, 0x23);

DecTimersLoop:
    LDQ_abs_xy(cpu.A, Timers, cpu.X);
    if (BEQ()) goto SkipExpTimer;
    DEC_abs_x(Timers);

SkipExpTimer:
    DEX();
    if (BPL()) goto DecTimersLoop;
    
NoDecTimers:
    INC_zp(FrameCounter);
    
PauseSkip:
    LDQ_imm(cpu.X, 0x00);
    LDQ_imm(cpu.Y, 0x07);
    LDQ_abs(cpu.A, PseudoRandomBitReg);
    AND_imm(0b00000010);
    STQ_zp(cpu.A, 0x00);
    LDQ_abs(cpu.A, PseudoRandomBitReg + 1);
    AND_imm(0b00000010);
    EOR_zp(0x00);
    CLC();
    if (BEQ()) goto RotPRandomBit;
    SEC();

RotPRandomBit:
    ROR_abs_x(PseudoRandomBitReg);
    INX();
    
    DEY();
    if (BNE()) goto RotPRandomBit;

    LDQ_abs(cpu.A, Sprite0HitDetectFlag);
    if (BEQ()) goto SkipSprite0;

    // clear and set sprite 0 hit is omitted almost entirely:

    // clear sprite 0 hit, not saved in A because overwritten
    ppu.sprite0Hit = false; // instead of ppu.status &= ~0x40
    // cpu.flag_Z = true; // precalc result of AND_imm(0x40) on a byte with bit 6 cleared, but overwritten

    LDQ_abs(cpu.A, GamePauseStatus);
    LSR_acc();

    if (BCC()) {
        CALL(MoveSpritesOffscreen);
        CALL(SpriteShuffler);
    }

    // set sprite 0 hit, not saved in A because overwritten
    ppu.sprite0Hit = true; // instead of ppu.status |= 0x40
    // cpu.flag_Z = false; // precalc result of AND_imm(0x40) on a byte with bit 6 set, but overwritten
    // HBlank delay loop was removed as the registers are soon overwritten (Y should be 0 here)

SkipSprite0:
    LDQ_abs(cpu.A, HorizontalScroll);
    ppu.WritePPU_Scroll(cpu.A);
    // LDQ_abs(cpu.A, VerticalScroll); // y scroll is unused
    LDQ_abs(cpu.A, Mirror_PPU_CTRL_REG1);
    PHA();
    ppu.WritePPU_Ctrl(cpu.A);
    LDQ_abs(cpu.A, GamePauseStatus);
    LSR_acc();
    if (BCS()) goto SkipMainOper;
    CALL(OperModeExecutionTree);
SkipMainOper:
    // ppu.ReadPPU_Status(cpu.A);
    PLA();
    ORA_imm(0b10000000);
    ppu.WritePPU_Ctrl(cpu.A);
    
    return; // NMI ends here!

PauseRoutine:
    LDQ_abs(cpu.A, OperMode);
    CMPQ_imm(cpu.A, VictoryModeValue);
    if (BEQ()) goto ChkPauseTimer;
    CMPQ_imm(cpu.A, GameModeValue);
    if (BNE()) goto ExitPause;
    LDQ_abs(cpu.A, OperMode_Task);
    CMPQ_imm(cpu.A, 0x03);
    if (BNE()) goto ExitPause;

ChkPauseTimer:
    LDQ_abs(cpu.A, GamePauseTimer);
    if (BEQ()) goto ChkStart;
    DEC_abs(GamePauseTimer);
    RET();
ChkStart:
    LDQ_abs(cpu.A, SavedJoypad1Bits);
    AND_imm(Start_Button);
    if (BEQ()) goto ClrPauseTimer;
    LDQ_abs(cpu.A, GamePauseStatus);
    AND_imm(0b10000000);
    if (BNE()) goto ExitPause;
    LDQ_imm(cpu.A, 0x2b);
    STQ_abs(cpu.A, GamePauseTimer);
    LDQ_abs(cpu.A, GamePauseStatus);
    CopyAY();
    INY();
    STQ_zp(cpu.Y, PauseSoundQueue);
    EOR_imm(0b00000001);
    ORA_imm(0b10000000);
    if (BNE()) goto SetPause;

ClrPauseTimer:
    LDQ_abs(cpu.A, GamePauseStatus);
    AND_imm(0b01111111);
SetPause:
    STQ_abs(cpu.A, GamePauseStatus);
ExitPause:
    RET();

SpriteShuffler:
    LDQ_abs(cpu.Y, AreaType);
    LDQ_imm(cpu.A, 0x28);
    STQ_zp(cpu.A, 0x00);
    LDQ_imm(cpu.X, 0x0e);
ShuffleLoop:
    LDQ_abs_xy(cpu.A, SprDataOffset, cpu.X);
    CMPQ_zp(cpu.A, 0x00);
    if (BCC()) goto NextSprOffset;
    LDQ_abs(cpu.Y, SprShuffleAmtOffset);
    CLC();
    ADC_abs_y(SprShuffleAmt);
    if (BCC()) goto StrSprOffset;
    CLC();
    ADC_zp(0x00);
StrSprOffset:
    STA_abs_xy(SprDataOffset, cpu.X);
NextSprOffset:
    DEX();
    if (BPL()) goto ShuffleLoop;
    LDQ_abs(cpu.X, SprShuffleAmtOffset);
    INX();
    CMPQ_imm(cpu.X, 0x03);
    if (BNE()) goto SetAmtOffset;
    LDQ_imm(cpu.X, 0x00);
SetAmtOffset:
    STQ_abs(cpu.X, SprShuffleAmtOffset);
    LDQ_imm(cpu.X, 0x08);
    LDQ_imm(cpu.Y, 0x02);
SetMiscOffset:
    LDQ_abs_xy(cpu.A, SprDataOffset + 5, cpu.Y);
    STA_abs_xy(Misc_SprDataOffset - 2, cpu.X);
    CLC();
    ADC_imm(0x08);
    STA_abs_xy(Misc_SprDataOffset - 1, cpu.X);
    CLC();
    ADC_imm(0x08);
    STA_abs_xy(Misc_SprDataOffset, cpu.X);
    DEX();
    DEX();
    DEX();
    DEY();
    if (BPL()) goto SetMiscOffset;
    RET();

OperModeExecutionTree:
    LDQ_abs(cpu.A, OperMode);
    switch (cpu.A) {
        case 0: goto TitleScreenMode;
        case 1: goto GameMode;
        case 2: goto VictoryMode;
        case 3: goto GameOverMode;
    }
    __builtin_unreachable();

MoveAllSpritesOffscreen:
    LDQ_imm(cpu.Y, 0x00);
    bitFlag = true;

MoveSpritesOffscreen:
    if (!bitFlag)
        LDQ_imm(cpu.Y, 0x04);
    else
        bitFlag = false;

    LDQ_imm(cpu.A, 0xf8);

SprInitLoop:
    STA_abs_xy(Sprite_Y_Position, cpu.Y);
    INY();
    INY();
    INY();
    INY();
    if (BNE()) goto SprInitLoop;
    RET();

TitleScreenMode:
    LDQ_abs(cpu.A, OperMode_Task);
    switch (cpu.A) {
        case 0: goto InitializeGame;
        case 1: goto ScreenRoutines;
        case 2: goto PrimaryGameSetup;
        case 3: goto GameMenuRoutine;
    }
    __builtin_unreachable();

GameMenuRoutine:
    LDQ_imm(cpu.Y, 0x00);
    LDQ_abs(cpu.A, SavedJoypad1Bits);
    ORA_abs(SavedJoypad2Bits);
    CMPQ_imm(cpu.A, Start_Button);
    if (BEQ()) goto StartGame;
    CMPQ_imm(cpu.A, A_Button+Start_Button);
    if (BNE()) goto ChkSelect;

StartGame:
    goto ChkContinue;

ChkSelect:
    CMPQ_imm(cpu.A, Select_Button);
    if (BEQ()) goto SelectBLogic;
    LDQ_abs(cpu.X, DemoTimer);
    if (BNE()) goto ChkWorldSel;
    STQ_abs(cpu.A, SelectTimer);
    CALL(DemoEngine);
    if (BCS()) goto ResetTitle;
    
    goto RunDemo;
ChkWorldSel:
    LDQ_abs(cpu.X, WorldSelectEnableFlag);
    if (BEQ()) goto NullJoypad;
    CMPQ_imm(cpu.A, B_Button);
    if (BNE()) goto NullJoypad;
    INY();
SelectBLogic:
    LDQ_abs(cpu.A, DemoTimer);
    if (BEQ()) goto ResetTitle;
    LDQ_imm(cpu.A, 0x18);
    STQ_abs(cpu.A, DemoTimer);
    LDQ_abs(cpu.A, SelectTimer);
    if (BNE()) goto NullJoypad;
    LDQ_imm(cpu.A, 0x10);
    STQ_abs(cpu.A, SelectTimer);
    CMPQ_imm(cpu.Y, 0x01);
    if (BEQ()) goto IncWorldSel;
    LDQ_abs(cpu.A, NumberOfPlayers);
    EOR_imm(0b00000001);
    STQ_abs(cpu.A, NumberOfPlayers);
    CALL(DrawMushroomIcon);
    
    goto NullJoypad;
IncWorldSel:
    LDQ_abs(cpu.X, WorldSelectNumber);
    INX();
    CopyXA();
    AND_imm(0b00000111);
    STQ_abs(cpu.A, WorldSelectNumber);
    CALL(GoContinue);
UpdateShroom:
    LDQ_PRGTable(cpu.A, WSelectBufferTemplate, cpu.X);
    STA_abs_xy(VRAM_Buffer1 - 1, cpu.X);
    INX();
    CMPQ_imm(cpu.X, 0x06);
    if (BMI()) goto UpdateShroom;
    LDQ_abs(cpu.Y, WorldNumber);
    INY();
    STQ_abs(cpu.Y, VRAM_Buffer1 + 3);
NullJoypad:
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, SavedJoypad1Bits);
RunDemo:
    CALL(GameCoreRoutine);
    LDQ_zp(cpu.A, GameEngineSubroutine);
    CMPQ_imm(cpu.A, 0x06);
    if (BNE()) goto ExitMenu;

ResetTitle:
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, OperMode);
    STQ_abs(cpu.A, OperMode_Task);
    STQ_abs(cpu.A, Sprite0HitDetectFlag);
    INC_abs(DisableScreenFlag);
    RET();
ChkContinue:
    LDQ_abs(cpu.Y, DemoTimer);
    if (BEQ()) goto ResetTitle;
    ASL_acc();
    if (BCC()) goto StartWorld1;
    LDQ_abs(cpu.A, ContinueWorld);
    CALL(GoContinue);
StartWorld1:
    CALL(LoadAreaPointer);
    INC_abs(Hidden1UpFlag);
    INC_abs(OffScr_Hidden1UpFlag);
    INC_abs(FetchNewGameTimerFlag);
    INC_abs(OperMode);
    LDQ_abs(cpu.A, WorldSelectEnableFlag);
    STQ_abs(cpu.A, PrimaryHardMode);
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, OperMode_Task);
    STQ_abs(cpu.A, DemoTimer);
    LDQ_imm(cpu.X, 0x17);
    LDQ_imm(cpu.A, 0x00);
InitScores:
    STA_abs_xy(ScoreAndCoinDisplay, cpu.X);
    DEX();
    if (BPL()) goto InitScores;

ExitMenu:
    RET();
GoContinue:
    STQ_abs(cpu.A, WorldNumber);
    STQ_abs(cpu.A, OffScr_WorldNumber);
    LDQ_imm(cpu.X, 0x00);
    STQ_abs(cpu.X, AreaNumber);
    STQ_abs(cpu.X, OffScr_AreaNumber);
    RET();

DrawMushroomIcon:
    LDQ_imm(cpu.Y, 0x07);
IconDataRead:
    LDQ_PRGTable(cpu.A, MushroomIconData, cpu.Y);
    STA_abs_xy(VRAM_Buffer1 - 1, cpu.Y);
    DEY();
    if (BPL()) goto IconDataRead;
    LDQ_abs(cpu.A, NumberOfPlayers);
    if (BEQ()) goto ExitIcon;
    LDQ_imm(cpu.A, 0x24);
    STQ_abs(cpu.A, VRAM_Buffer1 + 3);
    LDQ_imm(cpu.A, 0xce);
    STQ_abs(cpu.A, VRAM_Buffer1 + 5);
ExitIcon:
    RET();

DemoEngine:
    LDQ_abs(cpu.X, DemoAction);
    LDQ_abs(cpu.A, DemoActionTimer);
    if (BNE()) goto DoAction;
    INX();
    INC_abs(DemoAction);
    SEC();
    LDQ_PRGTable(cpu.A, DemoTimingData, cpu.X - 1);
    STQ_abs(cpu.A, DemoActionTimer);
    if (BEQ()) goto DemoOver;

DoAction:
    LDQ_PRGTable(cpu.A, DemoActionData, cpu.X - 1);
    STQ_abs(cpu.A, SavedJoypad1Bits);
    DEC_abs(DemoActionTimer);
    CLC();
DemoOver:
    RET();

VictoryMode:
    CALL(VictoryModeSubroutines);
    LDQ_abs(cpu.A, OperMode_Task);
    if (BEQ()) goto AutoPlayer;
    LDQ_imm(cpu.X, 0x00);
    STQ_zp(cpu.X, ObjectOffset);
    CALL(EnemiesAndLoopsCore);
AutoPlayer:
    CALL(RelativePlayerPosition);
    
    goto PlayerGfxHandler;

VictoryModeSubroutines:
    LDQ_abs(cpu.A, OperMode_Task);
    switch (cpu.A) {
        case 0: goto BridgeCollapse;
        case 1: goto SetupVictoryMode;
        case 2: goto PlayerVictoryWalk;
        case 3: goto PrintVictoryMessages;
        case 4: goto PlayerEndWorld;
    }
    __builtin_unreachable();

SetupVictoryMode:
    LDQ_abs(cpu.X, ScreenRight_PageLoc);
    INX();
    STQ_zp(cpu.X, DestinationPageLoc);
    LDQ_imm(cpu.A, EndOfCastleMusic);
    STQ_zp(cpu.A, EventMusicQueue);
    
    goto IncModeTask_B;

PlayerVictoryWalk:
    LDQ_imm(cpu.Y, 0x00);
    STQ_zp(cpu.Y, VictoryWalkControl);
    LDQ_zp(cpu.A, Player_PageLoc);
    CMPQ_zp(cpu.A, DestinationPageLoc);
    if (BNE()) goto PerformWalk;
    LDQ_zp(cpu.A, Player_X_Position);
    CMPQ_imm(cpu.A, 0x60);
    if (BCS()) goto DontWalk;

PerformWalk:
    INC_zp(VictoryWalkControl);
    INY();
DontWalk:
    CopyYA();
    CALL(AutoControlPlayer);
    LDQ_abs(cpu.A, ScreenLeft_PageLoc);
    CMPQ_zp(cpu.A, DestinationPageLoc);
    if (BEQ()) goto ExitVWalk;
    LDQ_abs(cpu.A, ScrollFractional);
    CLC();
    ADC_imm(0x80);
    STQ_abs(cpu.A, ScrollFractional);
    LDQ_imm(cpu.A, 0x01);
    ADC_imm(0x00);
    CopyAY();
    CALL(ScrollScreen);
    CALL(UpdScrollVar);
    INC_zp(VictoryWalkControl);
ExitVWalk:
    LDQ_zp(cpu.A, VictoryWalkControl);
    if (BEQ()) goto IncModeTask_A;
    RET();

PrintVictoryMessages:
    LDQ_abs(cpu.A, SecondaryMsgCounter);
    if (BNE()) goto IncMsgCounter;
    LDQ_abs(cpu.A, PrimaryMsgCounter);
    if (BEQ()) goto ThankPlayer;
    CMPQ_imm(cpu.A, 0x09);
    if (BCS()) goto IncMsgCounter;
    LDQ_abs(cpu.Y, WorldNumber);
    CMPQ_imm(cpu.Y, World8);
    if (BNE()) goto MRetainerMsg;
    CMPQ_imm(cpu.A, 0x03);
    if (BCC()) goto IncMsgCounter;
    SBC_imm(0x01);
    
    goto ThankPlayer;
MRetainerMsg:
    CMPQ_imm(cpu.A, 0x02);
    if (BCC()) goto IncMsgCounter;

ThankPlayer:
    CopyAY();
    if (BNE()) goto SecondPartMsg;
    LDQ_abs(cpu.A, CurrentPlayer);
    if (BEQ()) goto EvalForMusic;
    INY();
    if (BNE()) goto EvalForMusic;

SecondPartMsg:
    INY();
    LDQ_abs(cpu.A, WorldNumber);
    CMPQ_imm(cpu.A, World8);
    if (BEQ()) goto EvalForMusic;
    DEY();
    CMPQ_imm(cpu.Y, 0x04);
    if (BCS()) goto SetEndTimer;
    CMPQ_imm(cpu.Y, 0x03);
    if (BCS()) goto IncMsgCounter;

EvalForMusic:
    CMPQ_imm(cpu.Y, 0x03);
    if (BNE()) goto PrintMsg;
    LDQ_imm(cpu.A, VictoryMusic);
    STQ_zp(cpu.A, EventMusicQueue);
PrintMsg:
    CopyYA();
    CLC();
    ADC_imm(0x0c);
    STQ_abs(cpu.A, VRAM_Buffer_AddrCtrl);
IncMsgCounter:
    LDQ_abs(cpu.A, SecondaryMsgCounter);
    CLC();
    ADC_imm(0x04);
    STQ_abs(cpu.A, SecondaryMsgCounter);
    LDQ_abs(cpu.A, PrimaryMsgCounter);
    ADC_imm(0x00);
    STQ_abs(cpu.A, PrimaryMsgCounter);
    CMPQ_imm(cpu.A, 0x07);
SetEndTimer:
    if (BCC()) goto ExitMsgs;
    LDQ_imm(cpu.A, 0x06);
    STQ_abs(cpu.A, WorldEndTimer);
IncModeTask_A:
    INC_abs(OperMode_Task);
ExitMsgs:
    RET();

PlayerEndWorld:
    LDQ_abs(cpu.A, WorldEndTimer);
    if (BNE()) goto EndExitOne;
    LDQ_abs(cpu.Y, WorldNumber);
    CMPQ_imm(cpu.Y, World8);
    if (BCS()) goto EndChkBButton;
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, AreaNumber);
    STQ_abs(cpu.A, LevelNumber);
    STQ_abs(cpu.A, OperMode_Task);
    INC_abs(WorldNumber);
    CALL(LoadAreaPointer);
    INC_abs(FetchNewGameTimerFlag);
    LDQ_imm(cpu.A, GameModeValue);
    STQ_abs(cpu.A, OperMode);
EndExitOne:
    RET();
EndChkBButton:
    LDQ_abs(cpu.A, SavedJoypad1Bits);
    ORA_abs(SavedJoypad2Bits);
    AND_imm(B_Button);
    if (BEQ()) goto EndExitTwo;
    LDQ_imm(cpu.A, 0x01);
    STQ_abs(cpu.A, WorldSelectEnableFlag);
    LDQ_imm(cpu.A, 0xff);
    STQ_abs(cpu.A, NumberofLives);
    CALL(TerminateGame);
EndExitTwo:
    RET();

FloateyNumbersRoutine:
    LDQ_abs_xy(cpu.A, FloateyNum_Control, cpu.X);
    if (BEQ()) goto EndExitOne;
    CMPQ_imm(cpu.A, 0x0b);
    if (BCC()) goto ChkNumTimer;
    LDQ_imm(cpu.A, 0x0b);
    STA_abs_xy(FloateyNum_Control, cpu.X);
ChkNumTimer:
    CopyAY();
    LDQ_abs_xy(cpu.A, FloateyNum_Timer, cpu.X);
    if (BNE()) goto DecNumTimer;
    STA_abs_xy(FloateyNum_Control, cpu.X);
    RET();
DecNumTimer:
    DEC_abs_x(FloateyNum_Timer);
    CMPQ_imm(cpu.A, 0x2b);
    if (BNE()) goto ChkTallEnemy;
    CMPQ_imm(cpu.Y, 0x0b);
    if (BNE()) goto LoadNumTiles;
    INC_abs(NumberofLives);
    LDQ_imm(cpu.A, Sfx_ExtraLife);
    STQ_zp(cpu.A, Square2SoundQueue);
LoadNumTiles:
    LDQ_PRGTable(cpu.A, ScoreUpdateData, cpu.Y);
    LSR_acc();
    LSR_acc();
    LSR_acc();
    LSR_acc();
    CopyAX();
    LDQ_PRGTable(cpu.A, ScoreUpdateData, cpu.Y);
    AND_imm(0b00001111);
    STA_abs_xy(DigitModifier, cpu.X);
    CALL(AddToScore);
ChkTallEnemy:
    LDQ_abs_xy(cpu.Y, Enemy_SprDataOffset, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, Spiny);
    if (BEQ()) goto FloateyPart;
    CMPQ_imm(cpu.A, PiranhaPlant);
    if (BEQ()) goto FloateyPart;
    CMPQ_imm(cpu.A, HammerBro);
    if (BEQ()) goto GetAltOffset;
    CMPQ_imm(cpu.A, GreyCheepCheep);
    if (BEQ()) goto FloateyPart;
    CMPQ_imm(cpu.A, RedCheepCheep);
    if (BEQ()) goto FloateyPart;
    CMPQ_imm(cpu.A, TallEnemy);
    if (BCS()) goto GetAltOffset;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    CMPQ_imm(cpu.A, 0x02);
    if (BCS()) goto FloateyPart;

GetAltOffset:
    LDQ_abs(cpu.X, SprDataOffset_Ctrl);
    LDQ_abs_xy(cpu.Y, Alt_SprDataOffset, cpu.X);
    LDQ_zp(cpu.X, ObjectOffset);
FloateyPart:
    LDQ_abs_xy(cpu.A, FloateyNum_Y_Pos, cpu.X);
    CMPQ_imm(cpu.A, 0x18);
    if (BCC()) goto SetupNumSpr;
    SBC_imm(0x01);
    STA_abs_xy(FloateyNum_Y_Pos, cpu.X);
SetupNumSpr:
    LDQ_abs_xy(cpu.A, FloateyNum_Y_Pos, cpu.X);
    SBC_imm(0x08);
    CALL(DumpTwoSpr);
    LDQ_abs_xy(cpu.A, FloateyNum_X_Pos, cpu.X);
    STA_abs_xy(Sprite_X_Position, cpu.Y);
    CLC();
    ADC_imm(0x08);
    STA_abs_xy(Sprite_X_Position + 4, cpu.Y);
    LDQ_imm(cpu.A, 0x02);
    STA_abs_xy(Sprite_Attributes, cpu.Y);
    STA_abs_xy(Sprite_Attributes + 4, cpu.Y);
    LDQ_abs_xy(cpu.A, FloateyNum_Control, cpu.X);
    ASL_acc();
    CopyAX();
    LDQ_PRGTable(cpu.A, FloateyNumTileData, cpu.X);
    STA_abs_xy(Sprite_Tilenumber, cpu.Y);
    LDQ_PRGTable(cpu.A, FloateyNumTileData, cpu.X + 1);
    STA_abs_xy(Sprite_Tilenumber + 4, cpu.Y);
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

ScreenRoutines:
    LDQ_abs(cpu.A, ScreenRoutineTask);
    switch (cpu.A) {
        case 0: goto InitScreen;
        case 1: goto SetupIntermediate;
        case 2: goto WriteTopStatusLine;
        case 3: goto WriteBottomStatusLine;
        case 4: goto DisplayTimeUp;
        case 5:
        case 7: goto ResetSpritesAndScreenTimer;
        case 6: goto DisplayIntermediate;
        case 8: goto AreaParserTaskControl;
        case 9: goto GetAreaPalette;
        case 10: goto GetBackgroundColor;
        case 11: goto GetAlternatePalette1;
        case 12: goto DrawTitleScreen;
        case 13: goto ClearBuffersDrawIcon;
        case 14: goto WriteTopScore;
    }
    __builtin_unreachable();

InitScreen:
    CALL(MoveAllSpritesOffscreen);
    CALL(InitializeNameTables);
    LDQ_abs(cpu.A, OperMode);
    if (BEQ()) goto NextSubtask;
    LDQ_imm(cpu.X, 0x03);
    
    goto SetVRAMAddr_A;

SetupIntermediate:
    LDQ_abs(cpu.A, BackgroundColorCtrl);
    PHA();
    LDQ_abs(cpu.A, PlayerStatus);
    PHA();
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, PlayerStatus);
    LDQ_imm(cpu.A, 0x02);
    STQ_abs(cpu.A, BackgroundColorCtrl);
    CALL(GetPlayerColors);
    PLA();
    STQ_abs(cpu.A, PlayerStatus);
    PLA();
    STQ_abs(cpu.A, BackgroundColorCtrl);
    
    goto IncSubtask;

GetAreaPalette:
    LDQ_abs(cpu.Y, AreaType);
    LDQ_PRGTable(cpu.X, AreaPalette, cpu.Y);
SetVRAMAddr_A:
    STQ_abs(cpu.X, VRAM_Buffer_AddrCtrl);
NextSubtask:
    
    goto IncSubtask;

GetBackgroundColor:
    LDQ_abs(cpu.Y, BackgroundColorCtrl);
    if (BEQ()) goto NoBGColor;
    LDQ_PRGTable(cpu.A, BGColorCtrl_Addr, cpu.Y - 4);
    STQ_abs(cpu.A, VRAM_Buffer_AddrCtrl);
NoBGColor:
    INC_abs(ScreenRoutineTask);

GetPlayerColors:
    LDQ_abs(cpu.X, VRAM_Buffer1_Offset);
    LDQ_imm(cpu.Y, 0x00);
    LDQ_abs(cpu.A, CurrentPlayer);
    if (BEQ()) goto ChkFiery;
    LDQ_imm(cpu.Y, 0x04);
ChkFiery:
    LDQ_abs(cpu.A, PlayerStatus);
    CMPQ_imm(cpu.A, 0x02);
    if (BNE()) goto StartClrGet;
    LDQ_imm(cpu.Y, 0x08);
StartClrGet:
    LDQ_imm(cpu.A, 0x03);
    STQ_zp(cpu.A, 0x00);
ClrGetLoop:
    LDQ_PRGTable(cpu.A, PlayerColors, cpu.Y);
    STA_abs_xy(VRAM_Buffer1 + 3, cpu.X);
    INY();
    INX();
    DEC_zp(0x00);
    if (BPL()) goto ClrGetLoop;
    LDQ_abs(cpu.X, VRAM_Buffer1_Offset);
    LDQ_abs(cpu.Y, BackgroundColorCtrl);
    if (BNE()) goto SetBGColor;
    LDQ_abs(cpu.Y, AreaType);
SetBGColor:
    LDQ_PRGTable(cpu.A, BackgroundColors, cpu.Y);
    STA_abs_xy(VRAM_Buffer1 + 3, cpu.X);
    LDQ_imm(cpu.A, 0x3f);
    STA_abs_xy(VRAM_Buffer1, cpu.X);
    LDQ_imm(cpu.A, 0x10);
    STA_abs_xy(VRAM_Buffer1 + 1, cpu.X);
    LDQ_imm(cpu.A, 0x04);
    STA_abs_xy(VRAM_Buffer1 + 2, cpu.X);
    LDQ_imm(cpu.A, 0x00);
    STA_abs_xy(VRAM_Buffer1 + 7, cpu.X);
    CopyXA();
    CLC();
    ADC_imm(0x07);
SetVRAMOffset:
    STQ_abs(cpu.A, VRAM_Buffer1_Offset);
    RET();

GetAlternatePalette1:
    LDQ_abs(cpu.A, AreaStyle);
    CMPQ_imm(cpu.A, 0x01);
    if (BNE()) goto NoAltPal;
    LDQ_imm(cpu.A, 0x0b);
SetVRAMAddr_B:
    STQ_abs(cpu.A, VRAM_Buffer_AddrCtrl);
NoAltPal:
    
    goto IncSubtask;

WriteTopStatusLine:
    LDQ_imm(cpu.A, 0x00);
    CALL(WriteGameText);
    
    goto IncSubtask;

WriteBottomStatusLine:
    CALL(GetSBNybbles);
    LDQ_abs(cpu.X, VRAM_Buffer1_Offset);
    LDQ_imm(cpu.A, 0x20);
    STA_abs_xy(VRAM_Buffer1, cpu.X);
    LDQ_imm(cpu.A, 0x73);
    STA_abs_xy(VRAM_Buffer1 + 1, cpu.X);
    LDQ_imm(cpu.A, 0x03);
    STA_abs_xy(VRAM_Buffer1 + 2, cpu.X);
    LDQ_abs(cpu.Y, WorldNumber);
    INY();
    CopyYA();
    STA_abs_xy(VRAM_Buffer1 + 3, cpu.X);
    LDQ_imm(cpu.A, 0x28);
    STA_abs_xy(VRAM_Buffer1 + 4, cpu.X);
    LDQ_abs(cpu.Y, LevelNumber);
    INY();
    CopyYA();
    STA_abs_xy(VRAM_Buffer1 + 5, cpu.X);
    LDQ_imm(cpu.A, 0x00);
    STA_abs_xy(VRAM_Buffer1 + 6, cpu.X);
    CopyXA();
    CLC();
    ADC_imm(0x06);
    STQ_abs(cpu.A, VRAM_Buffer1_Offset);
    
    goto IncSubtask;

DisplayTimeUp:
    LDQ_abs(cpu.A, GameTimerExpiredFlag);
    if (BEQ()) goto NoTimeUp;
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, GameTimerExpiredFlag);
    LDQ_imm(cpu.A, 0x02);
    
    goto OutputInter;
NoTimeUp:
    INC_abs(ScreenRoutineTask);
    
    goto IncSubtask;

DisplayIntermediate:
    LDQ_abs(cpu.A, OperMode);
    if (BEQ()) goto NoInter;
    CMPQ_imm(cpu.A, GameOverModeValue);
    if (BEQ()) goto GameOverInter;
    LDQ_abs(cpu.A, AltEntranceControl);
    if (BNE()) goto NoInter;
    LDQ_abs(cpu.Y, AreaType);
    CMPQ_imm(cpu.Y, 0x03);
    if (BEQ()) goto PlayerInter;
    LDQ_abs(cpu.A, DisableIntermediate);
    if (BNE()) goto NoInter;

PlayerInter:
    CALL(DrawPlayer_Intermediate);
    LDQ_imm(cpu.A, 0x01);
OutputInter:
    CALL(WriteGameText);
    CALL(ResetScreenTimer);
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, DisableScreenFlag);
    RET();
GameOverInter:
    LDQ_imm(cpu.A, 0x12);
    STQ_abs(cpu.A, ScreenTimer);
    LDQ_imm(cpu.A, 0x03);
    CALL(WriteGameText);
    
    goto IncModeTask_B;
NoInter:
    LDQ_imm(cpu.A, 0x08);
    STQ_abs(cpu.A, ScreenRoutineTask);
    RET();

AreaParserTaskControl:
    INC_abs(DisableScreenFlag);
TaskLoop:
    CALL(AreaParserTaskHandler);
    LDQ_abs(cpu.A, AreaParserTaskNum);
    if (BNE()) goto TaskLoop;
    DEC_abs(ColumnSets);
    if (BPL()) goto OutputCol;
    INC_abs(ScreenRoutineTask);
OutputCol:
    LDQ_imm(cpu.A, 0x06);
    STQ_abs(cpu.A, VRAM_Buffer_AddrCtrl);
    RET();

DrawTitleScreen:
    LDQ_abs(cpu.A, OperMode);
    if (BNE()) goto IncModeTask_B;

    ppu.vram_addr = TitleScreenDataOffset; // simplified

    LDQ_imm(cpu.A, 0x03);
    STQ_zp(cpu.A, 0x01);
    LDQ_imm(cpu.Y, 0x00);
    STQ_zp(cpu.Y, 0x00);
    ppu.ReadPPU_Data(cpu.A);
OutputTScr:
    ppu.ReadPPU_Data(cpu.A);
    STA_ind_y(0x00);
    INY();
    if (BNE()) goto ChkHiByte;
    INC_zp(0x01);
ChkHiByte:
    LDQ_zp(cpu.A, 0x01);
    CMPQ_imm(cpu.A, 0x04);
    if (BNE()) goto OutputTScr;
    CMPQ_imm(cpu.Y, 0x3a);
    if (BCC()) goto OutputTScr;
    LDQ_imm(cpu.A, 0x05);
    
    goto SetVRAMAddr_B;

ClearBuffersDrawIcon:
    LDQ_abs(cpu.A, OperMode);
    if (BNE()) goto IncModeTask_B;
    LDQ_imm(cpu.X, 0x00);
TScrClear:
    STA_abs_xy(VRAM_Buffer1 - 1, cpu.X);
    STA_abs_xy(VRAM_Buffer1 - 1 + 0x100, cpu.X);
    DEX();
    if (BNE()) goto TScrClear;
    CALL(DrawMushroomIcon);
IncSubtask:
    INC_abs(ScreenRoutineTask);
    RET();

WriteTopScore:
    LDQ_imm(cpu.A, 0xfa);
    CALL(UpdateNumber);
IncModeTask_B:
    INC_abs(OperMode_Task);
    RET();

WriteGameText:
    PHA();
    ASL_acc();
    CopyAY();
    CMPQ_imm(cpu.Y, 0x04);
    if (BCC()) goto LdGameText;
    CMPQ_imm(cpu.Y, 0x08);
    if (BCC()) goto Chk2Players;
    LDQ_imm(cpu.Y, 0x08);
Chk2Players:
    LDQ_abs(cpu.A, NumberOfPlayers);
    if (BNE()) goto LdGameText;
    INY();
LdGameText:
    LDQ_PRGTable(cpu.X, GameTextOffsets, cpu.Y);
    LDQ_imm(cpu.Y, 0x00);
GameTextLoop:
    LDQ_PRGTable(cpu.A, GameText, cpu.X);
    CMPQ_imm(cpu.A, 0xff);
    if (BEQ()) goto EndGameText;
    STA_abs_xy(VRAM_Buffer1, cpu.Y);
    INX();
    INY();
    if (BNE()) goto GameTextLoop;

EndGameText:
    LDQ_imm(cpu.A, 0x00);
    STA_abs_xy(VRAM_Buffer1, cpu.Y);
    PLA();
    CopyAX();
    CMPQ_imm(cpu.A, 0x04);
    if (BCS()) goto PrintWarpZoneNumbers;
    DEX();
    if (BNE()) goto CheckPlayerName;
    LDQ_abs(cpu.A, NumberofLives);
    CLC();
    ADC_imm(0x01);
    CMPQ_imm(cpu.A, 10);
    if (BCC()) goto PutLives;
    SBC_imm(10);
    LDQ_imm(cpu.Y, 0x9f);
    STQ_abs(cpu.Y, VRAM_Buffer1 + 7);
PutLives:
    STQ_abs(cpu.A, VRAM_Buffer1 + 8);
    LDQ_abs(cpu.Y, WorldNumber);
    INY();
    STQ_abs(cpu.Y, VRAM_Buffer1 + 19);
    LDQ_abs(cpu.Y, LevelNumber);
    INY();
    STQ_abs(cpu.Y, VRAM_Buffer1 + 21);
    RET();

CheckPlayerName:
    LDQ_abs(cpu.A, NumberOfPlayers);
    if (BEQ()) goto ExitChkName;
    LDQ_abs(cpu.A, CurrentPlayer);
    DEX();
    if (BNE()) goto ChkLuigi;
    LDQ_abs(cpu.Y, OperMode);
    CMPQ_imm(cpu.Y, GameOverModeValue);
    if (BEQ()) goto ChkLuigi;
    EOR_imm(0b00000001);
ChkLuigi:
    LSR_acc();
    if (BCC()) goto ExitChkName;
    LDQ_imm(cpu.Y, 0x04);
NameLoop:
    LDQ_PRGTable(cpu.A, LuigiName, cpu.Y);
    STA_abs_xy(VRAM_Buffer1 + 3, cpu.Y);
    DEY();
    if (BPL()) goto NameLoop;

ExitChkName:
    RET();

PrintWarpZoneNumbers:
    SBC_imm(0x04);
    ASL_acc();
    ASL_acc();
    CopyAX();
    LDQ_imm(cpu.Y, 0x00);
WarpNumLoop:
    LDQ_PRGTable(cpu.A, WarpZoneNumbers, cpu.X);
    STA_abs_xy(VRAM_Buffer1 + 27, cpu.Y);
    INX();
    INY();
    INY();
    INY();
    INY();
    CMPQ_imm(cpu.Y, 0x0c);
    if (BCC()) goto WarpNumLoop;
    LDQ_imm(cpu.A, 0x2c);
    
    goto SetVRAMOffset;

ResetSpritesAndScreenTimer:
    LDQ_abs(cpu.A, ScreenTimer);
    if (BNE()) goto NoReset;
    CALL(MoveAllSpritesOffscreen);

ResetScreenTimer:
    LDQ_imm(cpu.A, 0x07);
    STQ_abs(cpu.A, ScreenTimer);
    INC_abs(ScreenRoutineTask);
NoReset:
    RET();

RenderAreaGraphics:
    LDQ_abs(cpu.A, CurrentColumnPos);
    AND_imm(0x01);
    STQ_zp(cpu.A, 0x05);
    LDQ_abs(cpu.Y, VRAM_Buffer2_Offset);
    STQ_zp(cpu.Y, 0x00);
    LDQ_abs(cpu.A, CurrentNTAddr_Low);
    STA_abs_xy(VRAM_Buffer2 + 1, cpu.Y);
    LDQ_abs(cpu.A, CurrentNTAddr_High);
    STA_abs_xy(VRAM_Buffer2, cpu.Y);
    LDQ_imm(cpu.A, 0x9a);
    STA_abs_xy(VRAM_Buffer2 + 2, cpu.Y);
    LDQ_imm(cpu.A, 0x00);
    STQ_zp(cpu.A, 0x04);
    CopyAX();

DrawMTLoop:
    STQ_zp(cpu.X, 0x01);
    LDQ_abs_xy(cpu.A, MetatileBuffer, cpu.X);
    AND_imm(0b11000000);
    STQ_zp(cpu.A, 0x03);
    ASL_acc();
    ROL_acc();
    ROL_acc();
    CopyAY();
    LDQ_PRGTable(cpu.A, MetatileGraphics_Low, cpu.Y);
    STQ_zp(cpu.A, 0x06);
    LDQ_PRGTable(cpu.A, MetatileGraphics_High, cpu.Y);
    STQ_zp(cpu.A, 0x07);
    LDQ_abs_xy(cpu.A, MetatileBuffer, cpu.X);
    ASL_acc();
    ASL_acc();
    STQ_zp(cpu.A, 0x02);
    LDQ_abs(cpu.A, AreaParserTaskNum);
    AND_imm(0b00000001);
    EOR_imm(0b00000001);
    ASL_acc();
    ADC_zp(0x02);
    CopyAY();
    LDQ_zp(cpu.X, 0x00);
    LDA_Dyn_ind_y(0x06);
    STA_abs_xy(VRAM_Buffer2 + 3, cpu.X);
    INY();
    LDA_Dyn_ind_y(0x06);
    STA_abs_xy(VRAM_Buffer2 + 4, cpu.X);
    LDQ_zp(cpu.Y, 0x04);
    LDQ_zp(cpu.A, 0x05);
    if (BNE()) goto RightCheck;
    LDQ_zp(cpu.A, 0x01);
    LSR_acc();
    if (BCS()) goto LLeft;
    ROL_zp(0x03);
    ROL_zp(0x03);
    ROL_zp(0x03);
    
    goto SetAttrib;
RightCheck:
    LDQ_zp(cpu.A, 0x01);
    LSR_acc();
    if (BCS()) goto NextMTRow;
    LSR_zp(0x03);
    LSR_zp(0x03);
    LSR_zp(0x03);
    LSR_zp(0x03);
    
    goto SetAttrib;
LLeft:
    LSR_zp(0x03);
    LSR_zp(0x03);
NextMTRow:
    INC_zp(0x04);
SetAttrib:
    LDQ_abs_xy(cpu.A, AttributeBuffer, cpu.Y);
    ORA_zp(0x03);
    STA_abs_xy(AttributeBuffer, cpu.Y);
    INC_zp(0x00);
    INC_zp(0x00);
    LDQ_zp(cpu.X, 0x01);
    INX();
    CMPQ_imm(cpu.X, 0x0d);
    if (BCC()) goto DrawMTLoop;
    LDQ_zp(cpu.Y, 0x00);
    INY();
    INY();
    INY();
    LDQ_imm(cpu.A, 0x00);
    STA_abs_xy(VRAM_Buffer2, cpu.Y);
    STQ_abs(cpu.Y, VRAM_Buffer2_Offset);
    INC_abs(CurrentNTAddr_Low);
    LDQ_abs(cpu.A, CurrentNTAddr_Low);
    AND_imm(0b00011111);
    if (BNE()) goto ExitDrawM;
    LDQ_imm(cpu.A, 0x80);
    STQ_abs(cpu.A, CurrentNTAddr_Low);
    LDQ_abs(cpu.A, CurrentNTAddr_High);
    EOR_imm(0b00000100);
    STQ_abs(cpu.A, CurrentNTAddr_High);
ExitDrawM:
    
    goto SetVRAMCtrl;

RenderAttributeTables:
    LDQ_abs(cpu.A, CurrentNTAddr_Low);
    AND_imm(0b00011111);
    SEC();
    SBC_imm(0x04);
    AND_imm(0b00011111);
    STQ_zp(cpu.A, 0x01);
    LDQ_abs(cpu.A, CurrentNTAddr_High);
    if (BCS()) goto SetATHigh;
    EOR_imm(0b00000100);
SetATHigh:
    AND_imm(0b00000100);
    ORA_imm(0x23);
    STQ_zp(cpu.A, 0x00);
    LDQ_zp(cpu.A, 0x01);
    LSR_acc();
    LSR_acc();
    ADC_imm(0xc0);
    STQ_zp(cpu.A, 0x01);
    LDQ_imm(cpu.X, 0x00);
    LDQ_abs(cpu.Y, VRAM_Buffer2_Offset);
AttribLoop:
    LDQ_zp(cpu.A, 0x00);
    STA_abs_xy(VRAM_Buffer2, cpu.Y);
    LDQ_zp(cpu.A, 0x01);
    CLC();
    ADC_imm(0x08);
    STA_abs_xy(VRAM_Buffer2 + 1, cpu.Y);
    STQ_zp(cpu.A, 0x01);
    LDQ_abs_xy(cpu.A, AttributeBuffer, cpu.X);
    STA_abs_xy(VRAM_Buffer2 + 3, cpu.Y);
    LDQ_imm(cpu.A, 0x01);
    STA_abs_xy(VRAM_Buffer2 + 2, cpu.Y);
    LSR_acc();
    STA_abs_xy(AttributeBuffer, cpu.X);
    INY();
    INY();
    INY();
    INY();
    INX();
    CMPQ_imm(cpu.X, 0x07);
    if (BCC()) goto AttribLoop;
    STA_abs_xy(VRAM_Buffer2, cpu.Y);
    STQ_abs(cpu.Y, VRAM_Buffer2_Offset);
SetVRAMCtrl:
    LDQ_imm(cpu.A, 0x06);
    STQ_abs(cpu.A, VRAM_Buffer_AddrCtrl);
    RET();

ColorRotation:
    LDQ_zp(cpu.A, FrameCounter);
    AND_imm(0x07);
    if (BNE()) goto ExitColorRot;
    LDQ_abs(cpu.X, VRAM_Buffer1_Offset);
    CMPQ_imm(cpu.X, 0x31);
    if (BCS()) goto ExitColorRot;
    CopyAY();
GetBlankPal:
    LDQ_PRGTable(cpu.A, BlankPalette, cpu.Y);
    STA_abs_xy(VRAM_Buffer1, cpu.X);
    INX();
    INY();
    CMPQ_imm(cpu.Y, 0x08);
    if (BCC()) goto GetBlankPal;
    LDQ_abs(cpu.X, VRAM_Buffer1_Offset);
    LDQ_imm(cpu.A, 0x03);
    STQ_zp(cpu.A, 0x00);
    LDQ_abs(cpu.A, AreaType);
    ASL_acc();
    ASL_acc();
    CopyAY();
GetAreaPal:
    LDQ_PRGTable(cpu.A, Palette3Data, cpu.Y);
    STA_abs_xy(VRAM_Buffer1 + 3, cpu.X);
    INY();
    INX();
    DEC_zp(0x00);
    if (BPL()) goto GetAreaPal;
    LDQ_abs(cpu.X, VRAM_Buffer1_Offset);
    LDQ_abs(cpu.Y, ColorRotateOffset);
    LDQ_PRGTable(cpu.A, ColorRotatePalette, cpu.Y);
    STA_abs_xy(VRAM_Buffer1 + 4, cpu.X);
    LDQ_abs(cpu.A, VRAM_Buffer1_Offset);
    CLC();
    ADC_imm(0x07);
    STQ_abs(cpu.A, VRAM_Buffer1_Offset);
    INC_abs(ColorRotateOffset);
    LDQ_abs(cpu.A, ColorRotateOffset);
    CMPQ_imm(cpu.A, 0x06);
    if (BCC()) goto ExitColorRot;
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, ColorRotateOffset);
ExitColorRot:
    RET();

RemoveCoin_Axe:
    LDQ_imm(cpu.Y, 0x41);
    LDQ_imm(cpu.A, 0x03);
    LDQ_abs(cpu.X, AreaType);
    if (BNE()) goto WriteBlankMT;
    LDQ_imm(cpu.A, 0x04);
WriteBlankMT:
    CALL(PutBlockMetatile);
    LDQ_imm(cpu.A, 0x06);
    STQ_abs(cpu.A, VRAM_Buffer_AddrCtrl);
    RET();

ReplaceBlockMetatile:
    CALL(WriteBlockMetatile);
    INC_abs(Block_ResidualCounter);
    DEC_abs_x(Block_RepFlag);
    RET();

DestroyBlockMetatile:
    LDQ_imm(cpu.A, 0x00);

WriteBlockMetatile:
    LDQ_imm(cpu.Y, 0x03);
    CMPQ_imm(cpu.A, 0x00);
    if (BEQ()) goto UseBOffset;
    LDQ_imm(cpu.Y, 0x00);
    CMPQ_imm(cpu.A, 0x58);
    if (BEQ()) goto UseBOffset;
    CMPQ_imm(cpu.A, 0x51);
    if (BEQ()) goto UseBOffset;
    INY();
    CMPQ_imm(cpu.A, 0x5d);
    if (BEQ()) goto UseBOffset;
    CMPQ_imm(cpu.A, 0x52);
    if (BEQ()) goto UseBOffset;
    INY();
UseBOffset:
    CopyYA();
    LDQ_abs(cpu.Y, VRAM_Buffer1_Offset);
    INY();
    CALL(PutBlockMetatile);
MoveVOffset:
    DEY();
    CopyYA();
    CLC();
    ADC_imm(10);
    
    goto SetVRAMOffset;

PutBlockMetatile:
    STQ_zp(cpu.X, 0x00);
    STQ_zp(cpu.Y, 0x01);
    ASL_acc();
    ASL_acc();
    CopyAX();
    LDQ_imm(cpu.Y, 0x20);
    LDQ_zp(cpu.A, 0x06);
    CMPQ_imm(cpu.A, 0xd0);
    if (BCC()) goto SaveHAdder;
    LDQ_imm(cpu.Y, 0x24);
SaveHAdder:
    STQ_zp(cpu.Y, 0x03);
    AND_imm(0x0f);
    ASL_acc();
    STQ_zp(cpu.A, 0x04);
    LDQ_imm(cpu.A, 0x00);
    STQ_zp(cpu.A, 0x05);
    LDQ_zp(cpu.A, 0x02);
    CLC();
    ADC_imm(0x20);
    ASL_acc();
    ROL_zp(0x05);
    ASL_acc();
    ROL_zp(0x05);
    ADC_zp(0x04);
    STQ_zp(cpu.A, 0x04);
    LDQ_zp(cpu.A, 0x05);
    ADC_imm(0x00);
    CLC();
    ADC_zp(0x03);
    STQ_zp(cpu.A, 0x05);
    LDQ_zp(cpu.Y, 0x01);
RemBridge:
    LDQ_PRGTable(cpu.A, BlockGfxData, cpu.X);
    STA_abs_xy(VRAM_Buffer1 + 2, cpu.Y);
    LDQ_PRGTable(cpu.A, BlockGfxData, cpu.X + 1);
    STA_abs_xy(VRAM_Buffer1 + 3, cpu.Y);
    LDQ_PRGTable(cpu.A, BlockGfxData, cpu.X + 2);
    STA_abs_xy(VRAM_Buffer1 + 7, cpu.Y);
    LDQ_PRGTable(cpu.A, BlockGfxData, cpu.X + 3);
    STA_abs_xy(VRAM_Buffer1 + 8, cpu.Y);
    LDQ_zp(cpu.A, 0x04);
    STA_abs_xy(VRAM_Buffer1, cpu.Y);
    CLC();
    ADC_imm(0x20);
    STA_abs_xy(VRAM_Buffer1 + 5, cpu.Y);
    LDQ_zp(cpu.A, 0x05);
    STA_abs_xy(VRAM_Buffer1 - 1, cpu.Y);
    STA_abs_xy(VRAM_Buffer1 + 4, cpu.Y);
    LDQ_imm(cpu.A, 0x02);
    STA_abs_xy(VRAM_Buffer1 + 1, cpu.Y);
    STA_abs_xy(VRAM_Buffer1 + 6, cpu.Y);
    LDQ_imm(cpu.A, 0x00);
    STA_abs_xy(VRAM_Buffer1 + 9, cpu.Y);
    LDQ_zp(cpu.X, 0x00);
    RET();

InitializeNameTables:
    // ppu.ReadPPU_Status(cpu.A);
    LDQ_abs(cpu.A, Mirror_PPU_CTRL_REG1);
    ORA_imm(0b00010000);
    AND_imm(0b11110000);
    CALL(WritePPUReg1);
    // LDQ_imm(cpu.A, 0x24);

    // WriteNTAddr logic simplified:
    ppu.vram_addr = 0x2000;

    LDQ_imm(cpu.X, 0x04);
    LDQ_imm(cpu.Y, 0xc0);
    LDQ_imm(cpu.A, 0x24);

InitNTLoop:
    ppu.WritePPU_Data(cpu.A);
    DEY();
    if (BNE()) goto InitNTLoop;
    DEX();
    if (BNE()) goto InitNTLoop;
    LDQ_imm(cpu.Y, 64);
    CopyXA();
    STQ_abs(cpu.A, VRAM_Buffer1_Offset);
    STQ_abs(cpu.A, VRAM_Buffer1);

InitATLoop:
    ppu.WritePPU_Data(cpu.A);
    DEY();
    if (BNE()) goto InitATLoop;
    STQ_abs(cpu.A, HorizontalScroll);
    // STQ_abs(cpu.A, VerticalScroll); // unused so it can be omitted, but causes a RAM mismatch!
    
    goto InitScroll;

WriteBufferToScreen:
    ppu.vram_addr = (cpu.A << 8); // set high byte
    
    INY();
    LDA_Dyn_ind_y(0x00);
    ppu.vram_addr |= cpu.A; // set low byte

    INY();
    LDA_Dyn_ind_y(0x00);
    ASL_acc();
    PHA();
    LDQ_abs(cpu.A, Mirror_PPU_CTRL_REG1);
    ORA_imm(0b00000100);
    if (BCS()) goto SetupWrites;
    AND_imm(0b11111011);
SetupWrites:
    CALL(WritePPUReg1);
    PLA();
    ASL_acc();
    if (BCC()) goto GetLength;
    ORA_imm(0b00000010);
    INY();
GetLength:
    LSR_acc();
    LSR_acc();
    CopyAX();
OutputToVRAM:
    if (BCS()) goto RepeatByte;
    INY();
RepeatByte:
    LDA_Dyn_ind_y(0x00);
    ppu.WritePPU_Data(cpu.A);
    DEX();
    if (BNE()) goto OutputToVRAM;
    SEC();
    CopyYA();
    ADC_zp(0x00);
    STQ_zp(cpu.A, 0x00);
    LDQ_imm(cpu.A, 0x00);
    ADC_zp(0x01);
    STQ_zp(cpu.A, 0x01);
    LDQ_imm(cpu.A, 0x3f);

    // simplified:
    ppu.vram_addr = 0x3F00;

    /* old
    ppu.WritePPU_Addr(cpu.A);
    LDQ_imm(cpu.A, 0x00);
    ppu.WritePPU_Addr(cpu.A);
    ppu.WritePPU_Addr(cpu.A);
    ppu.WritePPU_Addr(cpu.A);
    */

UpdateScreen:
    // ppu.ReadPPU_Status(cpu.X);
    LDQ_imm(cpu.Y, 0x00);
    LDA_Dyn_ind_y(0x00);
    if (BNE()) goto WriteBufferToScreen;

InitScroll:
    ppu.WritePPU_Scroll(cpu.A);
    // ppu.WritePPU_Scroll(cpu.A);
    RET();

WritePPUReg1:
    ppu.WritePPU_Ctrl(cpu.A);
    STQ_abs(cpu.A, Mirror_PPU_CTRL_REG1);
    RET();

PrintStatusBarNumbers:
    STQ_zp(cpu.A, 0x00);
    CALL(OutputNumbers);
    LDQ_zp(cpu.A, 0x00);
    LSR_acc();
    LSR_acc();
    LSR_acc();
    LSR_acc();

OutputNumbers:
    CLC();
    ADC_imm(0x01);
    AND_imm(0b00001111);
    CMPQ_imm(cpu.A, 0x06);
    if (BCS()) goto ExitOutputN;
    PHA();
    ASL_acc();
    CopyAY();
    LDQ_abs(cpu.X, VRAM_Buffer1_Offset);
    LDQ_imm(cpu.A, 0x20);
    CMPQ_imm(cpu.Y, 0x00);
    if (BNE()) goto SetupNums;
    LDQ_imm(cpu.A, 0x22);
SetupNums:
    STA_abs_xy(VRAM_Buffer1, cpu.X);
    LDQ_PRGTable(cpu.A, StatusBarData, cpu.Y);
    STA_abs_xy(VRAM_Buffer1 + 1, cpu.X);
    LDQ_PRGTable(cpu.A, StatusBarData, cpu.Y + 1);
    STA_abs_xy(VRAM_Buffer1 + 2, cpu.X);
    STQ_zp(cpu.A, 0x03);
    STQ_zp(cpu.X, 0x02);
    PLA();
    CopyAX();
    LDQ_PRGTable(cpu.A, StatusBarOffset, cpu.X);
    SEC();
    SBC_PRGTable(StatusBarData, cpu.Y + 1);
    CopyAY();
    LDQ_zp(cpu.X, 0x02);
DigitPLoop:
    LDQ_abs_xy(cpu.A, DisplayDigits, cpu.Y);
    STA_abs_xy(VRAM_Buffer1 + 3, cpu.X);
    INX();
    INY();
    DEC_zp(0x03);
    if (BNE()) goto DigitPLoop;
    LDQ_imm(cpu.A, 0x00);
    STA_abs_xy(VRAM_Buffer1 + 3, cpu.X);
    INX();
    INX();
    INX();
    STQ_abs(cpu.X, VRAM_Buffer1_Offset);
ExitOutputN:
    RET();

DigitsMathRoutine:
    LDQ_abs(cpu.A, OperMode);
    CMPQ_imm(cpu.A, TitleScreenModeValue);
    if (BEQ()) goto EraseDMods;
    LDQ_imm(cpu.X, 0x05);
AddModLoop:
    LDQ_abs_xy(cpu.A, DigitModifier, cpu.X);
    CLC();
    ADC_abs_y(DisplayDigits);
    if (BMI()) goto BorrowOne;
    CMPQ_imm(cpu.A, 10);
    if (BCS()) goto CarryOne;

StoreNewD:
    STA_abs_xy(DisplayDigits, cpu.Y);
    DEY();
    DEX();
    if (BPL()) goto AddModLoop;

EraseDMods:
    LDQ_imm(cpu.A, 0x00);
    LDQ_imm(cpu.X, 0x06);
EraseMLoop:
    STA_abs_xy(DigitModifier - 1, cpu.X);
    DEX();
    if (BPL()) goto EraseMLoop;
    RET();
BorrowOne:
    DEC_abs_x(DigitModifier - 1);
    LDQ_imm(cpu.A, 0x09);
    if (BNE()) goto StoreNewD;

CarryOne:
    SEC();
    SBC_imm(10);
    INC_abs_x(DigitModifier - 1);
    
    goto StoreNewD;

UpdateTopScore:
    LDQ_imm(cpu.X, 0x05);
    CALL(TopScoreCheck);
    LDQ_imm(cpu.X, 0x0b);

TopScoreCheck:
    LDQ_imm(cpu.Y, 0x05);
    SEC();
GetScoreDiff:
    LDQ_abs_xy(cpu.A, PlayerScoreDisplay, cpu.X);
    SBC_abs_y(TopScoreDisplay);
    DEX();
    DEY();
    if (BPL()) goto GetScoreDiff;
    if (BCC()) goto NoTopSc;
    INX();
    INY();
CopyScore:
    LDQ_abs_xy(cpu.A, PlayerScoreDisplay, cpu.X);
    STA_abs_xy(TopScoreDisplay, cpu.Y);
    INX();
    INY();
    CMPQ_imm(cpu.Y, 0x06);
    if (BCC()) goto CopyScore;

NoTopSc:
    RET();

InitializeGame:
    LDQ_imm(cpu.Y, 0x6f);
    CALL(InitializeMemory);
    LDQ_imm(cpu.Y, 0x1f);
ClrSndLoop:
    STA_abs_xy(SoundMemory, cpu.Y);
    DEY();
    if (BPL()) goto ClrSndLoop;
    LDQ_imm(cpu.A, 0x18);
    STQ_abs(cpu.A, DemoTimer);
    CALL(LoadAreaPointer);

InitializeArea:
    LDQ_imm(cpu.Y, 0x4b);
    CALL(InitializeMemory);
    LDQ_imm(cpu.X, 0x21);
    LDQ_imm(cpu.A, 0x00);
ClrTimersLoop:
    STA_abs_xy(Timers, cpu.X);
    DEX();
    if (BPL()) goto ClrTimersLoop;
    LDQ_abs(cpu.A, HalfwayPage);
    LDQ_abs(cpu.Y, AltEntranceControl);
    if (BEQ()) goto StartPage;
    LDQ_abs(cpu.A, EntrancePage);
StartPage:
    STQ_abs(cpu.A, ScreenLeft_PageLoc);
    STQ_abs(cpu.A, CurrentPageLoc);
    STQ_abs(cpu.A, BackloadingFlag);
    CALL(GetScreenPosition);
    LDQ_imm(cpu.Y, 0x20);
    AND_imm(0b00000001);
    if (BEQ()) goto SetInitNTHigh;
    LDQ_imm(cpu.Y, 0x24);
SetInitNTHigh:
    STQ_abs(cpu.Y, CurrentNTAddr_High);
    LDQ_imm(cpu.Y, 0x80);
    STQ_abs(cpu.Y, CurrentNTAddr_Low);
    ASL_acc();
    ASL_acc();
    ASL_acc();
    ASL_acc();
    STQ_abs(cpu.A, BlockBufferColumnPos);
    DEC_abs(AreaObjectLength);
    DEC_abs(AreaObjectLength + 1);
    DEC_abs(AreaObjectLength + 2);
    LDQ_imm(cpu.A, 0x0b);
    STQ_abs(cpu.A, ColumnSets);
    CALL(GetAreaDataAddrs);
    LDQ_abs(cpu.A, PrimaryHardMode);
    if (BNE()) goto SetSecHard;
    LDQ_abs(cpu.A, WorldNumber);
    CMPQ_imm(cpu.A, World5);
    if (BCC()) goto CheckHalfway;
    if (BNE()) goto SetSecHard;
    LDQ_abs(cpu.A, LevelNumber);
    CMPQ_imm(cpu.A, Level3);
    if (BCC()) goto CheckHalfway;

SetSecHard:
    INC_abs(SecondaryHardMode);
CheckHalfway:
    LDQ_abs(cpu.A, HalfwayPage);
    if (BEQ()) goto DoneInitArea;
    LDQ_imm(cpu.A, 0x02);
    STQ_abs(cpu.A, PlayerEntranceCtrl);
DoneInitArea:
    LDQ_imm(cpu.A, Silence);
    STQ_zp(cpu.A, AreaMusicQueue);
    LDQ_imm(cpu.A, 0x01);
    STQ_abs(cpu.A, DisableScreenFlag);
    INC_abs(OperMode_Task);
    RET();

PrimaryGameSetup:
    LDQ_imm(cpu.A, 0x01);
    STQ_abs(cpu.A, FetchNewGameTimerFlag);
    STQ_abs(cpu.A, PlayerSize);
    LDQ_imm(cpu.A, 0x02);
    STQ_abs(cpu.A, NumberofLives);
    STQ_abs(cpu.A, OffScr_NumberofLives);

SecondaryGameSetup:
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, DisableScreenFlag);
    CopyAY();
ClearVRLoop:
    STA_abs_xy(VRAM_Buffer1 - 1, cpu.Y);
    INY();
    if (BNE()) goto ClearVRLoop;
    STQ_abs(cpu.A, GameTimerExpiredFlag);
    STQ_abs(cpu.A, DisableIntermediate);
    STQ_abs(cpu.A, BackloadingFlag);
    LDQ_imm(cpu.A, 0xff);
    STQ_abs(cpu.A, BalPlatformAlignment);
    LDQ_abs(cpu.A, ScreenLeft_PageLoc);
    LSR_abs(Mirror_PPU_CTRL_REG1);
    AND_imm(0x01);
    ROR_acc();
    ROL_abs(Mirror_PPU_CTRL_REG1);
    CALL(GetAreaMusic);
    LDQ_imm(cpu.A, 0x38);
    STQ_abs(cpu.A, SprShuffleAmt + 2);
    LDQ_imm(cpu.A, 0x48);
    STQ_abs(cpu.A, SprShuffleAmt + 1);
    LDQ_imm(cpu.A, 0x58);
    STQ_abs(cpu.A, SprShuffleAmt);
    LDQ_imm(cpu.X, 0x0e);
ShufAmtLoop:
    LDQ_PRGTable(cpu.A, DefaultSprOffsets, cpu.X);
    STA_abs_xy(SprDataOffset, cpu.X);
    DEX();
    if (BPL()) goto ShufAmtLoop;
    LDQ_imm(cpu.Y, 0x03);
ISpr0Loop:
    LDQ_PRGTable(cpu.A, Sprite0Data, cpu.Y);
    STA_abs_xy(Sprite_Data, cpu.Y);
    DEY();
    if (BPL()) goto ISpr0Loop;
    CALL(DoNothing2);
    CALL(DoNothing1);
    INC_abs(Sprite0HitDetectFlag);
    INC_abs(OperMode_Task);
    RET();

InitializeMemory:
    LDQ_imm(cpu.X, 0x07);
    LDQ_imm(cpu.A, 0x00);
    STQ_zp(cpu.A, 0x06);
InitPageLoop:
    STQ_zp(cpu.X, 0x07);
InitByteLoop:
    CMPQ_imm(cpu.X, 0x01);
    if (BNE()) goto InitByte;
    CMPQ_imm(cpu.Y, 0x60);
    if (BCS()) goto SkipByte;

InitByte:
    STA_ind_y(0x06);
SkipByte:
    DEY();
    CMPQ_imm(cpu.Y, 0xff);
    if (BNE()) goto InitByteLoop;
    DEX();
    if (BPL()) goto InitPageLoop;
    RET();

GetAreaMusic:
    LDQ_abs(cpu.A, OperMode);
    if (BEQ()) goto ExitGetM;
    LDQ_abs(cpu.A, AltEntranceControl);
    CMPQ_imm(cpu.A, 0x02);
    if (BEQ()) goto ChkAreaType;
    LDQ_imm(cpu.Y, 0x05);
    LDQ_abs(cpu.A, PlayerEntranceCtrl);
    CMPQ_imm(cpu.A, 0x06);
    if (BEQ()) goto StoreMusic;
    CMPQ_imm(cpu.A, 0x07);
    if (BEQ()) goto StoreMusic;

ChkAreaType:
    LDQ_abs(cpu.Y, AreaType);
    LDQ_abs(cpu.A, CloudTypeOverride);
    if (BEQ()) goto StoreMusic;
    LDQ_imm(cpu.Y, 0x04);
StoreMusic:
    LDQ_PRGTable(cpu.A, MusicSelectData, cpu.Y);
    STQ_zp(cpu.A, AreaMusicQueue);
ExitGetM:
    RET();

Entrance_GameTimerSetup:
    LDQ_abs(cpu.A, ScreenLeft_PageLoc);
    STQ_zp(cpu.A, Player_PageLoc);
    LDQ_imm(cpu.A, 0x28);
    STQ_abs(cpu.A, VerticalForceDown);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp(cpu.A, PlayerFacingDir);
    STQ_zp(cpu.A, Player_Y_HighPos);
    LDQ_imm(cpu.A, 0x00);
    STQ_zp(cpu.A, Player_State);
    DEC_abs(Player_CollisionBits);
    LDQ_imm(cpu.Y, 0x00);
    STQ_abs(cpu.Y, HalfwayPage);
    LDQ_abs(cpu.A, AreaType);
    if (BNE()) goto ChkStPos;
    INY();
ChkStPos:
    STQ_abs(cpu.Y, SwimmingFlag);
    LDQ_abs(cpu.X, PlayerEntranceCtrl);
    LDQ_abs(cpu.Y, AltEntranceControl);
    if (BEQ()) goto SetStPos;
    CMPQ_imm(cpu.Y, 0x01);
    if (BEQ()) goto SetStPos;
    LDQ_PRGTable(cpu.X, AltYPosOffset, cpu.Y - 2);
SetStPos:
    LDQ_PRGTable(cpu.A, PlayerStarting_X_Pos, cpu.Y);
    STQ_zp(cpu.A, Player_X_Position);
    LDQ_PRGTable(cpu.A, PlayerStarting_Y_Pos, cpu.X);
    STQ_zp(cpu.A, Player_Y_Position);
    LDQ_PRGTable(cpu.A, PlayerBGPriorityData, cpu.X);
    STQ_abs(cpu.A, Player_SprAttrib);
    CALL(GetPlayerColors);
    LDQ_abs(cpu.Y, GameTimerSetting);
    if (BEQ()) goto ChkOverR;
    LDQ_abs(cpu.A, FetchNewGameTimerFlag);
    if (BEQ()) goto ChkOverR;
    LDQ_PRGTable(cpu.A, GameTimerData, cpu.Y);
    STQ_abs(cpu.A, GameTimerDisplay);
    LDQ_imm(cpu.A, 0x01);
    STQ_abs(cpu.A, GameTimerDisplay + 2);
    LSR_acc();
    STQ_abs(cpu.A, GameTimerDisplay + 1);
    STQ_abs(cpu.A, FetchNewGameTimerFlag);
    STQ_abs(cpu.A, StarInvincibleTimer);
ChkOverR:
    LDQ_abs(cpu.Y, JoypadOverride);
    if (BEQ()) goto ChkSwimE;
    LDQ_imm(cpu.A, 0x03);
    STQ_zp(cpu.A, Player_State);
    LDQ_imm(cpu.X, 0x00);
    CALL(InitBlock_XY_Pos);
    LDQ_imm(cpu.A, 0xf0);
    STQ_zp(cpu.A, Block_Y_Position);
    LDQ_imm(cpu.X, 0x05);
    LDQ_imm(cpu.Y, 0x00);
    CALL(Setup_Vine);
ChkSwimE:
    LDQ_abs(cpu.Y, AreaType);
    if (BNE()) goto SetPESub;
    CALL(SetupBubble);
SetPESub:
    LDQ_imm(cpu.A, 0x07);
    STQ_zp(cpu.A, GameEngineSubroutine);
    RET();

PlayerLoseLife:
    INC_abs(DisableScreenFlag);
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, Sprite0HitDetectFlag);
    LDQ_imm(cpu.A, Silence);
    STQ_zp(cpu.A, EventMusicQueue);
    DEC_abs(NumberofLives);
    if (BPL()) goto StillInGame;
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, OperMode_Task);
    LDQ_imm(cpu.A, GameOverModeValue);
    STQ_abs(cpu.A, OperMode);
    RET();
StillInGame:
    LDQ_abs(cpu.A, WorldNumber);
    ASL_acc();
    CopyAX();
    LDQ_abs(cpu.A, LevelNumber);
    AND_imm(0x02);
    if (BEQ()) goto GetHalfway;
    INX();
GetHalfway:
    LDQ_PRGTable(cpu.Y, HalfwayPageNybbles, cpu.X);
    LDQ_abs(cpu.A, LevelNumber);
    LSR_acc();
    CopyYA();
    if (BCS()) goto MaskHPNyb;
    LSR_acc();
    LSR_acc();
    LSR_acc();
    LSR_acc();
MaskHPNyb:
    AND_imm(0b00001111);
    CMPQ_abs(cpu.A, ScreenLeft_PageLoc);
    if (BEQ()) goto SetHalfway;
    if (BCC()) goto SetHalfway;
    LDQ_imm(cpu.A, 0x00);
SetHalfway:
    STQ_abs(cpu.A, HalfwayPage);
    CALL(TransposePlayers);
    
    goto ContinueGame;

GameOverMode:
    LDQ_abs(cpu.A, OperMode_Task);
    switch (cpu.A) {
        case 0: goto SetupGameOver;
        case 1: goto ScreenRoutines;
        case 2: goto RunGameOver;
    }
    __builtin_unreachable();

SetupGameOver:
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, ScreenRoutineTask);
    STQ_abs(cpu.A, Sprite0HitDetectFlag);
    LDQ_imm(cpu.A, GameOverMusic);
    STQ_zp(cpu.A, EventMusicQueue);
    INC_abs(DisableScreenFlag);
    INC_abs(OperMode_Task);
    RET();

RunGameOver:
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, DisableScreenFlag);
    LDQ_abs(cpu.A, SavedJoypad1Bits);
    AND_imm(Start_Button);
    if (BNE()) goto TerminateGame;
    LDQ_abs(cpu.A, ScreenTimer);
    if (BNE()) goto GameIsOn;

TerminateGame:
    LDQ_imm(cpu.A, Silence);
    STQ_zp(cpu.A, EventMusicQueue);
    CALL(TransposePlayers);
    if (BCC()) goto ContinueGame;
    LDQ_abs(cpu.A, WorldNumber);
    STQ_abs(cpu.A, ContinueWorld);
    LDQ_imm(cpu.A, 0x00);
    ASL_acc();
    STQ_abs(cpu.A, OperMode_Task);
    STQ_abs(cpu.A, ScreenTimer);
    STQ_abs(cpu.A, OperMode);
    RET();

ContinueGame:
    CALL(LoadAreaPointer);
    LDQ_imm(cpu.A, 0x01);
    STQ_abs(cpu.A, PlayerSize);
    INC_abs(FetchNewGameTimerFlag);
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, TimerControl);
    STQ_abs(cpu.A, PlayerStatus);
    STQ_zp(cpu.A, GameEngineSubroutine);
    STQ_abs(cpu.A, OperMode_Task);
    LDQ_imm(cpu.A, 0x01);
    STQ_abs(cpu.A, OperMode);
GameIsOn:
    RET();

TransposePlayers:
    SEC();
    LDQ_abs(cpu.A, NumberOfPlayers);
    if (BEQ()) goto ExTrans;
    LDQ_abs(cpu.A, OffScr_NumberofLives);
    if (BMI()) goto ExTrans;
    LDQ_abs(cpu.A, CurrentPlayer);
    EOR_imm(0b00000001);
    STQ_abs(cpu.A, CurrentPlayer);
    LDQ_imm(cpu.X, 0x06);
TransLoop:
    LDQ_abs_xy(cpu.A, OnscreenPlayerInfo, cpu.X);
    PHA();
    LDQ_abs_xy(cpu.A, OffscreenPlayerInfo, cpu.X);
    STA_abs_xy(OnscreenPlayerInfo, cpu.X);
    PLA();
    STA_abs_xy(OffscreenPlayerInfo, cpu.X);
    DEX();
    if (BPL()) goto TransLoop;
    CLC();
ExTrans:
    RET();

DoNothing1:
    LDQ_imm(cpu.A, 0xff);
    STQ_abs(cpu.A, 0x06c9);
DoNothing2:
    RET();

AreaParserTaskHandler:
    LDQ_abs(cpu.Y, AreaParserTaskNum);
    if (BNE()) goto DoAPTasks;
    LDQ_imm(cpu.Y, 0x08);
    STQ_abs(cpu.Y, AreaParserTaskNum);
DoAPTasks:
    DEY();
    CopyYA();
    CALL(AreaParserTasks);
    DEC_abs(AreaParserTaskNum);
    if (BNE()) goto SkipATRender;
    CALL(RenderAttributeTables);
SkipATRender:
    RET();

AreaParserTasks:
    switch (cpu.A) {
        case 0:
        case 4: goto IncrementColumnPos;
        case 1:
        case 2:
        case 5:
        case 6: goto RenderAreaGraphics;
        case 3:
        case 7: goto AreaParserCore;
    }
    __builtin_unreachable();

IncrementColumnPos:
    INC_abs(CurrentColumnPos);
    LDQ_abs(cpu.A, CurrentColumnPos);
    AND_imm(0b00001111);
    if (BNE()) goto NoColWrap;
    STQ_abs(cpu.A, CurrentColumnPos);
    INC_abs(CurrentPageLoc);
NoColWrap:
    INC_abs(BlockBufferColumnPos);
    LDQ_abs(cpu.A, BlockBufferColumnPos);
    AND_imm(0b00011111);
    STQ_abs(cpu.A, BlockBufferColumnPos);
    RET();

AreaParserCore:
    LDQ_abs(cpu.A, BackloadingFlag);
    if (BEQ()) goto RenderSceneryTerrain;
    CALL(ProcessAreaData);

RenderSceneryTerrain:
    LDQ_imm(cpu.X, 0x0c);
    LDQ_imm(cpu.A, 0x00);
ClrMTBuf:
    STA_abs_xy(MetatileBuffer, cpu.X);
    DEX();
    if (BPL()) goto ClrMTBuf;
    LDQ_abs(cpu.Y, BackgroundScenery);
    if (BEQ()) goto RendFore;
    LDQ_abs(cpu.A, CurrentPageLoc);
ThirdP:
    CMPQ_imm(cpu.A, 0x03);
    if (BMI()) goto RendBack;
    SEC();
    SBC_imm(0x03);
    if (BPL()) goto ThirdP;

RendBack:
    ASL_acc();
    ASL_acc();
    ASL_acc();
    ASL_acc();
    ADC_PRGTable(BSceneDataOffsets, cpu.Y - 1);
    ADC_abs(CurrentColumnPos);
    CopyAX();
    LDQ_PRGTable(cpu.A, BackSceneryData, cpu.X);
    if (BEQ()) goto RendFore;
    PHA();
    AND_imm(0x0f);
    SEC();
    SBC_imm(0x01);
    STQ_zp(cpu.A, 0x00);
    ASL_acc();
    ADC_zp(0x00);
    CopyAX();
    PLA();
    LSR_acc();
    LSR_acc();
    LSR_acc();
    LSR_acc();
    CopyAY();
    LDQ_imm(cpu.A, 0x03);
    STQ_zp(cpu.A, 0x00);
SceLoop1:
    LDQ_PRGTable(cpu.A, BackSceneryMetatiles, cpu.X);
    STA_abs_xy(MetatileBuffer, cpu.Y);
    INX();
    INY();
    CMPQ_imm(cpu.Y, 0x0b);
    if (BEQ()) goto RendFore;
    DEC_zp(0x00);
    if (BNE()) goto SceLoop1;

RendFore:
    LDQ_abs(cpu.X, ForegroundScenery);
    if (BEQ()) goto RendTerr;
    LDQ_PRGTable(cpu.Y, FSceneDataOffsets, cpu.X - 1);
    LDQ_imm(cpu.X, 0x00);
SceLoop2:
    LDQ_PRGTable(cpu.A, ForeSceneryData, cpu.Y);
    if (BEQ()) goto NoFore;
    STA_abs_xy(MetatileBuffer, cpu.X);
NoFore:
    INY();
    INX();
    CMPQ_imm(cpu.X, 0x0d);
    if (BNE()) goto SceLoop2;

RendTerr:
    LDQ_abs(cpu.Y, AreaType);
    if (BNE()) goto TerMTile;
    LDQ_abs(cpu.A, WorldNumber);
    CMPQ_imm(cpu.A, World8);
    if (BNE()) goto TerMTile;
    LDQ_imm(cpu.A, 0x62);
    
    goto StoreMT;
TerMTile:
    LDQ_PRGTable(cpu.A, TerrainMetatiles, cpu.Y);
    LDQ_abs(cpu.Y, CloudTypeOverride);
    if (BEQ()) goto StoreMT;
    LDQ_imm(cpu.A, 0x88);
StoreMT:
    STQ_zp(cpu.A, 0x07);
    LDQ_imm(cpu.X, 0x00);
    LDQ_abs(cpu.A, TerrainControl);
    ASL_acc();
    CopyAY();
TerrLoop:
    LDQ_PRGTable(cpu.A, TerrainRenderBits, cpu.Y);
    STQ_zp(cpu.A, 0x00);
    INY();
    STQ_zp(cpu.Y, 0x01);
    LDQ_abs(cpu.A, CloudTypeOverride);
    if (BEQ()) goto NoCloud2;
    CMPQ_imm(cpu.X, 0x00);
    if (BEQ()) goto NoCloud2;
    LDQ_zp(cpu.A, 0x00);
    AND_imm(0b00001000);
    STQ_zp(cpu.A, 0x00);
NoCloud2:
    LDQ_imm(cpu.Y, 0x00);
TerrBChk:
    LDQ_PRGTable(cpu.A, Bitmasks, cpu.Y);
    BIT_zp(0x00);
    if (BEQ()) goto NextTBit;
    LDQ_zp(cpu.A, 0x07);
    STA_abs_xy(MetatileBuffer, cpu.X);
NextTBit:
    INX();
    CMPQ_imm(cpu.X, 0x0d);
    if (BEQ()) goto RendBBuf;
    LDQ_abs(cpu.A, AreaType);
    CMPQ_imm(cpu.A, 0x02);
    if (BNE()) goto EndUChk;
    CMPQ_imm(cpu.X, 0x0b);
    if (BNE()) goto EndUChk;
    LDQ_imm(cpu.A, 0x54);
    STQ_zp(cpu.A, 0x07);
EndUChk:
    INY();
    CMPQ_imm(cpu.Y, 0x08);
    if (BNE()) goto TerrBChk;
    LDQ_zp(cpu.Y, 0x01);
    if (BNE()) goto TerrLoop;

RendBBuf:
    CALL(ProcessAreaData);
    LDQ_abs(cpu.A, BlockBufferColumnPos);
    CALL(GetBlockBufferAddr);
    LDQ_imm(cpu.X, 0x00);
    LDQ_imm(cpu.Y, 0x00);
ChkMTLow:
    STQ_zp(cpu.Y, 0x00);
    LDQ_abs_xy(cpu.A, MetatileBuffer, cpu.X);
    AND_imm(0b11000000);
    ASL_acc();
    ROL_acc();
    ROL_acc();
    CopyAY();
    LDQ_abs_xy(cpu.A, MetatileBuffer, cpu.X);
    CMPA_PRGTable(BlockBuffLowBounds, cpu.Y);
    if (BCS()) goto StrBlock;
    LDQ_imm(cpu.A, 0x00);
StrBlock:
    LDQ_zp(cpu.Y, 0x00);
    STA_ind_y(0x06);
    CopyYA();
    CLC();
    ADC_imm(0x10);
    CopyAY();
    INX();
    CMPQ_imm(cpu.X, 0x0d);
    if (BCC()) goto ChkMTLow;
    RET();

ProcessAreaData:
    LDQ_imm(cpu.X, 0x02);
ProcADLoop:
    STQ_zp(cpu.X, ObjectOffset);
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, BehindAreaParserFlag);
    LDQ_abs(cpu.Y, AreaDataOffset);
    LDA_Dyn_ind_y(AreaData);
    CMPQ_imm(cpu.A, 0xfd);
    if (BEQ()) goto RdyDecode;
    LDQ_abs_xy(cpu.A, AreaObjectLength, cpu.X);
    if (BPL()) goto RdyDecode;
    INY();
    LDA_Dyn_ind_y(AreaData);
    ASL_acc();
    if (BCC()) goto Chk1Row13;
    LDQ_abs(cpu.A, AreaObjectPageSel);
    if (BNE()) goto Chk1Row13;
    INC_abs(AreaObjectPageSel);
    INC_abs(AreaObjectPageLoc);
Chk1Row13:
    DEY();
    LDA_Dyn_ind_y(AreaData);
    AND_imm(0x0f);
    CMPQ_imm(cpu.A, 0x0d);
    if (BNE()) goto Chk1Row14;
    INY();
    LDA_Dyn_ind_y(AreaData);
    DEY();
    AND_imm(0b01000000);
    if (BNE()) goto CheckRear;
    LDQ_abs(cpu.A, AreaObjectPageSel);
    if (BNE()) goto CheckRear;
    INY();
    LDA_Dyn_ind_y(AreaData);
    AND_imm(0b00011111);
    STQ_abs(cpu.A, AreaObjectPageLoc);
    INC_abs(AreaObjectPageSel);
    
    goto NextAObj;
Chk1Row14:
    CMPQ_imm(cpu.A, 0x0e);
    if (BNE()) goto CheckRear;
    LDQ_abs(cpu.A, BackloadingFlag);
    if (BNE()) goto RdyDecode;

CheckRear:
    LDQ_abs(cpu.A, AreaObjectPageLoc);
    CMPQ_abs(cpu.A, CurrentPageLoc);
    if (BCC()) goto SetBehind;

RdyDecode:
    CALL(DecodeAreaData);
    
    goto ChkLength;
SetBehind:
    INC_abs(BehindAreaParserFlag);
NextAObj:
    CALL(IncAreaObjOffset);
ChkLength:
    LDQ_zp(cpu.X, ObjectOffset);
    LDQ_abs_xy(cpu.A, AreaObjectLength, cpu.X);
    if (BMI()) goto ProcLoopb;
    DEC_abs_x(AreaObjectLength);
ProcLoopb:
    DEX();
    if (BPL()) goto ProcADLoop;
    LDQ_abs(cpu.A, BehindAreaParserFlag);
    if (BNE()) goto ProcessAreaData;
    LDQ_abs(cpu.A, BackloadingFlag);
    if (BNE()) goto ProcessAreaData;

EndAParse:
    RET();

IncAreaObjOffset:
    INC_abs(AreaDataOffset);
    INC_abs(AreaDataOffset);
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, AreaObjectPageSel);
    RET();

DecodeAreaData:
    LDQ_abs_xy(cpu.A, AreaObjectLength, cpu.X);
    if (BMI()) goto Chk1stB;
    LDQ_abs_xy(cpu.Y, AreaObjOffsetBuffer, cpu.X);
Chk1stB:
    LDQ_imm(cpu.X, 0x10);
    LDA_Dyn_ind_y(AreaData);
    CMPQ_imm(cpu.A, 0xfd);
    if (BEQ()) goto EndAParse;
    AND_imm(0x0f);
    CMPQ_imm(cpu.A, 0x0f);
    if (BEQ()) goto ChkRow14;
    LDQ_imm(cpu.X, 0x08);
    CMPQ_imm(cpu.A, 0x0c);
    if (BEQ()) goto ChkRow14;
    LDQ_imm(cpu.X, 0x00);
ChkRow14:
    STQ_zp(cpu.X, 0x07);
    LDQ_zp(cpu.X, ObjectOffset);
    CMPQ_imm(cpu.A, 0x0e);
    if (BNE()) goto ChkRow13;
    LDQ_imm(cpu.A, 0x00);
    STQ_zp(cpu.A, 0x07);
    LDQ_imm(cpu.A, 0x2e);
    if (BNE()) goto NormObj;

ChkRow13:
    CMPQ_imm(cpu.A, 0x0d);
    if (BNE()) goto ChkSRows;
    LDQ_imm(cpu.A, 0x22);
    STQ_zp(cpu.A, 0x07);
    INY();
    LDA_Dyn_ind_y(AreaData);
    AND_imm(0b01000000);
    if (BEQ()) goto LeavePar;
    LDA_Dyn_ind_y(AreaData);
    AND_imm(0b01111111);
    CMPQ_imm(cpu.A, 0x4b);
    if (BNE()) goto Mask2MSB;
    INC_abs(LoopCommand);
Mask2MSB:
    AND_imm(0b00111111);
    
    goto NormObj;
ChkSRows:
    CMPQ_imm(cpu.A, 0x0c);
    if (BCS()) goto SpecObj;
    INY();
    LDA_Dyn_ind_y(AreaData);
    AND_imm(0b01110000);
    if (BNE()) goto LrgObj;
    LDQ_imm(cpu.A, 0x16);
    STQ_zp(cpu.A, 0x07);
    LDA_Dyn_ind_y(AreaData);
    AND_imm(0b00001111);
    
    goto NormObj;
LrgObj:
    STQ_zp(cpu.A, 0x00);
    CMPQ_imm(cpu.A, 0x70);
    if (BNE()) goto NotWPipe;
    LDA_Dyn_ind_y(AreaData);
    AND_imm(0b00001000);
    if (BEQ()) goto NotWPipe;
    LDQ_imm(cpu.A, 0x00);
    STQ_zp(cpu.A, 0x00);
NotWPipe:
    LDQ_zp(cpu.A, 0x00);
    
    goto MoveAOId;
SpecObj:
    INY();
    LDA_Dyn_ind_y(AreaData);
    AND_imm(0b01110000);
MoveAOId:
    LSR_acc();
    LSR_acc();
    LSR_acc();
    LSR_acc();
NormObj:
    STQ_zp(cpu.A, 0x00);
    LDQ_abs_xy(cpu.A, AreaObjectLength, cpu.X);
    if (BPL()) goto RunAObj;
    LDQ_abs(cpu.A, AreaObjectPageLoc);
    CMPQ_abs(cpu.A, CurrentPageLoc);
    if (BEQ()) goto InitRear;
    LDQ_abs(cpu.Y, AreaDataOffset);
    LDA_Dyn_ind_y(AreaData);
    AND_imm(0b00001111);
    CMPQ_imm(cpu.A, 0x0e);
    if (BNE()) goto LeavePar;
    LDQ_abs(cpu.A, BackloadingFlag);
    if (BNE()) goto StrAObj;

LeavePar:
    RET();
InitRear:
    LDQ_abs(cpu.A, BackloadingFlag);
    if (BEQ()) goto BackColC;
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, BackloadingFlag);
    STQ_abs(cpu.A, BehindAreaParserFlag);
    STQ_zp(cpu.A, ObjectOffset);
LoopCmdE:
    RET();
BackColC:
    LDQ_abs(cpu.Y, AreaDataOffset);
    LDA_Dyn_ind_y(AreaData);
    AND_imm(0b11110000);
    LSR_acc();
    LSR_acc();
    LSR_acc();
    LSR_acc();
    CMPQ_abs(cpu.A, CurrentColumnPos);
    if (BNE()) goto LeavePar;

StrAObj:
    LDQ_abs(cpu.A, AreaDataOffset);
    STA_abs_xy(AreaObjOffsetBuffer, cpu.X);
    CALL(IncAreaObjOffset);
RunAObj:
    LDQ_zp(cpu.A, 0x00);
    CLC();
    ADC_zp(0x07);
    switch (cpu.A) {
        case 0:
        case 7: goto VerticalPipe;
        case 1: goto AreaStyleObject;
        case 2: goto RowOfBricks;
        case 3: goto RowOfSolidBlocks;
        case 4: goto RowOfCoins;
        case 5: goto ColumnOfBricks;
        case 6: goto ColumnOfSolidBlocks;
        case 8: goto Hole_Empty;
        case 9: goto PulleyRopeObject;
        case 10: goto Bridge_High;
        case 11: goto Bridge_Middle;
        case 12: goto Bridge_Low;
        case 13: goto Hole_Water;
        case 14: goto QuestionBlockRow_High;
        case 15: goto QuestionBlockRow_Low;
        case 16: goto EndlessRope;
        case 17: goto BalancePlatRope;
        case 18: goto CastleObject;
        case 19: goto StaircaseObject;
        case 20: goto ExitPipe;
#ifndef REMOVE_RESIDUAL
        case 21: goto FlagBalls_Residual;
#endif
        case 22:
        case 23:
        case 24: goto QuestionBlock;
        case 25: goto Hidden1UpBlock;
        case 26:
        case 27:
        case 28:
        case 30: goto BrickWithItem;
        case 29: goto BrickWithCoins;
        case 31: goto WaterPipe;
        case 32: goto EmptyBlock;
        case 33: goto Jumpspring;
        case 34: goto IntroPipe;
        case 35: goto FlagpoleObject;
        case 36: goto AxeObj;
        case 37: goto ChainObj;
        case 38: goto CastleBridgeObj;
        case 39: goto ScrollLockObject_Warp;
#ifndef REMOVE_RESIDUAL
        case 40:
#endif
        case 41: goto ScrollLockObject;
        case 42:
        case 43:
        case 44: goto AreaFrenzy;
        case 45: goto LoopCmdE;
        case 46: goto AlterAreaAttributes;
    }
    __builtin_unreachable();

AlterAreaAttributes:
    LDQ_abs_xy(cpu.Y, AreaObjOffsetBuffer, cpu.X);
    INY();
    LDA_Dyn_ind_y(AreaData);
    PHA();
    AND_imm(0b01000000);
    if (BNE()) goto Alter2;
    PLA();
    PHA();
    AND_imm(0b00001111);
    STQ_abs(cpu.A, TerrainControl);
    PLA();
    AND_imm(0b00110000);
    LSR_acc();
    LSR_acc();
    LSR_acc();
    LSR_acc();
    STQ_abs(cpu.A, BackgroundScenery);
    RET();
Alter2:
    PLA();
    AND_imm(0b00000111);
    CMPQ_imm(cpu.A, 0x04);
    if (BCC()) goto SetFore;
    STQ_abs(cpu.A, BackgroundColorCtrl);
    LDQ_imm(cpu.A, 0x00);
SetFore:
    STQ_abs(cpu.A, ForegroundScenery);
    RET();

ScrollLockObject_Warp:
    LDQ_imm(cpu.X, 0x04);
    LDQ_abs(cpu.A, WorldNumber);
    if (BEQ()) goto WarpNum;
    INX();
    LDQ_abs(cpu.Y, AreaType);
    DEY();
    if (BNE()) goto WarpNum;
    INX();
WarpNum:
    CopyXA();
    STQ_abs(cpu.A, WarpZoneControl);
    CALL(WriteGameText);
    LDQ_imm(cpu.A, PiranhaPlant);
    CALL(KillEnemies);

ScrollLockObject:
    LDQ_abs(cpu.A, ScrollLock);
    EOR_imm(0b00000001);
    STQ_abs(cpu.A, ScrollLock);
    RET();

KillEnemies:
    STQ_zp(cpu.A, 0x00);
    LDQ_imm(cpu.A, 0x00);
    LDQ_imm(cpu.X, 0x04);
KillELoop:
    LDQ_zp_xy(cpu.Y, Enemy_ID, cpu.X);
    CMPQ_zp(cpu.Y, 0x00);
    if (BNE()) goto NoKillE;
    STQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
NoKillE:
    DEX();
    if (BPL()) goto KillELoop;
    RET();

AreaFrenzy:
    LDQ_zp(cpu.X, 0x00);
    LDQ_PRGTable(cpu.A, FrenzyIDData, cpu.X - 8);
    LDQ_imm(cpu.Y, 0x05);
FreCompLoop:
    DEY();
    if (BMI()) goto ExitAFrenzy;
    CMPA_abs_xy(Enemy_ID, cpu.Y);
    if (BNE()) goto FreCompLoop;
    LDQ_imm(cpu.A, 0x00);
ExitAFrenzy:
    STQ_abs(cpu.A, EnemyFrenzyQueue);
    RET();

AreaStyleObject:
    LDQ_abs(cpu.A, AreaStyle);
    switch (cpu.A) {
        case 0: goto TreeLedge;
        case 1: goto MushroomLedge;
        case 2: goto BulletBillCannon;
    }
    __builtin_unreachable();

TreeLedge:
    CALL(GetLrgObjAttrib);
    LDQ_abs_xy(cpu.A, AreaObjectLength, cpu.X);
    if (BEQ()) goto EndTreeL;
    if (BPL()) goto MidTreeL;
    CopyYA();
    STA_abs_xy(AreaObjectLength, cpu.X);
    LDQ_abs(cpu.A, CurrentPageLoc);
    ORA_abs(CurrentColumnPos);
    if (BEQ()) goto MidTreeL;
    LDQ_imm(cpu.A, 0x16);
    
    goto NoUnder;
MidTreeL:
    LDQ_zp(cpu.X, 0x07);
    LDQ_imm(cpu.A, 0x17);
    STA_abs_xy(MetatileBuffer, cpu.X);
    LDQ_imm(cpu.A, 0x4c);
    
    goto AllUnder;
EndTreeL:
    LDQ_imm(cpu.A, 0x18);
    
    goto NoUnder;

MushroomLedge:
    CALL(ChkLrgObjLength);
    STQ_zp(cpu.Y, 0x06);
    if (BCC()) goto EndMushL;
    LDQ_abs_xy(cpu.A, AreaObjectLength, cpu.X);
    LSR_acc();
    STA_abs_xy(MushroomLedgeHalfLen, cpu.X);
    LDQ_imm(cpu.A, 0x19);
    
    goto NoUnder;
EndMushL:
    LDQ_imm(cpu.A, 0x1b);
    LDQ_abs_xy(cpu.Y, AreaObjectLength, cpu.X);
    if (BEQ()) goto NoUnder;
    LDQ_abs_xy(cpu.A, MushroomLedgeHalfLen, cpu.X);
    STQ_zp(cpu.A, 0x06);
    LDQ_zp(cpu.X, 0x07);
    LDQ_imm(cpu.A, 0x1a);
    STA_abs_xy(MetatileBuffer, cpu.X);
    CMPQ_zp(cpu.Y, 0x06);
    if (BNE()) goto MushLExit;
    INX();
    LDQ_imm(cpu.A, 0x4f);
    STA_abs_xy(MetatileBuffer, cpu.X);
    LDQ_imm(cpu.A, 0x50);
AllUnder:
    INX();
    LDQ_imm(cpu.Y, 0x0f);
    
    goto RenderUnderPart;
NoUnder:
    LDQ_zp(cpu.X, 0x07);
    LDQ_imm(cpu.Y, 0x00);
    
    goto RenderUnderPart;

PulleyRopeObject:
    CALL(ChkLrgObjLength);
    LDQ_imm(cpu.Y, 0x00);
    if (BCS()) goto RenderPul;
    INY();
    LDQ_abs_xy(cpu.A, AreaObjectLength, cpu.X);
    if (BNE()) goto RenderPul;
    INY();
RenderPul:
    LDQ_PRGTable(cpu.A, PulleyRopeMetatiles, cpu.Y);
    STQ_abs(cpu.A, MetatileBuffer);
MushLExit:
    RET();

CastleObject:
    CALL(GetLrgObjAttrib);
    STQ_zp(cpu.Y, 0x07);
    LDQ_imm(cpu.Y, 0x04);
    CALL(ChkLrgObjFixedLength);
    CopyXA();
    PHA();
    LDQ_abs_xy(cpu.Y, AreaObjectLength, cpu.X);
    LDQ_zp(cpu.X, 0x07);
    LDQ_imm(cpu.A, 0x0b);
    STQ_zp(cpu.A, 0x06);
CRendLoop:
    LDQ_PRGTable(cpu.A, CastleMetatiles, cpu.Y);
    STA_abs_xy(MetatileBuffer, cpu.X);
    INX();
    LDQ_zp(cpu.A, 0x06);
    if (BEQ()) goto ChkCFloor;
    INY();
    INY();
    INY();
    INY();
    INY();
    DEC_zp(0x06);
ChkCFloor:
    CMPQ_imm(cpu.X, 0x0b);
    if (BNE()) goto CRendLoop;
    PLA();
    CopyAX();
    LDQ_abs(cpu.A, CurrentPageLoc);
    if (BEQ()) goto ExitCastle;
    LDQ_abs_xy(cpu.A, AreaObjectLength, cpu.X);
    CMPQ_imm(cpu.A, 0x01);
    if (BEQ()) goto PlayerStop;
    LDQ_zp(cpu.Y, 0x07);
    if (BNE()) goto NotTall;
    CMPQ_imm(cpu.A, 0x03);
    if (BEQ()) goto PlayerStop;

NotTall:
    CMPQ_imm(cpu.A, 0x02);
    if (BNE()) goto ExitCastle;
    CALL(GetAreaObjXPosition);
    PHA();
    CALL(FindEmptyEnemySlot);
    PLA();
    STQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    LDQ_abs(cpu.A, CurrentPageLoc);
    STQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Enemy_Y_HighPos, cpu.X);
    STQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    LDQ_imm(cpu.A, 0x90);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    LDQ_imm(cpu.A, StarFlagObject);
    STQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    RET();
PlayerStop:
    LDQ_imm(cpu.Y, 0x52);
    STQ_abs(cpu.Y, MetatileBuffer + 10);
ExitCastle:
    RET();

WaterPipe:
    CALL(GetLrgObjAttrib);
    LDQ_abs_xy(cpu.Y, AreaObjectLength, cpu.X);
    LDQ_zp(cpu.X, 0x07);
    LDQ_imm(cpu.A, 0x6b);
    STA_abs_xy(MetatileBuffer, cpu.X);
    LDQ_imm(cpu.A, 0x6c);
    STA_abs_xy(MetatileBuffer + 1, cpu.X);
    RET();

IntroPipe:
    LDQ_imm(cpu.Y, 0x03);
    CALL(ChkLrgObjFixedLength);
    LDQ_imm(cpu.Y, 0x0a);
    CALL(RenderSidewaysPipe);
    if (BCS()) goto NoBlankP;
    LDQ_imm(cpu.X, 0x06);
VPipeSectLoop:
    LDQ_imm(cpu.A, 0x00);
    STA_abs_xy(MetatileBuffer, cpu.X);
    DEX();
    if (BPL()) goto VPipeSectLoop;
    LDQ_PRGTable(cpu.A, VerticalPipeData, cpu.Y);
    STQ_abs(cpu.A, MetatileBuffer + 7);
NoBlankP:
    RET();

ExitPipe:
    LDQ_imm(cpu.Y, 0x03);
    CALL(ChkLrgObjFixedLength);
    CALL(GetLrgObjAttrib);

RenderSidewaysPipe:
    DEY();
    DEY();
    STQ_zp(cpu.Y, 0x05);
    LDQ_abs_xy(cpu.Y, AreaObjectLength, cpu.X);
    STQ_zp(cpu.Y, 0x06);
    LDQ_zp(cpu.X, 0x05);
    INX();
    LDQ_PRGTable(cpu.A, SidePipeShaftData, cpu.Y);
    CMPQ_imm(cpu.A, 0x00);
    if (BEQ()) goto DrawSidePart;
    LDQ_imm(cpu.X, 0x00);
    LDQ_zp(cpu.Y, 0x05);
    CALL(RenderUnderPart);
    CLC();
DrawSidePart:
    LDQ_zp(cpu.Y, 0x06);
    LDQ_PRGTable(cpu.A, SidePipeTopPart, cpu.Y);
    STA_abs_xy(MetatileBuffer, cpu.X);
    LDQ_PRGTable(cpu.A, SidePipeBottomPart, cpu.Y);
    STA_abs_xy(MetatileBuffer + 1, cpu.X);
    RET();

VerticalPipe:
    CALL(GetPipeHeight);
    LDQ_zp(cpu.A, 0x00);
    if (BEQ()) goto WarpPipe;
    INY();
    INY();
    INY();
    INY();
WarpPipe:
    CopyYA();
    PHA();
    LDQ_abs(cpu.A, AreaNumber);
    ORA_abs(WorldNumber);
    if (BEQ()) goto DrawPipe;
    LDQ_abs_xy(cpu.Y, AreaObjectLength, cpu.X);
    if (BEQ()) goto DrawPipe;
    CALL(FindEmptyEnemySlot);
    if (BCS()) goto DrawPipe;
    CALL(GetAreaObjXPosition);
    CLC();
    ADC_imm(0x08);
    STQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    LDQ_abs(cpu.A, CurrentPageLoc);
    ADC_imm(0x00);
    STQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Enemy_Y_HighPos, cpu.X);
    STQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    CALL(GetAreaObjYPosition);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    LDQ_imm(cpu.A, PiranhaPlant);
    STQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CALL(InitPiranhaPlant);
DrawPipe:
    PLA();
    CopyAY();
    LDQ_zp(cpu.X, 0x07);
    LDQ_PRGTable(cpu.A, VerticalPipeData, cpu.Y);
    STA_abs_xy(MetatileBuffer, cpu.X);
    INX();
    LDQ_PRGTable(cpu.A, VerticalPipeData, cpu.Y + 2);
    LDQ_zp(cpu.Y, 0x06);
    DEY();
    
    goto RenderUnderPart;

GetPipeHeight:
    LDQ_imm(cpu.Y, 0x01);
    CALL(ChkLrgObjFixedLength);
    CALL(GetLrgObjAttrib);
    CopyYA();
    AND_imm(0x07);
    STQ_zp(cpu.A, 0x06);
    LDQ_abs_xy(cpu.Y, AreaObjectLength, cpu.X);
    RET();

FindEmptyEnemySlot:
    LDQ_imm(cpu.X, 0x00);
EmptyChkLoop:
    CLC();
    LDQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    if (BEQ()) goto ExitEmptyChk;
    INX();
    CMPQ_imm(cpu.X, 0x05);
    if (BNE()) goto EmptyChkLoop;

ExitEmptyChk:
    RET();

Hole_Water:
    CALL(ChkLrgObjLength);
    LDQ_imm(cpu.A, 0x86);
    STQ_abs(cpu.A, MetatileBuffer + 10);
    LDQ_imm(cpu.X, 0x0b);
    LDQ_imm(cpu.Y, 0x01);
    LDQ_imm(cpu.A, 0x87);
    
    goto RenderUnderPart;

QuestionBlockRow_High:
    LDQ_imm(cpu.A, 0x03);
    bitFlag = true;

QuestionBlockRow_Low:
    if (!bitFlag)
        LDQ_imm(cpu.A, 0x07);
    else
        bitFlag = false;

    PHA();
    CALL(ChkLrgObjLength);
    PLA();
    CopyAX();
    LDQ_imm(cpu.A, 0xc0);
    STA_abs_xy(MetatileBuffer, cpu.X);
    RET();

Bridge_High:
    LDQ_imm(cpu.A, 0x06);
    bitFlag = true;

Bridge_Middle:
    if (!bitFlag)
        LDQ_imm(cpu.A, 0x07);

    bitFlag = true;

Bridge_Low:
    if (!bitFlag)
        LDQ_imm(cpu.A, 0x09);
    else
        bitFlag = false;

    PHA();
    CALL(ChkLrgObjLength);
    PLA();
    CopyAX();
    LDQ_imm(cpu.A, 0x0b);
    STA_abs_xy(MetatileBuffer, cpu.X);
    INX();
    LDQ_imm(cpu.Y, 0x00);
    LDQ_imm(cpu.A, 0x63);
    
    goto RenderUnderPart;

#ifndef REMOVE_RESIDUAL
FlagBalls_Residual:
    CALL(GetLrgObjAttrib);
    LDQ_imm(cpu.X, 0x02);
    LDQ_imm(cpu.A, 0x6d);

    goto RenderUnderPart;
#endif

FlagpoleObject:
    LDQ_imm(cpu.A, 0x24);
    STQ_abs(cpu.A, MetatileBuffer);
    LDQ_imm(cpu.X, 0x01);
    LDQ_imm(cpu.Y, 0x08);
    LDQ_imm(cpu.A, 0x25);
    CALL(RenderUnderPart);
    LDQ_imm(cpu.A, 0x61);
    STQ_abs(cpu.A, MetatileBuffer + 10);
    CALL(GetAreaObjXPosition);
    SEC();
    SBC_imm(0x08);
    STQ_zp(cpu.A, Enemy_X_Position + 5);
    LDQ_abs(cpu.A, CurrentPageLoc);
    SBC_imm(0x00);
    STQ_zp(cpu.A, Enemy_PageLoc + 5);
    LDQ_imm(cpu.A, 0x30);
    STQ_zp(cpu.A, Enemy_Y_Position + 5);
    LDQ_imm(cpu.A, 0xb0);
    STQ_abs(cpu.A, FlagpoleFNum_Y_Pos);
    LDQ_imm(cpu.A, FlagpoleFlagObject);
    STQ_zp(cpu.A, Enemy_ID + 5);
    INC_zp(Enemy_Flag + 5);
    RET();

EndlessRope:
    LDQ_imm(cpu.X, 0x00);
    LDQ_imm(cpu.Y, 0x0f);
    
    goto DrawRope;

BalancePlatRope:
    CopyXA();
    PHA();
    LDQ_imm(cpu.X, 0x01);
    LDQ_imm(cpu.Y, 0x0f);
    LDQ_imm(cpu.A, 0x44);
    CALL(RenderUnderPart);
    PLA();
    CopyAX();
    CALL(GetLrgObjAttrib);
    LDQ_imm(cpu.X, 0x01);
DrawRope:
    LDQ_imm(cpu.A, 0x40);
    
    goto RenderUnderPart;

RowOfCoins:
    LDQ_abs(cpu.Y, AreaType);
    LDQ_PRGTable(cpu.A, CoinMetatileData, cpu.Y);
    
    goto GetRow;

CastleBridgeObj:
    LDQ_imm(cpu.Y, 0x0c);
    CALL(ChkLrgObjFixedLength);
    
    goto ChainObj;

AxeObj:
    LDQ_imm(cpu.A, 0x08);
    STQ_abs(cpu.A, VRAM_Buffer_AddrCtrl);

ChainObj:
    LDQ_zp(cpu.Y, 0x00);
    LDQ_PRGTable(cpu.X, C_ObjectRow, cpu.Y - 2);
    LDQ_PRGTable(cpu.A, C_ObjectMetatile, cpu.Y - 2);
    
    goto ColObj;

EmptyBlock:
    CALL(GetLrgObjAttrib);
    LDQ_zp(cpu.X, 0x07);
    LDQ_imm(cpu.A, 0xc4);
ColObj:
    LDQ_imm(cpu.Y, 0x00);
    
    goto RenderUnderPart;

RowOfBricks:
    LDQ_abs(cpu.Y, AreaType);
    LDQ_abs(cpu.A, CloudTypeOverride);
    if (BEQ()) goto DrawBricks;
    LDQ_imm(cpu.Y, 0x04);
DrawBricks:
    LDQ_PRGTable(cpu.A, BrickMetatiles, cpu.Y);
    
    goto GetRow;

RowOfSolidBlocks:
    LDQ_abs(cpu.Y, AreaType);
    LDQ_PRGTable(cpu.A, SolidBlockMetatiles, cpu.Y);
GetRow:
    PHA();
    CALL(ChkLrgObjLength);
DrawRow:
    LDQ_zp(cpu.X, 0x07);
    LDQ_imm(cpu.Y, 0x00);
    PLA();
    
    goto RenderUnderPart;

ColumnOfBricks:
    LDQ_abs(cpu.Y, AreaType);
    LDQ_PRGTable(cpu.A, BrickMetatiles, cpu.Y);
    
    goto GetRow2;

ColumnOfSolidBlocks:
    LDQ_abs(cpu.Y, AreaType);
    LDQ_PRGTable(cpu.A, SolidBlockMetatiles, cpu.Y);
GetRow2:
    PHA();
    CALL(GetLrgObjAttrib);
    PLA();
    LDQ_zp(cpu.X, 0x07);
    
    goto RenderUnderPart;

BulletBillCannon:
    CALL(GetLrgObjAttrib);
    LDQ_zp(cpu.X, 0x07);
    LDQ_imm(cpu.A, 0x64);
    STA_abs_xy(MetatileBuffer, cpu.X);
    INX();
    DEY();
    if (BMI()) goto SetupCannon;
    LDQ_imm(cpu.A, 0x65);
    STA_abs_xy(MetatileBuffer, cpu.X);
    INX();
    DEY();
    if (BMI()) goto SetupCannon;
    LDQ_imm(cpu.A, 0x66);
    CALL(RenderUnderPart);
SetupCannon:
    LDQ_abs(cpu.X, Cannon_Offset);
    CALL(GetAreaObjYPosition);
    STA_abs_xy(Cannon_Y_Position, cpu.X);
    LDQ_abs(cpu.A, CurrentPageLoc);
    STA_abs_xy(Cannon_PageLoc, cpu.X);
    CALL(GetAreaObjXPosition);
    STA_abs_xy(Cannon_X_Position, cpu.X);
    INX();
    CMPQ_imm(cpu.X, 0x06);
    if (BCC()) goto StrCOffset;
    LDQ_imm(cpu.X, 0x00);
StrCOffset:
    STQ_abs(cpu.X, Cannon_Offset);
    RET();

StaircaseObject:
    CALL(ChkLrgObjLength);
    if (BCC()) goto NextStair;
    LDQ_imm(cpu.A, 0x09);
    STQ_abs(cpu.A, StaircaseControl);
NextStair:
    DEC_abs(StaircaseControl);
    LDQ_abs(cpu.Y, StaircaseControl);
    LDQ_PRGTable(cpu.X, StaircaseRowData, cpu.Y);
    LDQ_PRGTable(cpu.A, StaircaseHeightData, cpu.Y);
    CopyAY();
    LDQ_imm(cpu.A, 0x61);
    
    goto RenderUnderPart;

Jumpspring:
    CALL(GetLrgObjAttrib);
    CALL(FindEmptyEnemySlot);
    CALL(GetAreaObjXPosition);
    STQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    LDQ_abs(cpu.A, CurrentPageLoc);
    STQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    CALL(GetAreaObjYPosition);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    STQ_zp_xy(cpu.A, Jumpspring_FixedYPos, cpu.X);
    LDQ_imm(cpu.A, JumpspringObject);
    STQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    LDQ_imm(cpu.Y, 0x01);
    STQ_zp_xy(cpu.Y, Enemy_Y_HighPos, cpu.X);
    INC_zp_x(Enemy_Flag);
    LDQ_zp(cpu.X, 0x07);
    LDQ_imm(cpu.A, 0x67);
    STA_abs_xy(MetatileBuffer, cpu.X);
    LDQ_imm(cpu.A, 0x68);
    STA_abs_xy(MetatileBuffer + 1, cpu.X);
    RET();

Hidden1UpBlock:
    LDQ_abs(cpu.A, Hidden1UpFlag);
    if (BEQ()) goto ExitDecBlock;
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, Hidden1UpFlag);
    
    goto BrickWithItem;

QuestionBlock:
    CALL(GetAreaObjectID);
    
    goto DrawQBlk;

BrickWithCoins:
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, BrickCoinTimerFlag);

BrickWithItem:
    CALL(GetAreaObjectID);
    STQ_zp(cpu.Y, 0x07);
    LDQ_imm(cpu.A, 0x00);
    LDQ_abs(cpu.Y, AreaType);
    DEY();
    if (BEQ()) goto BWithL;
    LDQ_imm(cpu.A, 0x05);
BWithL:
    CLC();
    ADC_zp(0x07);
    CopyAY();
DrawQBlk:
    LDQ_PRGTable(cpu.A, BrickQBlockMetatiles, cpu.Y);
    PHA();
    CALL(GetLrgObjAttrib);
    
    goto DrawRow;

GetAreaObjectID:
    LDQ_zp(cpu.A, 0x00);
    SEC();
    SBC_imm(0x00);
    CopyAY();
ExitDecBlock:
    RET();

Hole_Empty:
    CALL(ChkLrgObjLength);
    if (BCC()) goto NoWhirlP;
    LDQ_abs(cpu.A, AreaType);
    if (BNE()) goto NoWhirlP;
    LDQ_abs(cpu.X, Whirlpool_Offset);
    CALL(GetAreaObjXPosition);
    SEC();
    SBC_imm(0x10);
    STA_abs_xy(Whirlpool_LeftExtent, cpu.X);
    LDQ_abs(cpu.A, CurrentPageLoc);
    SBC_imm(0x00);
    STA_abs_xy(Whirlpool_PageLoc, cpu.X);
    INY();
    INY();
    CopyYA();
    ASL_acc();
    ASL_acc();
    ASL_acc();
    ASL_acc();
    STA_abs_xy(Whirlpool_Length, cpu.X);
    INX();
    CMPQ_imm(cpu.X, 0x05);
#ifndef REMOVE_RESIDUAL
    if (BCC()) goto StrWOffset;
    LDQ_imm(cpu.X, 0x00);
    
StrWOffset:
#endif
    STQ_abs(cpu.X, Whirlpool_Offset);

NoWhirlP:
    LDQ_abs(cpu.X, AreaType);
    LDQ_PRGTable(cpu.A, HoleMetatiles, cpu.X);
    LDQ_imm(cpu.X, 0x08);
    LDQ_imm(cpu.Y, 0x0f);

RenderUnderPart:
    STQ_abs(cpu.Y, AreaObjectHeight);
    LDQ_abs_xy(cpu.Y, MetatileBuffer, cpu.X);
    if (BEQ()) goto DrawThisRow;
    CMPQ_imm(cpu.Y, 0x17);
    if (BEQ()) goto WaitOneRow;
    CMPQ_imm(cpu.Y, 0x1a);
    if (BEQ()) goto WaitOneRow;
    CMPQ_imm(cpu.Y, 0xc0);
    if (BEQ()) goto DrawThisRow;
    CMPQ_imm(cpu.Y, 0xc0);
    if (BCS()) goto WaitOneRow;
    CMPQ_imm(cpu.Y, 0x54);
    if (BNE()) goto DrawThisRow;
    CMPQ_imm(cpu.A, 0x50);
    if (BEQ()) goto WaitOneRow;

DrawThisRow:
    STA_abs_xy(MetatileBuffer, cpu.X);
WaitOneRow:
    INX();
    CMPQ_imm(cpu.X, 0x0d);
    if (BCS()) goto ExitUPartR;
    LDQ_abs(cpu.Y, AreaObjectHeight);
    DEY();
    if (BPL()) goto RenderUnderPart;

ExitUPartR:
    RET();

ChkLrgObjLength:
    CALL(GetLrgObjAttrib);

ChkLrgObjFixedLength:
    LDQ_abs_xy(cpu.A, AreaObjectLength, cpu.X);
    CLC();
    if (BPL()) goto LenSet;
    CopyYA();
    STA_abs_xy(AreaObjectLength, cpu.X);
    SEC();
LenSet:
    RET();

GetLrgObjAttrib:
    LDQ_abs_xy(cpu.Y, AreaObjOffsetBuffer, cpu.X);
    LDA_Dyn_ind_y(AreaData);
    AND_imm(0b00001111);
    STQ_zp(cpu.A, 0x07);
    INY();
    LDA_Dyn_ind_y(AreaData);
    AND_imm(0b00001111);
    CopyAY();
    RET();

GetAreaObjXPosition:
    LDQ_abs(cpu.A, CurrentColumnPos);
    ASL_acc();
    ASL_acc();
    ASL_acc();
    ASL_acc();
    RET();

GetAreaObjYPosition:
    LDQ_zp(cpu.A, 0x07);
    ASL_acc();
    ASL_acc();
    ASL_acc();
    ASL_acc();
    CLC();
    ADC_imm(32);
    RET();

GetBlockBufferAddr:
    PHA();
    LSR_acc();
    LSR_acc();
    LSR_acc();
    LSR_acc();
    CopyAY();
    LDQ_PRGTable(cpu.A, BlockBufferAddr, cpu.Y + 2);
    STQ_zp(cpu.A, 0x07);
    PLA();
    AND_imm(0b00001111);
    CLC();
    ADC_PRGTable(BlockBufferAddr, cpu.Y);
    STQ_zp(cpu.A, 0x06);
    RET();

LoadAreaPointer:
    CALL(FindAreaPointer);
    STQ_abs(cpu.A, AreaPointer);
GetAreaType:
    AND_imm(0b01100000);
    ASL_acc();
    ROL_acc();
    ROL_acc();
    ROL_acc();
    STQ_abs(cpu.A, AreaType);
    RET();

FindAreaPointer:
    LDQ_abs(cpu.Y, WorldNumber);
    LDQ_PRGTable(cpu.A, WorldAddrOffsets, cpu.Y);
    CLC();
    ADC_abs(AreaNumber);
    CopyAY();
    LDQ_PRGTable(cpu.A, AreaAddrOffsets, cpu.Y);
    RET();

GetAreaDataAddrs:
    LDQ_abs(cpu.A, AreaPointer);
    CALL(GetAreaType);
    CopyAY();
    LDQ_abs(cpu.A, AreaPointer);
    AND_imm(0b00011111);
    STQ_abs(cpu.A, AreaAddrsLOffset);
    LDQ_PRGTable(cpu.A, EnemyAddrHOffsets, cpu.Y);
    CLC();
    ADC_abs(AreaAddrsLOffset);
    CopyAY();
    LDQ_PRGTable(cpu.A, EnemyDataAddrLow, cpu.Y);
    STQ_zp(cpu.A, EnemyDataLow);
    LDQ_PRGTable(cpu.A, EnemyDataAddrHigh, cpu.Y);
    STQ_zp(cpu.A, EnemyDataHigh);
    LDQ_abs(cpu.Y, AreaType);
    LDQ_PRGTable(cpu.A, AreaDataHOffsets, cpu.Y);
    CLC();
    ADC_abs(AreaAddrsLOffset);
    CopyAY();
    LDQ_PRGTable(cpu.A, AreaDataAddrLow, cpu.Y);
    STQ_zp(cpu.A, AreaDataLow);
    LDQ_PRGTable(cpu.A, AreaDataAddrHigh, cpu.Y);
    STQ_zp(cpu.A, AreaDataHigh);
    LDQ_imm(cpu.Y, 0x00);
    LDA_Dyn_ind_y(AreaData);
    PHA();
    AND_imm(0b00000111);
    CMPQ_imm(cpu.A, 0x04);
    if (BCC()) goto StoreFore;
    STQ_abs(cpu.A, BackgroundColorCtrl);
    LDQ_imm(cpu.A, 0x00);
StoreFore:
    STQ_abs(cpu.A, ForegroundScenery);
    PLA();
    PHA();
    AND_imm(0b00111000);
    LSR_acc();
    LSR_acc();
    LSR_acc();
    STQ_abs(cpu.A, PlayerEntranceCtrl);
    PLA();
    AND_imm(0b11000000);
    CLC();
    ROL_acc();
    ROL_acc();
    ROL_acc();
    STQ_abs(cpu.A, GameTimerSetting);
    INY();
    LDA_Dyn_ind_y(AreaData);
    PHA();
    AND_imm(0b00001111);
    STQ_abs(cpu.A, TerrainControl);
    PLA();
    PHA();
    AND_imm(0b00110000);
    LSR_acc();
    LSR_acc();
    LSR_acc();
    LSR_acc();
    STQ_abs(cpu.A, BackgroundScenery);
    PLA();
    AND_imm(0b11000000);
    CLC();
    ROL_acc();
    ROL_acc();
    ROL_acc();
    CMPQ_imm(cpu.A, 0b00000011);
    if (BNE()) goto StoreStyle;
    STQ_abs(cpu.A, CloudTypeOverride);
    LDQ_imm(cpu.A, 0x00);
StoreStyle:
    STQ_abs(cpu.A, AreaStyle);
    LDQ_zp(cpu.A, AreaDataLow);
    CLC();
    ADC_imm(0x02);
    STQ_zp(cpu.A, AreaDataLow);
    LDQ_zp(cpu.A, AreaDataHigh);
    ADC_imm(0x00);
    STQ_zp(cpu.A, AreaDataHigh);
    RET();

GameMode:
    LDQ_abs(cpu.A, OperMode_Task);
    switch (cpu.A) {
        case 0: goto InitializeArea;
        case 1: goto ScreenRoutines;
        case 2: goto SecondaryGameSetup;
        case 3: goto GameCoreRoutine;
    }
    __builtin_unreachable();

GameCoreRoutine:
    LDQ_abs(cpu.X, CurrentPlayer);
    LDQ_abs_xy(cpu.A, SavedJoypadBits, cpu.X);
    STQ_abs(cpu.A, SavedJoypadBits);
    CALL(GameRoutines);
    LDQ_abs(cpu.A, OperMode_Task);
    CMPQ_imm(cpu.A, 0x03);
    if (BCS()) goto GameEngine;
    RET();

GameEngine:
    CALL(ProcFireball_Bubble);
    LDQ_imm(cpu.X, 0x00);
ProcELoop:
    STQ_zp(cpu.X, ObjectOffset);
    CALL(EnemiesAndLoopsCore);
    CALL(FloateyNumbersRoutine);
    INX();
    CMPQ_imm(cpu.X, 0x06);
    if (BNE()) goto ProcELoop;
    CALL(GetPlayerOffscreenBits);
    CALL(RelativePlayerPosition);
    CALL(PlayerGfxHandler);
    CALL(BlockObjMT_Updater);
    LDQ_imm(cpu.X, 0x01);
    STQ_zp(cpu.X, ObjectOffset);
    CALL(BlockObjectsCore);
    DEX();
    STQ_zp(cpu.X, ObjectOffset);
    CALL(BlockObjectsCore);
    CALL(MiscObjectsCore);
    CALL(ProcessCannons);
    CALL(ProcessWhirlpools);
    CALL(FlagpoleRoutine);
    CALL(RunGameTimer);
    CALL(ColorRotation);
    LDQ_zp(cpu.A, Player_Y_HighPos);
    CMPQ_imm(cpu.A, 0x02);
    if (BPL()) goto NoChgMus;
    LDQ_abs(cpu.A, StarInvincibleTimer);
    if (BEQ()) goto ClrPlrPal;
    CMPQ_imm(cpu.A, 0x04);
    if (BNE()) goto NoChgMus;
    LDQ_abs(cpu.A, IntervalTimerControl);
    if (BNE()) goto NoChgMus;
    CALL(GetAreaMusic);
NoChgMus:
    LDQ_abs(cpu.Y, StarInvincibleTimer);
    LDQ_zp(cpu.A, FrameCounter);
    CMPQ_imm(cpu.Y, 0x08);
    if (BCS()) goto CycleTwo;
    LSR_acc();
    LSR_acc();
CycleTwo:
    LSR_acc();
    CALL(CyclePlayerPalette);
    
    goto SaveAB;
ClrPlrPal:
    CALL(ResetPalStar);
SaveAB:
    LDQ_zp(cpu.A, A_B_Buttons);
    STQ_zp(cpu.A, PreviousA_B_Buttons);
    LDQ_imm(cpu.A, 0x00);
    STQ_zp(cpu.A, Left_Right_Buttons);
UpdScrollVar:
    LDQ_abs(cpu.A, VRAM_Buffer_AddrCtrl);
    CMPQ_imm(cpu.A, 0x06);
    if (BEQ()) goto ExitEng;
    LDQ_abs(cpu.A, AreaParserTaskNum);
    if (BNE()) goto RunParser;
    LDQ_abs(cpu.A, ScrollThirtyTwo);
    CMPQ_imm(cpu.A, 0x20);
    if (BMI()) goto ExitEng;
    LDQ_abs(cpu.A, ScrollThirtyTwo);
    SBC_imm(0x20);
    STQ_abs(cpu.A, ScrollThirtyTwo);
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, VRAM_Buffer2_Offset);
RunParser:
    CALL(AreaParserTaskHandler);
ExitEng:
    RET();

ScrollHandler:
    LDQ_abs(cpu.A, Player_X_Scroll);
    CLC();
    ADC_abs(Platform_X_Scroll);
    STQ_abs(cpu.A, Player_X_Scroll);
    LDQ_abs(cpu.A, ScrollLock);
    if (BNE()) goto InitScrlAmt;
    LDQ_abs(cpu.A, Player_Pos_ForScroll);
    CMPQ_imm(cpu.A, 0x50);
    if (BCC()) goto InitScrlAmt;
    LDQ_abs(cpu.A, SideCollisionTimer);
    if (BNE()) goto InitScrlAmt;
    LDQ_abs(cpu.Y, Player_X_Scroll);
    DEY();
    if (BMI()) goto InitScrlAmt;
    INY();
    CMPQ_imm(cpu.Y, 0x02);
    if (BCC()) goto ChkNearMid;
    DEY();
ChkNearMid:
    LDQ_abs(cpu.A, Player_Pos_ForScroll);
    CMPQ_imm(cpu.A, 0x70);
    if (BCC()) goto ScrollScreen;
    LDQ_abs(cpu.Y, Player_X_Scroll);

ScrollScreen:
    CopyYA();
    STQ_abs(cpu.A, ScrollAmount);
    CLC();
    ADC_abs(ScrollThirtyTwo);
    STQ_abs(cpu.A, ScrollThirtyTwo);
    CopyYA();
    CLC();
    ADC_abs(ScreenLeft_X_Pos);
    STQ_abs(cpu.A, ScreenLeft_X_Pos);
    STQ_abs(cpu.A, HorizontalScroll);
    LDQ_abs(cpu.A, ScreenLeft_PageLoc);
    ADC_imm(0x00);
    STQ_abs(cpu.A, ScreenLeft_PageLoc);
    AND_imm(0x01);
    STQ_zp(cpu.A, 0x00);
    LDQ_abs(cpu.A, Mirror_PPU_CTRL_REG1);
    AND_imm(0b11111110);
    ORA_zp(0x00);
    STQ_abs(cpu.A, Mirror_PPU_CTRL_REG1);
    CALL(GetScreenPosition);
    LDQ_imm(cpu.A, 0x08);
    STQ_abs(cpu.A, ScrollIntervalTimer);
    
    goto ChkPOffscr;
InitScrlAmt:
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, ScrollAmount);
ChkPOffscr:
    LDQ_imm(cpu.X, 0x00);
    CALL(GetXOffscreenBits);
    STQ_zp(cpu.A, 0x00);
    LDQ_imm(cpu.Y, 0x00);
    ASL_acc();
    if (BCS()) goto KeepOnscr;
    INY();
    LDQ_zp(cpu.A, 0x00);
    AND_imm(0b00100000);
    if (BEQ()) goto InitPlatScrl;

KeepOnscr:
    LDQ_abs_xy(cpu.A, ScreenEdge_X_Pos, cpu.Y);
    SEC();
    SBC_PRGTable(X_SubtracterData, cpu.Y);
    STQ_zp(cpu.A, Player_X_Position);
    LDQ_abs_xy(cpu.A, ScreenEdge_PageLoc, cpu.Y);
    SBC_imm(0x00);
    STQ_zp(cpu.A, Player_PageLoc);
    LDQ_zp(cpu.A, Left_Right_Buttons);
    CMPA_PRGTable(OffscrJoypadBitsData, cpu.Y);
    if (BEQ()) goto InitPlatScrl;
    LDQ_imm(cpu.A, 0x00);
    STQ_zp(cpu.A, Player_X_Speed);
InitPlatScrl:
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, Platform_X_Scroll);
    RET();

GetScreenPosition:
    LDQ_abs(cpu.A, ScreenLeft_X_Pos);
    CLC();
    ADC_imm(0xff);
    STQ_abs(cpu.A, ScreenRight_X_Pos);
    LDQ_abs(cpu.A, ScreenLeft_PageLoc);
    ADC_imm(0x00);
    STQ_abs(cpu.A, ScreenRight_PageLoc);
    RET();

GameRoutines:
    LDQ_zp(cpu.A, GameEngineSubroutine);
    switch (cpu.A) {
        case 0: goto Entrance_GameTimerSetup;
        case 1: goto Vine_AutoClimb;
        case 2: goto SideExitPipeEntry;
        case 3: goto VerticalPipeEntry;
        case 4: goto FlagpoleSlide;
        case 5: goto PlayerEndLevel;
        case 6: goto PlayerLoseLife;
        case 7: goto PlayerEntrance;
        case 8: goto PlayerCtrlRoutine;
        case 9: goto PlayerChangeSize;
        case 10: goto PlayerInjuryBlink;
        case 11: goto PlayerDeath;
        case 12: goto PlayerFireFlower;
    }
    __builtin_unreachable();

PlayerEntrance:
    LDQ_abs(cpu.A, AltEntranceControl);
    CMPQ_imm(cpu.A, 0x02);
    if (BEQ()) goto EntrMode2;
    LDQ_imm(cpu.A, 0x00);
    LDQ_zp(cpu.Y, Player_Y_Position);
    CMPQ_imm(cpu.Y, 0x30);
    if (BCC()) goto AutoControlPlayer;
    LDQ_abs(cpu.A, PlayerEntranceCtrl);
    CMPQ_imm(cpu.A, 0x06);
    if (BEQ()) goto ChkBehPipe;
    CMPQ_imm(cpu.A, 0x07);
    if (BNE()) goto PlayerRdy;

ChkBehPipe:
    LDQ_abs(cpu.A, Player_SprAttrib);
    if (BNE()) goto IntroEntr;
    LDQ_imm(cpu.A, 0x01);
    
    goto AutoControlPlayer;
IntroEntr:
    CALL(EnterSidePipe);
    DEC_abs(ChangeAreaTimer);
    if (BNE()) goto ExitEntr;
    INC_abs(DisableIntermediate);
    
    goto NextArea;
EntrMode2:
    LDQ_abs(cpu.A, JoypadOverride);
    if (BNE()) goto VineEntr;
    LDQ_imm(cpu.A, 0xff);
    CALL(MovePlayerYAxis);
    LDQ_zp(cpu.A, Player_Y_Position);
    CMPQ_imm(cpu.A, 0x91);
    if (BCC()) goto PlayerRdy;
    RET();
VineEntr:
    LDQ_abs(cpu.A, VineHeight);
    CMPQ_imm(cpu.A, 0x60);
    if (BNE()) goto ExitEntr;
    LDQ_zp(cpu.A, Player_Y_Position);
    CMPQ_imm(cpu.A, 0x99);
    LDQ_imm(cpu.Y, 0x00);
    LDQ_imm(cpu.A, 0x01);
    if (BCC()) goto OffVine;
    LDQ_imm(cpu.A, 0x03);
    STQ_zp(cpu.A, Player_State);
    INY();
    LDQ_imm(cpu.A, 0x08);
    STQ_abs(cpu.A, Block_Buffer_1 + 0xb4);
OffVine:
    STQ_abs(cpu.Y, DisableCollisionDet);
    CALL(AutoControlPlayer);
    LDQ_zp(cpu.A, Player_X_Position);
    CMPQ_imm(cpu.A, 0x48);
    if (BCC()) goto ExitEntr;

PlayerRdy:
    LDQ_imm(cpu.A, 0x08);
    STQ_zp(cpu.A, GameEngineSubroutine);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp(cpu.A, PlayerFacingDir);
    LSR_acc();
    STQ_abs(cpu.A, AltEntranceControl);
    STQ_abs(cpu.A, DisableCollisionDet);
    STQ_abs(cpu.A, JoypadOverride);
ExitEntr:
    RET();

AutoControlPlayer:
    STQ_abs(cpu.A, SavedJoypadBits);

PlayerCtrlRoutine:
    LDQ_zp(cpu.A, GameEngineSubroutine);
    CMPQ_imm(cpu.A, 0x0b);
    if (BEQ()) goto SizeChk;
    LDQ_abs(cpu.A, AreaType);
    if (BNE()) goto SaveJoyp;
    LDQ_zp(cpu.Y, Player_Y_HighPos);
    DEY();
    if (BNE()) goto DisJoyp;
    LDQ_zp(cpu.A, Player_Y_Position);
    CMPQ_imm(cpu.A, 0xd0);
    if (BCC()) goto SaveJoyp;

DisJoyp:
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, SavedJoypadBits);
SaveJoyp:
    LDQ_abs(cpu.A, SavedJoypadBits);
    AND_imm(0b11000000);
    STQ_zp(cpu.A, A_B_Buttons);
    LDQ_abs(cpu.A, SavedJoypadBits);
    AND_imm(0b00000011);
    STQ_zp(cpu.A, Left_Right_Buttons);
    LDQ_abs(cpu.A, SavedJoypadBits);
    AND_imm(0b00001100);
    STQ_zp(cpu.A, Up_Down_Buttons);
    AND_imm(0b00000100);
    if (BEQ()) goto SizeChk;
    LDQ_zp(cpu.A, Player_State);
    if (BNE()) goto SizeChk;
    LDQ_zp(cpu.Y, Left_Right_Buttons);
    if (BEQ()) goto SizeChk;
    LDQ_imm(cpu.A, 0x00);
    STQ_zp(cpu.A, Left_Right_Buttons);
    STQ_zp(cpu.A, Up_Down_Buttons);
SizeChk:
    CALL(PlayerMovementSubs);
    LDQ_imm(cpu.Y, 0x01);
    LDQ_abs(cpu.A, PlayerSize);
    if (BNE()) goto ChkMoveDir;
    LDQ_imm(cpu.Y, 0x00);
    LDQ_abs(cpu.A, CrouchingFlag);
    if (BEQ()) goto ChkMoveDir;
    LDQ_imm(cpu.Y, 0x02);
ChkMoveDir:
    STQ_abs(cpu.Y, Player_BoundBoxCtrl);
    LDQ_imm(cpu.A, 0x01);
    LDQ_zp(cpu.Y, Player_X_Speed);
    if (BEQ()) goto PlayerSubs;
    if (BPL()) goto SetMoveDir;
    ASL_acc();
SetMoveDir:
    STQ_zp(cpu.A, Player_MovingDir);
PlayerSubs:
    CALL(ScrollHandler);
    CALL(GetPlayerOffscreenBits);
    CALL(RelativePlayerPosition);
    LDQ_imm(cpu.X, 0x00);
    CALL(BoundingBoxCore);
    CALL(PlayerBGCollision);
    LDQ_zp(cpu.A, Player_Y_Position);
    CMPQ_imm(cpu.A, 0x40);
    if (BCC()) goto PlayerHole;
    LDQ_zp(cpu.A, GameEngineSubroutine);
    CMPQ_imm(cpu.A, 0x05);
    if (BEQ()) goto PlayerHole;
    CMPQ_imm(cpu.A, 0x07);
    if (BEQ()) goto PlayerHole;
    CMPQ_imm(cpu.A, 0x04);
    if (BCC()) goto PlayerHole;
    LDQ_abs(cpu.A, Player_SprAttrib);
    AND_imm(0b11011111);
    STQ_abs(cpu.A, Player_SprAttrib);
PlayerHole:
    LDQ_zp(cpu.A, Player_Y_HighPos);
    CMPQ_imm(cpu.A, 0x02);
    if (BMI()) goto ExitCtrl;
    LDQ_imm(cpu.X, 0x01);
    STQ_abs(cpu.X, ScrollLock);
    LDQ_imm(cpu.Y, 0x04);
    STQ_zp(cpu.Y, 0x07);
    LDQ_imm(cpu.X, 0x00);
    LDQ_abs(cpu.Y, GameTimerExpiredFlag);
    if (BNE()) goto HoleDie;
    LDQ_abs(cpu.Y, CloudTypeOverride);
    if (BNE()) goto ChkHoleX;

HoleDie:
    INX();
    LDQ_zp(cpu.Y, GameEngineSubroutine);
    CMPQ_imm(cpu.Y, 0x0b);
    if (BEQ()) goto ChkHoleX;
    LDQ_abs(cpu.Y, DeathMusicLoaded);
    if (BNE()) goto HoleBottom;
    INY();
    STQ_zp(cpu.Y, EventMusicQueue);
    STQ_abs(cpu.Y, DeathMusicLoaded);
HoleBottom:
    LDQ_imm(cpu.Y, 0x06);
    STQ_zp(cpu.Y, 0x07);
ChkHoleX:
    CMPQ_zp(cpu.A, 0x07);
    if (BMI()) goto ExitCtrl;
    DEX();
    if (BMI()) goto CloudExit;
    LDQ_abs(cpu.Y, EventMusicBuffer);
    if (BNE()) goto ExitCtrl;
    LDQ_imm(cpu.A, 0x06);
    STQ_zp(cpu.A, GameEngineSubroutine);
ExitCtrl:
    RET();

CloudExit:
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, JoypadOverride);
    CALL(SetEntr);
    INC_abs(AltEntranceControl);
    RET();

Vine_AutoClimb:
    LDQ_zp(cpu.A, Player_Y_HighPos);
    if (BNE()) goto AutoClimb;
    LDQ_zp(cpu.A, Player_Y_Position);
    CMPQ_imm(cpu.A, 0xe4);
    if (BCC()) goto SetEntr;

AutoClimb:
    LDQ_imm(cpu.A, 0b00001000);
    STQ_abs(cpu.A, JoypadOverride);
    LDQ_imm(cpu.Y, 0x03);
    STQ_zp(cpu.Y, Player_State);
    
    goto AutoControlPlayer;
SetEntr:
    LDQ_imm(cpu.A, 0x02);
    STQ_abs(cpu.A, AltEntranceControl);
    
    goto ChgAreaMode;

VerticalPipeEntry:
    LDQ_imm(cpu.A, 0x01);
    CALL(MovePlayerYAxis);
    CALL(ScrollHandler);
    LDQ_imm(cpu.Y, 0x00);
    LDQ_abs(cpu.A, WarpZoneControl);
    if (BNE()) goto ChgAreaPipe;
    INY();
    LDQ_abs(cpu.A, AreaType);
    CMPQ_imm(cpu.A, 0x03);
    if (BNE()) goto ChgAreaPipe;
    INY();
    
    goto ChgAreaPipe;

MovePlayerYAxis:
    CLC();
    ADC_zp(Player_Y_Position);
    STQ_zp(cpu.A, Player_Y_Position);
    RET();

SideExitPipeEntry:
    CALL(EnterSidePipe);
    LDQ_imm(cpu.Y, 0x02);
ChgAreaPipe:
    DEC_abs(ChangeAreaTimer);
    if (BNE()) goto ExitCAPipe;
    STQ_abs(cpu.Y, AltEntranceControl);
ChgAreaMode:
    INC_abs(DisableScreenFlag);
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, OperMode_Task);
    STQ_abs(cpu.A, Sprite0HitDetectFlag);
ExitCAPipe:
    RET();

EnterSidePipe:
    LDQ_imm(cpu.A, 0x08);
    STQ_zp(cpu.A, Player_X_Speed);
    LDQ_imm(cpu.Y, 0x01);
    LDQ_zp(cpu.A, Player_X_Position);
    AND_imm(0b00001111);
    if (BNE()) goto RightPipe;
    STQ_zp(cpu.A, Player_X_Speed);
    CopyAY();
RightPipe:
    CopyYA();
    CALL(AutoControlPlayer);
    RET();

PlayerChangeSize:
    LDQ_abs(cpu.A, TimerControl);
    CMPQ_imm(cpu.A, 0xf8);
    if (BNE()) goto EndChgSize;
    
    goto InitChangeSize;
EndChgSize:
    CMPQ_imm(cpu.A, 0xc4);
    if (BNE()) goto ExitChgSize;
    CALL(DonePlayerTask);
ExitChgSize:
    RET();

PlayerInjuryBlink:
    LDQ_abs(cpu.A, TimerControl);
    CMPQ_imm(cpu.A, 0xf0);
    if (BCS()) goto ExitBlink;
    CMPQ_imm(cpu.A, 0xc8);
    if (BEQ()) goto DonePlayerTask;
    
    goto PlayerCtrlRoutine;
ExitBlink:
    if (BNE()) goto ExitBoth;

InitChangeSize:
    LDQ_abs(cpu.Y, PlayerChangeSizeFlag);
    if (BNE()) goto ExitBoth;
    STQ_abs(cpu.Y, PlayerAnimCtrl);
    INC_abs(PlayerChangeSizeFlag);
    LDQ_abs(cpu.A, PlayerSize);
    EOR_imm(0x01);
    STQ_abs(cpu.A, PlayerSize);
ExitBoth:
    RET();

PlayerDeath:
    LDQ_abs(cpu.A, TimerControl);
    CMPQ_imm(cpu.A, 0xf0);
    if (BCS()) goto ExitDeath;
    
    goto PlayerCtrlRoutine;

DonePlayerTask:
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, TimerControl);
    LDQ_imm(cpu.A, 0x08);
    STQ_zp(cpu.A, GameEngineSubroutine);
    RET();

PlayerFireFlower:
    LDQ_abs(cpu.A, TimerControl);
    CMPQ_imm(cpu.A, 0xc0);
    if (BEQ()) goto ResetPalFireFlower;
    LDQ_zp(cpu.A, FrameCounter);
    LSR_acc();
    LSR_acc();

CyclePlayerPalette:
    AND_imm(0x03);
    STQ_zp(cpu.A, 0x00);
    LDQ_abs(cpu.A, Player_SprAttrib);
    AND_imm(0b11111100);
    ORA_zp(0x00);
    STQ_abs(cpu.A, Player_SprAttrib);
    RET();

ResetPalFireFlower:
    CALL(DonePlayerTask);

ResetPalStar:
    LDQ_abs(cpu.A, Player_SprAttrib);
    AND_imm(0b11111100);
    STQ_abs(cpu.A, Player_SprAttrib);
    RET();

ExitDeath:
    RET();

FlagpoleSlide:
    LDQ_zp(cpu.A, Enemy_ID + 5);
    CMPQ_imm(cpu.A, FlagpoleFlagObject);
    if (BNE()) goto NoFPObj;
    LDQ_abs(cpu.A, FlagpoleSoundQueue);
    STQ_zp(cpu.A, Square1SoundQueue);
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, FlagpoleSoundQueue);
    LDQ_zp(cpu.Y, Player_Y_Position);
    CMPQ_imm(cpu.Y, 0x9e);
    if (BCS()) goto SlidePlayer;
    LDQ_imm(cpu.A, 0x04);
SlidePlayer:
    
    goto AutoControlPlayer;
NoFPObj:
    INC_zp(GameEngineSubroutine);
    RET();

PlayerEndLevel:
    LDQ_imm(cpu.A, 0x01);
    CALL(AutoControlPlayer);
    LDQ_zp(cpu.A, Player_Y_Position);
    CMPQ_imm(cpu.A, 0xae);
    if (BCC()) goto ChkStop;
    LDQ_abs(cpu.A, ScrollLock);
    if (BEQ()) goto ChkStop;
    LDQ_imm(cpu.A, EndOfLevelMusic);
    STQ_zp(cpu.A, EventMusicQueue);
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, ScrollLock);
ChkStop:
    LDQ_abs(cpu.A, Player_CollisionBits);
    LSR_acc();
    if (BCS()) goto RdyNextA;
    LDQ_abs(cpu.A, StarFlagTaskControl);
    if (BNE()) goto InCastle;
    INC_abs(StarFlagTaskControl);
InCastle:
    LDQ_imm(cpu.A, 0b00100000);
    STQ_abs(cpu.A, Player_SprAttrib);
RdyNextA:
    LDQ_abs(cpu.A, StarFlagTaskControl);
    CMPQ_imm(cpu.A, 0x05);
    if (BNE()) goto ExitNA;
    INC_abs(LevelNumber);
    LDQ_abs(cpu.A, LevelNumber);
    CMPQ_imm(cpu.A, 0x03);
    if (BNE()) goto NextArea;
    LDQ_abs(cpu.Y, WorldNumber);
    LDQ_abs(cpu.A, CoinTallyFor1Ups);
    CMPA_PRGTable(Hidden1UpCoinAmts, cpu.Y);
    if (BCC()) goto NextArea;
    INC_abs(Hidden1UpFlag);
NextArea:
    INC_abs(AreaNumber);
    CALL(LoadAreaPointer);
    INC_abs(FetchNewGameTimerFlag);
    CALL(ChgAreaMode);
    STQ_abs(cpu.A, HalfwayPage);
    LDQ_imm(cpu.A, Silence);
    STQ_zp(cpu.A, EventMusicQueue);
ExitNA:
    RET();

PlayerMovementSubs:
    LDQ_imm(cpu.A, 0x00);
    LDQ_abs(cpu.Y, PlayerSize);
    if (BNE()) goto SetCrouch;
    LDQ_zp(cpu.A, Player_State);
    if (BNE()) goto ProcMove;
    LDQ_zp(cpu.A, Up_Down_Buttons);
    AND_imm(0b00000100);
SetCrouch:
    STQ_abs(cpu.A, CrouchingFlag);
ProcMove:
    CALL(PlayerPhysicsSub);
    LDQ_abs(cpu.A, PlayerChangeSizeFlag);
    if (BNE()) goto NoMoveSub;
    LDQ_zp(cpu.A, Player_State);
    CMPQ_imm(cpu.A, 0x03);
    if (BEQ()) goto MoveSubs;
    LDQ_imm(cpu.Y, 0x18);
    STQ_abs(cpu.Y, ClimbSideTimer);
MoveSubs:
    switch (cpu.A) {
        case 0: goto OnGroundStateSub;
        case 1: goto JumpSwimSub;
        case 2: goto FallingSub;
        case 3: goto ClimbingSub;
    }
    __builtin_unreachable();

NoMoveSub:
    RET();

OnGroundStateSub:
    CALL(GetPlayerAnimSpeed);
    LDQ_zp(cpu.A, Left_Right_Buttons);
    if (BEQ()) goto GndMove;
    STQ_zp(cpu.A, PlayerFacingDir);
GndMove:
    CALL(ImposeFriction);
    CALL(MovePlayerHorizontally);
    STQ_abs(cpu.A, Player_X_Scroll);
    RET();

FallingSub:
    LDQ_abs(cpu.A, VerticalForceDown);
    STQ_abs(cpu.A, VerticalForce);
    
    goto LRAir;

JumpSwimSub:
    LDQ_zp(cpu.Y, Player_Y_Speed);
    if (BPL()) goto DumpFall;
    LDQ_zp(cpu.A, A_B_Buttons);
    AND_imm(A_Button);
    AND_zp(PreviousA_B_Buttons);
    if (BNE()) goto ProcSwim;
    LDQ_abs(cpu.A, JumpOrigin_Y_Position);
    SEC();
    SBC_zp(Player_Y_Position);
    CMPQ_abs(cpu.A, DiffToHaltJump);
    if (BCC()) goto ProcSwim;

DumpFall:
    LDQ_abs(cpu.A, VerticalForceDown);
    STQ_abs(cpu.A, VerticalForce);
ProcSwim:
    LDQ_abs(cpu.A, SwimmingFlag);
    if (BEQ()) goto LRAir;
    CALL(GetPlayerAnimSpeed);
    LDQ_zp(cpu.A, Player_Y_Position);
    CMPQ_imm(cpu.A, 0x14);
    if (BCS()) goto LRWater;
    LDQ_imm(cpu.A, 0x18);
    STQ_abs(cpu.A, VerticalForce);
LRWater:
    LDQ_zp(cpu.A, Left_Right_Buttons);
    if (BEQ()) goto LRAir;
    STQ_zp(cpu.A, PlayerFacingDir);
LRAir:
    LDQ_zp(cpu.A, Left_Right_Buttons);
    if (BEQ()) goto JSMove;
    CALL(ImposeFriction);
JSMove:
    CALL(MovePlayerHorizontally);
    STQ_abs(cpu.A, Player_X_Scroll);
    LDQ_zp(cpu.A, GameEngineSubroutine);
    CMPQ_imm(cpu.A, 0x0b);
    if (BNE()) goto ExitMov1;
    LDQ_imm(cpu.A, 0x28);
    STQ_abs(cpu.A, VerticalForce);
ExitMov1:
    
    goto MovePlayerVertically;

ClimbingSub:
    LDQ_abs(cpu.A, Player_YMF_Dummy);
    CLC();
    ADC_abs(Player_Y_MoveForce);
    STQ_abs(cpu.A, Player_YMF_Dummy);
    LDQ_imm(cpu.Y, 0x00);
    LDQ_zp(cpu.A, Player_Y_Speed);
    if (BPL()) goto MoveOnVine;
    DEY();
MoveOnVine:
    STQ_zp(cpu.Y, 0x00);
    ADC_zp(Player_Y_Position);
    STQ_zp(cpu.A, Player_Y_Position);
    LDQ_zp(cpu.A, Player_Y_HighPos);
    ADC_zp(0x00);
    STQ_zp(cpu.A, Player_Y_HighPos);
    LDQ_zp(cpu.A, Left_Right_Buttons);
    AND_abs(Player_CollisionBits);
    if (BEQ()) goto InitCSTimer;
    LDQ_abs(cpu.Y, ClimbSideTimer);
    if (BNE()) goto ExitCSub;
    LDQ_imm(cpu.Y, 0x18);
    STQ_abs(cpu.Y, ClimbSideTimer);
    LDQ_imm(cpu.X, 0x00);
    LDQ_zp(cpu.Y, PlayerFacingDir);
    LSR_acc();
    if (BCS()) goto ClimbFD;
    INX();
    INX();
ClimbFD:
    DEY();
    if (BEQ()) goto CSetFDir;
    INX();
CSetFDir:
    LDQ_zp(cpu.A, Player_X_Position);
    CLC();
    ADC_PRGTable(ClimbAdderLow, cpu.X);
    STQ_zp(cpu.A, Player_X_Position);
    LDQ_zp(cpu.A, Player_PageLoc);
    ADC_PRGTable(ClimbAdderHigh, cpu.X);
    STQ_zp(cpu.A, Player_PageLoc);
    LDQ_zp(cpu.A, Left_Right_Buttons);
    EOR_imm(0b00000011);
    STQ_zp(cpu.A, PlayerFacingDir);
ExitCSub:
    RET();
InitCSTimer:
    STQ_abs(cpu.A, ClimbSideTimer);
    RET();

PlayerPhysicsSub:
    LDQ_zp(cpu.A, Player_State);
    CMPQ_imm(cpu.A, 0x03);
    if (BNE()) goto CheckForJumping;
    LDQ_imm(cpu.Y, 0x00);
    LDQ_zp(cpu.A, Up_Down_Buttons);
    AND_abs(Player_CollisionBits);
    if (BEQ()) goto ProcClimb;
    INY();
    AND_imm(0b00001000);
    if (BNE()) goto ProcClimb;
    INY();
ProcClimb:
    LDQ_PRGTable(cpu.X, Climb_Y_MForceData, cpu.Y);
    STQ_abs(cpu.X, Player_Y_MoveForce);
    LDQ_imm(cpu.A, 0x08);
    LDQ_PRGTable(cpu.X, Climb_Y_SpeedData, cpu.Y);
    STQ_zp(cpu.X, Player_Y_Speed);
    if (BMI()) goto SetCAnim;
    LSR_acc();
SetCAnim:
    STQ_abs(cpu.A, PlayerAnimTimerSet);
    RET();

CheckForJumping:
    LDQ_abs(cpu.A, JumpspringAnimCtrl);
    if (BNE()) goto NoJump;
    LDQ_zp(cpu.A, A_B_Buttons);
    AND_imm(A_Button);
    if (BEQ()) goto NoJump;
    AND_zp(PreviousA_B_Buttons);
    if (BEQ()) goto ProcJumping;

NoJump:
    
    goto X_Physics;

ProcJumping:
    LDQ_zp(cpu.A, Player_State);
    if (BEQ()) goto InitJS;
    LDQ_abs(cpu.A, SwimmingFlag);
    if (BEQ()) goto NoJump;
    LDQ_abs(cpu.A, JumpSwimTimer);
    if (BNE()) goto InitJS;
    LDQ_zp(cpu.A, Player_Y_Speed);
    if (BPL()) goto InitJS;
    
    goto X_Physics;
InitJS:
    LDQ_imm(cpu.A, 0x20);
    STQ_abs(cpu.A, JumpSwimTimer);
    LDQ_imm(cpu.Y, 0x00);
    STQ_abs(cpu.Y, Player_YMF_Dummy);
    STQ_abs(cpu.Y, Player_Y_MoveForce);
    LDQ_zp(cpu.A, Player_Y_HighPos);
    STQ_abs(cpu.A, JumpOrigin_Y_HighPos);
    LDQ_zp(cpu.A, Player_Y_Position);
    STQ_abs(cpu.A, JumpOrigin_Y_Position);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp(cpu.A, Player_State);
    LDQ_abs(cpu.A, Player_XSpeedAbsolute);
    CMPQ_imm(cpu.A, 0x09);
    if (BCC()) goto ChkWtr;
    INY();
    CMPQ_imm(cpu.A, 0x10);
    if (BCC()) goto ChkWtr;
    INY();
    CMPQ_imm(cpu.A, 0x19);
    if (BCC()) goto ChkWtr;
    INY();
    CMPQ_imm(cpu.A, 0x1c);
    if (BCC()) goto ChkWtr;
    INY();
ChkWtr:
    LDQ_imm(cpu.A, 0x01);
    STQ_abs(cpu.A, DiffToHaltJump);
    LDQ_abs(cpu.A, SwimmingFlag);
    if (BEQ()) goto GetYPhy;
    LDQ_imm(cpu.Y, 0x05);
    LDQ_abs(cpu.A, Whirlpool_Flag);
    if (BEQ()) goto GetYPhy;
    INY();
GetYPhy:
    LDQ_PRGTable(cpu.A, JumpMForceData, cpu.Y);
    STQ_abs(cpu.A, VerticalForce);
    LDQ_PRGTable(cpu.A, FallMForceData, cpu.Y);
    STQ_abs(cpu.A, VerticalForceDown);
    LDQ_PRGTable(cpu.A, InitMForceData, cpu.Y);
    STQ_abs(cpu.A, Player_Y_MoveForce);
    LDQ_PRGTable(cpu.A, PlayerYSpdData, cpu.Y);
    STQ_zp(cpu.A, Player_Y_Speed);
    LDQ_abs(cpu.A, SwimmingFlag);
    if (BEQ()) goto PJumpSnd;
    LDQ_imm(cpu.A, Sfx_EnemyStomp);
    STQ_zp(cpu.A, Square1SoundQueue);
    LDQ_zp(cpu.A, Player_Y_Position);
    CMPQ_imm(cpu.A, 0x14);
    if (BCS()) goto X_Physics;
    LDQ_imm(cpu.A, 0x00);
    STQ_zp(cpu.A, Player_Y_Speed);
    
    goto X_Physics;
PJumpSnd:
    LDQ_imm(cpu.A, Sfx_BigJump);
    LDQ_abs(cpu.Y, PlayerSize);
    if (BEQ()) goto SJumpSnd;
    LDQ_imm(cpu.A, Sfx_SmallJump);
SJumpSnd:
    STQ_zp(cpu.A, Square1SoundQueue);
X_Physics:
    LDQ_imm(cpu.Y, 0x00);
    STQ_zp(cpu.Y, 0x00);
    LDQ_zp(cpu.A, Player_State);
    if (BEQ()) goto ProcPRun;
    LDQ_abs(cpu.A, Player_XSpeedAbsolute);
    CMPQ_imm(cpu.A, 0x19);
    if (BCS()) goto GetXPhy;
    if (BCC()) goto ChkRFast;

ProcPRun:
    INY();
    LDQ_abs(cpu.A, AreaType);
    if (BEQ()) goto ChkRFast;
    DEY();
    LDQ_zp(cpu.A, Left_Right_Buttons);
    CMPQ_zp(cpu.A, Player_MovingDir);
    if (BNE()) goto ChkRFast;
    LDQ_zp(cpu.A, A_B_Buttons);
    AND_imm(B_Button);
    if (BNE()) goto SetRTmr;
    LDQ_abs(cpu.A, RunningTimer);
    if (BNE()) goto GetXPhy;

ChkRFast:
    INY();
    INC_zp(0x00);
    LDQ_abs(cpu.A, RunningSpeed);
    if (BNE()) goto FastXSp;
    LDQ_abs(cpu.A, Player_XSpeedAbsolute);
    CMPQ_imm(cpu.A, 0x21);
    if (BCC()) goto GetXPhy;

FastXSp:
    INC_zp(0x00);
    
    goto GetXPhy;
SetRTmr:
    LDQ_imm(cpu.A, 0x0a);
    STQ_abs(cpu.A, RunningTimer);
GetXPhy:
    LDQ_PRGTable(cpu.A, MaxLeftXSpdData, cpu.Y);
    STQ_abs(cpu.A, MaximumLeftSpeed);
    LDQ_zp(cpu.A, GameEngineSubroutine);
    CMPQ_imm(cpu.A, 0x07);
    if (BNE()) goto GetXPhy2;
    LDQ_imm(cpu.Y, 0x03);
GetXPhy2:
    LDQ_PRGTable(cpu.A, MaxRightXSpdData, cpu.Y);
    STQ_abs(cpu.A, MaximumRightSpeed);
    LDQ_zp(cpu.Y, 0x00);
    LDQ_PRGTable(cpu.A, FrictionData, cpu.Y);
    STQ_abs(cpu.A, FrictionAdderLow);
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, FrictionAdderHigh);
    LDQ_zp(cpu.A, PlayerFacingDir);
    CMPQ_zp(cpu.A, Player_MovingDir);
    if (BEQ()) goto ExitPhy;
    ASL_abs(FrictionAdderLow);
    ROL_abs(FrictionAdderHigh);
ExitPhy:
    RET();

GetPlayerAnimSpeed:
    LDQ_imm(cpu.Y, 0x00);
    LDQ_abs(cpu.A, Player_XSpeedAbsolute);
    CMPQ_imm(cpu.A, 0x1c);
    if (BCS()) goto SetRunSpd;
    INY();
    CMPQ_imm(cpu.A, 0x0e);
    if (BCS()) goto ChkSkid;
    INY();
ChkSkid:
    LDQ_abs(cpu.A, SavedJoypadBits);
    AND_imm(0b01111111);
    if (BEQ()) goto SetAnimSpd;
    AND_imm(0x03);
    CMPQ_zp(cpu.A, Player_MovingDir);
    if (BNE()) goto ProcSkid;
    LDQ_imm(cpu.A, 0x00);
SetRunSpd:
    STQ_abs(cpu.A, RunningSpeed);
    
    goto SetAnimSpd;
ProcSkid:
    LDQ_abs(cpu.A, Player_XSpeedAbsolute);
    CMPQ_imm(cpu.A, 0x0b);
    if (BCS()) goto SetAnimSpd;
    LDQ_zp(cpu.A, PlayerFacingDir);
    STQ_zp(cpu.A, Player_MovingDir);
    LDQ_imm(cpu.A, 0x00);
    STQ_zp(cpu.A, Player_X_Speed);
    STQ_abs(cpu.A, Player_X_MoveForce);
SetAnimSpd:
    LDQ_PRGTable(cpu.A, PlayerAnimTmrData, cpu.Y);
    STQ_abs(cpu.A, PlayerAnimTimerSet);
    RET();

ImposeFriction:
    AND_abs(Player_CollisionBits);
    CMPQ_imm(cpu.A, 0x00);
    if (BNE()) goto JoypFrict;
    LDQ_zp(cpu.A, Player_X_Speed);
    if (BEQ()) goto SetAbsSpd;
    if (BPL()) goto RghtFrict;
    if (BMI()) goto LeftFrict;

JoypFrict:
    LSR_acc();
    if (BCC()) goto RghtFrict;

LeftFrict:
    LDQ_abs(cpu.A, Player_X_MoveForce);
    CLC();
    ADC_abs(FrictionAdderLow);
    STQ_abs(cpu.A, Player_X_MoveForce);
    LDQ_zp(cpu.A, Player_X_Speed);
    ADC_abs(FrictionAdderHigh);
    STQ_zp(cpu.A, Player_X_Speed);
    CMPQ_abs(cpu.A, MaximumRightSpeed);
    if (BMI()) goto XSpdSign;
    LDQ_abs(cpu.A, MaximumRightSpeed);
    STQ_zp(cpu.A, Player_X_Speed);
    
    goto SetAbsSpd;
RghtFrict:
    LDQ_abs(cpu.A, Player_X_MoveForce);
    SEC();
    SBC_abs(FrictionAdderLow);
    STQ_abs(cpu.A, Player_X_MoveForce);
    LDQ_zp(cpu.A, Player_X_Speed);
    SBC_abs(FrictionAdderHigh);
    STQ_zp(cpu.A, Player_X_Speed);
    CMPQ_abs(cpu.A, MaximumLeftSpeed);
    if (BPL()) goto XSpdSign;
    LDQ_abs(cpu.A, MaximumLeftSpeed);
    STQ_zp(cpu.A, Player_X_Speed);
XSpdSign:
    CMPQ_imm(cpu.A, 0x00);
    if (BPL()) goto SetAbsSpd;
    EOR_imm(0xff);
    CLC();
    ADC_imm(0x01);
SetAbsSpd:
    STQ_abs(cpu.A, Player_XSpeedAbsolute);
    RET();

ProcFireball_Bubble:
    LDQ_abs(cpu.A, PlayerStatus);
    CMPQ_imm(cpu.A, 0x02);
    if (BCC()) goto ProcAirBubbles;
    LDQ_zp(cpu.A, A_B_Buttons);
    AND_imm(B_Button);
    if (BEQ()) goto ProcFireballs;
    AND_zp(PreviousA_B_Buttons);
    if (BNE()) goto ProcFireballs;
    LDQ_abs(cpu.A, FireballCounter);
    AND_imm(0b00000001);
    CopyAX();
    LDQ_zp_xy(cpu.A, Fireball_State, cpu.X);
    if (BNE()) goto ProcFireballs;
    LDQ_zp(cpu.Y, Player_Y_HighPos);
    DEY();
    if (BNE()) goto ProcFireballs;
    LDQ_abs(cpu.A, CrouchingFlag);
    if (BNE()) goto ProcFireballs;
    LDQ_zp(cpu.A, Player_State);
    CMPQ_imm(cpu.A, 0x03);
    if (BEQ()) goto ProcFireballs;
    LDQ_imm(cpu.A, Sfx_Fireball);
    STQ_zp(cpu.A, Square1SoundQueue);
    LDQ_imm(cpu.A, 0x02);
    STQ_zp_xy(cpu.A, Fireball_State, cpu.X);
    LDQ_abs(cpu.Y, PlayerAnimTimerSet);
    STQ_abs(cpu.Y, FireballThrowingTimer);
    DEY();
    STQ_abs(cpu.Y, PlayerAnimTimer);
    INC_abs(FireballCounter);

ProcFireballs:
    LDQ_imm(cpu.X, 0x00);
    CALL(FireballObjCore);
    LDQ_imm(cpu.X, 0x01);
    CALL(FireballObjCore);

ProcAirBubbles:
    LDQ_abs(cpu.A, AreaType);
    if (BNE()) goto BublExit;
    LDQ_imm(cpu.X, 0x02);
BublLoop:
    STQ_zp(cpu.X, ObjectOffset);
    CALL(BubbleCheck);
    CALL(RelativeBubblePosition);
    CALL(GetBubbleOffscreenBits);
    CALL(DrawBubble);
    DEX();
    if (BPL()) goto BublLoop;

BublExit:
    RET();

FireballObjCore:
    STQ_zp(cpu.X, ObjectOffset);
    LDQ_zp_xy(cpu.A, Fireball_State, cpu.X);
    ASL_acc();
    if (BCS()) goto FireballExplosion;
    LDQ_zp_xy(cpu.Y, Fireball_State, cpu.X);
    if (BEQ()) goto NoFBall;
    DEY();
    if (BEQ()) goto RunFB;
    LDQ_zp(cpu.A, Player_X_Position);
    ADC_imm(0x04);
    STQ_zp_xy(cpu.A, Fireball_X_Position, cpu.X);
    LDQ_zp(cpu.A, Player_PageLoc);
    ADC_imm(0x00);
    STQ_zp_xy(cpu.A, Fireball_PageLoc, cpu.X);
    LDQ_zp(cpu.A, Player_Y_Position);
    STQ_zp_xy(cpu.A, Fireball_Y_Position, cpu.X);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Fireball_Y_HighPos, cpu.X);
    LDQ_zp(cpu.Y, PlayerFacingDir);
    DEY();
    LDQ_PRGTable(cpu.A, FireballXSpdData, cpu.Y);
    STQ_zp_xy(cpu.A, Fireball_X_Speed, cpu.X);
    LDQ_imm(cpu.A, 0x04);
    STQ_zp_xy(cpu.A, Fireball_Y_Speed, cpu.X);
    LDQ_imm(cpu.A, 0x07);
    STA_abs_xy(Fireball_BoundBoxCtrl, cpu.X);
    DEC_zp_x(Fireball_State);
RunFB:
    CopyXA();
    CLC();
    ADC_imm(0x07);
    CopyAX();
    LDQ_imm(cpu.A, 0x50);
    STQ_zp(cpu.A, 0x00);
    LDQ_imm(cpu.A, 0x03);
    STQ_zp(cpu.A, 0x02);
    LDQ_imm(cpu.A, 0x00);
    CALL(ImposeGravity);
    CALL(MoveObjectHorizontally);
    LDQ_zp(cpu.X, ObjectOffset);
    CALL(RelativeFireballPosition);
    CALL(GetFireballOffscreenBits);
    CALL(GetFireballBoundBox);
    CALL(FireballBGCollision);
    LDQ_abs(cpu.A, FBall_OffscreenBits);
    AND_imm(0b11001100);
    if (BNE()) goto EraseFB;
    CALL(FireballEnemyCollision);
    
    goto DrawFireball;
EraseFB:
    LDQ_imm(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, Fireball_State, cpu.X);
NoFBall:
    RET();

FireballExplosion:
    CALL(RelativeFireballPosition);
    
    goto DrawExplosion_Fireball;

BubbleCheck:
    LDQ_abs_xy(cpu.A, PseudoRandomBitReg + 1, cpu.X);
    AND_imm(0x01);
    STQ_zp(cpu.A, 0x07);
    LDQ_zp_xy(cpu.A, Bubble_Y_Position, cpu.X);
    CMPQ_imm(cpu.A, 0xf8);
    if (BNE()) goto MoveBubl;
    LDQ_abs(cpu.A, AirBubbleTimer);
    if (BNE()) goto ExitBubl;

SetupBubble:
    LDQ_imm(cpu.Y, 0x00);
    LDQ_zp(cpu.A, PlayerFacingDir);
    LSR_acc();
    if (BCC()) goto PosBubl;
    LDQ_imm(cpu.Y, 0x08);
PosBubl:
    CopyYA();
    ADC_zp(Player_X_Position);
    STQ_zp_xy(cpu.A, Bubble_X_Position, cpu.X);
    LDQ_zp(cpu.A, Player_PageLoc);
    ADC_imm(0x00);
    STQ_zp_xy(cpu.A, Bubble_PageLoc, cpu.X);
    LDQ_zp(cpu.A, Player_Y_Position);
    CLC();
    ADC_imm(0x08);
    STQ_zp_xy(cpu.A, Bubble_Y_Position, cpu.X);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Bubble_Y_HighPos, cpu.X);
    LDQ_zp(cpu.Y, 0x07);
    LDQ_PRGTable(cpu.A, BubbleTimerData, cpu.Y);
    STQ_abs(cpu.A, AirBubbleTimer);
MoveBubl:
    LDQ_zp(cpu.Y, 0x07);
    LDQ_abs_xy(cpu.A, Bubble_YMF_Dummy, cpu.X);
    SEC();
    SBC_PRGTable(Bubble_MForceData, cpu.Y);
    STA_abs_xy(Bubble_YMF_Dummy, cpu.X);
    LDQ_zp_xy(cpu.A, Bubble_Y_Position, cpu.X);
    SBC_imm(0x00);
    CMPQ_imm(cpu.A, 0x20);
    if (BCS()) goto Y_Bubl;
    LDQ_imm(cpu.A, 0xf8);
Y_Bubl:
    STQ_zp_xy(cpu.A, Bubble_Y_Position, cpu.X);
ExitBubl:
    RET();

RunGameTimer:
    LDQ_abs(cpu.A, OperMode);
    if (BEQ()) goto ExGTimer;
    LDQ_zp(cpu.A, GameEngineSubroutine);
    CMPQ_imm(cpu.A, 0x08);
    if (BCC()) goto ExGTimer;
    CMPQ_imm(cpu.A, 0x0b);
    if (BEQ()) goto ExGTimer;
    LDQ_zp(cpu.A, Player_Y_HighPos);
    CMPQ_imm(cpu.A, 0x02);
    if (BCS()) goto ExGTimer;
    LDQ_abs(cpu.A, GameTimerCtrlTimer);
    if (BNE()) goto ExGTimer;
    LDQ_abs(cpu.A, GameTimerDisplay);
    ORA_abs(GameTimerDisplay + 1);
    ORA_abs(GameTimerDisplay + 2);
    if (BEQ()) goto TimeUpOn;
    LDQ_abs(cpu.Y, GameTimerDisplay);
    DEY();
    if (BNE()) goto ResGTCtrl;
    LDQ_abs(cpu.A, GameTimerDisplay + 1);
    ORA_abs(GameTimerDisplay + 2);
    if (BNE()) goto ResGTCtrl;
    LDQ_imm(cpu.A, TimeRunningOutMusic);
    STQ_zp(cpu.A, EventMusicQueue);
ResGTCtrl:
    LDQ_imm(cpu.A, 0x18);
    STQ_abs(cpu.A, GameTimerCtrlTimer);
    LDQ_imm(cpu.Y, 0x23);
    LDQ_imm(cpu.A, 0xff);
    STQ_abs(cpu.A, DigitModifier + 5);
    CALL(DigitsMathRoutine);
    LDQ_imm(cpu.A, 0xa4);
    
    goto PrintStatusBarNumbers;
TimeUpOn:
    STQ_abs(cpu.A, PlayerStatus);
    CALL(ForceInjury);
    INC_abs(GameTimerExpiredFlag);
ExGTimer:
    RET();

WarpZoneObject:
    LDQ_abs(cpu.A, ScrollLock);
    if (BEQ()) goto ExGTimer;
    LDQ_zp(cpu.A, Player_Y_Position);
    AND_zp(Player_Y_HighPos);
    if (BNE()) goto ExGTimer;
    STQ_abs(cpu.A, ScrollLock);
    INC_abs(WarpZoneControl);
    
    goto EraseEnemyObject;

ProcessWhirlpools:
    LDQ_abs(cpu.A, AreaType);
    if (BNE()) goto ExitWh;
    STQ_abs(cpu.A, Whirlpool_Flag);
    LDQ_abs(cpu.A, TimerControl);
    if (BNE()) goto ExitWh;
    LDQ_imm(cpu.Y, 0x04);
WhLoop:
    LDQ_abs_xy(cpu.A, Whirlpool_LeftExtent, cpu.Y);
    CLC();
    ADC_abs_y(Whirlpool_Length);
    STQ_zp(cpu.A, 0x02);
    LDQ_abs_xy(cpu.A, Whirlpool_PageLoc, cpu.Y);
    if (BEQ()) goto NextWh;
    ADC_imm(0x00);
    STQ_zp(cpu.A, 0x01);
    LDQ_zp(cpu.A, Player_X_Position);
    SEC();
    SBC_abs_y(Whirlpool_LeftExtent);
    LDQ_zp(cpu.A, Player_PageLoc);
    SBC_abs_y(Whirlpool_PageLoc);
    if (BMI()) goto NextWh;
    LDQ_zp(cpu.A, 0x02);
    SEC();
    SBC_zp(Player_X_Position);
    LDQ_zp(cpu.A, 0x01);
    SBC_zp(Player_PageLoc);
    if (BPL()) goto WhirlpoolActivate;

NextWh:
    DEY();
    if (BPL()) goto WhLoop;

ExitWh:
    RET();

WhirlpoolActivate:
    LDQ_abs_xy(cpu.A, Whirlpool_Length, cpu.Y);
    LSR_acc();
    STQ_zp(cpu.A, 0x00);
    LDQ_abs_xy(cpu.A, Whirlpool_LeftExtent, cpu.Y);
    CLC();
    ADC_zp(0x00);
    STQ_zp(cpu.A, 0x01);
    LDQ_abs_xy(cpu.A, Whirlpool_PageLoc, cpu.Y);
    ADC_imm(0x00);
    STQ_zp(cpu.A, 0x00);
    LDQ_zp(cpu.A, FrameCounter);
    LSR_acc();
    if (BCC()) goto WhPull;
    LDQ_zp(cpu.A, 0x01);
    SEC();
    SBC_zp(Player_X_Position);
    LDQ_zp(cpu.A, 0x00);
    SBC_zp(Player_PageLoc);
    if (BPL()) goto LeftWh;
    LDQ_zp(cpu.A, Player_X_Position);
    SEC();
    SBC_imm(0x01);
    STQ_zp(cpu.A, Player_X_Position);
    LDQ_zp(cpu.A, Player_PageLoc);
    SBC_imm(0x00);
    
    goto SetPWh;
LeftWh:
    LDQ_abs(cpu.A, Player_CollisionBits);
    LSR_acc();
    if (BCC()) goto WhPull;
    LDQ_zp(cpu.A, Player_X_Position);
    CLC();
    ADC_imm(0x01);
    STQ_zp(cpu.A, Player_X_Position);
    LDQ_zp(cpu.A, Player_PageLoc);
    ADC_imm(0x00);
SetPWh:
    STQ_zp(cpu.A, Player_PageLoc);
WhPull:
    LDQ_imm(cpu.A, 0x10);
    STQ_zp(cpu.A, 0x00);
    LDQ_imm(cpu.A, 0x01);
    STQ_abs(cpu.A, Whirlpool_Flag);
    STQ_zp(cpu.A, 0x02);
    LSR_acc();
    CopyAX();
    
    goto ImposeGravity;

FlagpoleRoutine:
    LDQ_imm(cpu.X, 0x05);
    STQ_zp(cpu.X, ObjectOffset);
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, FlagpoleFlagObject);
    if (BNE()) goto ExitFlagP;
    LDQ_zp(cpu.A, GameEngineSubroutine);
    CMPQ_imm(cpu.A, 0x04);
    if (BNE()) goto SkipScore;
    LDQ_zp(cpu.A, Player_State);
    CMPQ_imm(cpu.A, 0x03);
    if (BNE()) goto SkipScore;
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CMPQ_imm(cpu.A, 0xaa);
    if (BCS()) goto GiveFPScr;
    LDQ_zp(cpu.A, Player_Y_Position);
    CMPQ_imm(cpu.A, 0xa2);
    if (BCS()) goto GiveFPScr;
    LDQ_abs_xy(cpu.A, Enemy_YMF_Dummy, cpu.X);
    ADC_imm(0xff);
    STA_abs_xy(Enemy_YMF_Dummy, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    ADC_imm(0x01);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    LDQ_abs(cpu.A, FlagpoleFNum_YMFDummy);
    SEC();
    SBC_imm(0xff);
    STQ_abs(cpu.A, FlagpoleFNum_YMFDummy);
    LDQ_abs(cpu.A, FlagpoleFNum_Y_Pos);
    SBC_imm(0x01);
    STQ_abs(cpu.A, FlagpoleFNum_Y_Pos);
SkipScore:
    
    goto FPGfx;
GiveFPScr:
    LDQ_abs(cpu.Y, FlagpoleScore);
    LDQ_PRGTable(cpu.A, FlagpoleScoreMods, cpu.Y);
    LDQ_PRGTable(cpu.X, FlagpoleScoreDigits, cpu.Y);
    STA_abs_xy(DigitModifier, cpu.X);
    CALL(AddToScore);
    LDQ_imm(cpu.A, 0x05);
    STQ_zp(cpu.A, GameEngineSubroutine);
FPGfx:
    CALL(GetEnemyOffscreenBits);
    CALL(RelativeEnemyPosition);
    CALL(FlagpoleGfxHandler);
ExitFlagP:
    RET();

JumpspringHandler:
    CALL(GetEnemyOffscreenBits);
    LDQ_abs(cpu.A, TimerControl);
    if (BNE()) goto DrawJSpr;
    LDQ_abs(cpu.A, JumpspringAnimCtrl);
    if (BEQ()) goto DrawJSpr;
    CopyAY();
    DEY();
    CopyYA();
    AND_imm(0b00000010);
    if (BNE()) goto DownJSpr;
    INC_zp(Player_Y_Position);
    INC_zp(Player_Y_Position);
    
    goto PosJSpr;
DownJSpr:
    DEC_zp(Player_Y_Position);
    DEC_zp(Player_Y_Position);
PosJSpr:
    LDQ_zp_xy(cpu.A, Jumpspring_FixedYPos, cpu.X);
    CLC();
    ADC_PRGTable(Jumpspring_Y_PosData, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CMPQ_imm(cpu.Y, 0x01);
    if (BCC()) goto BounceJS;
    LDQ_zp(cpu.A, A_B_Buttons);
    AND_imm(A_Button);
    if (BEQ()) goto BounceJS;
    AND_zp(PreviousA_B_Buttons);
    if (BNE()) goto BounceJS;
    LDQ_imm(cpu.A, 0xf4);
    STQ_abs(cpu.A, JumpspringForce);
BounceJS:
    CMPQ_imm(cpu.Y, 0x03);
    if (BNE()) goto DrawJSpr;
    LDQ_abs(cpu.A, JumpspringForce);
    STQ_zp(cpu.A, Player_Y_Speed);
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, JumpspringAnimCtrl);
DrawJSpr:
    CALL(RelativeEnemyPosition);
    CALL(EnemyGfxHandler);
    CALL(OffscreenBoundsCheck);
    LDQ_abs(cpu.A, JumpspringAnimCtrl);
    if (BEQ()) goto ExJSpring;
    LDQ_abs(cpu.A, JumpspringTimer);
    if (BNE()) goto ExJSpring;
    LDQ_imm(cpu.A, 0x04);
    STQ_abs(cpu.A, JumpspringTimer);
    INC_abs(JumpspringAnimCtrl);
ExJSpring:
    RET();

Setup_Vine:
    LDQ_imm(cpu.A, VineObject);
    STQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    LDQ_abs_xy(cpu.A, Block_PageLoc, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    LDQ_abs_xy(cpu.A, Block_X_Position, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    LDQ_abs_xy(cpu.A, Block_Y_Position, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    LDQ_abs(cpu.Y, VineFlagOffset);
    if (BNE()) goto NextVO;
    STQ_abs(cpu.A, VineStart_Y_Position);
NextVO:
    CopyXA();
    STA_abs_xy(VineObjOffset, cpu.Y);
    INC_abs(VineFlagOffset);
    LDQ_imm(cpu.A, Sfx_GrowVine);
    STQ_zp(cpu.A, Square2SoundQueue);
    RET();

VineObjectHandler:
    CMPQ_imm(cpu.X, 0x05);
    if (BNE()) goto ExitVH;
    LDQ_abs(cpu.Y, VineFlagOffset);
    DEY();
    LDQ_abs(cpu.A, VineHeight);
    CMPA_PRGTable(VineHeightData, cpu.Y);
    if (BEQ()) goto RunVSubs;
    LDQ_zp(cpu.A, FrameCounter);
    LSR_acc();
    LSR_acc();
    if (BCC()) goto RunVSubs;
    LDQ_zp(cpu.A, Enemy_Y_Position + 5);
    SBC_imm(0x01);
    STQ_zp(cpu.A, Enemy_Y_Position + 5);
    INC_abs(VineHeight);
RunVSubs:
    LDQ_abs(cpu.A, VineHeight);
    CMPQ_imm(cpu.A, 0x08);
    if (BCC()) goto ExitVH;
    CALL(RelativeEnemyPosition);
    CALL(GetEnemyOffscreenBits);
    LDQ_imm(cpu.Y, 0x00);
VDrawLoop:
    CALL(DrawVine);
    INY();
    CMPQ_abs(cpu.Y, VineFlagOffset);
    if (BNE()) goto VDrawLoop;
    LDQ_abs(cpu.A, Enemy_OffscreenBits);
    AND_imm(0b00001100);
    if (BEQ()) goto WrCMTile;
    DEY();
KillVine:
    LDQ_abs_xy(cpu.X, VineObjOffset, cpu.Y);
    CALL(EraseEnemyObject);
    DEY();
    if (BPL()) goto KillVine;
    STQ_abs(cpu.A, VineFlagOffset);
    STQ_abs(cpu.A, VineHeight);
WrCMTile:
    LDQ_abs(cpu.A, VineHeight);
    CMPQ_imm(cpu.A, 0x20);
    if (BCC()) goto ExitVH;
    LDQ_imm(cpu.X, 0x06);
    LDQ_imm(cpu.A, 0x01);
    LDQ_imm(cpu.Y, 0x1b);
    CALL(BlockBufferCollision);
    LDQ_zp(cpu.Y, 0x02);
    CMPQ_imm(cpu.Y, 0xd0);
    if (BCS()) goto ExitVH;
    LDA_Dyn_ind_y(0x06);
    if (BNE()) goto ExitVH;
    LDQ_imm(cpu.A, 0x26);
    STA_ind_y(0x06);
ExitVH:
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

ProcessCannons:
    LDQ_abs(cpu.A, AreaType);
    if (BEQ()) goto ExCannon;
    LDQ_imm(cpu.X, 0x02);
ThreeSChk:
    STQ_zp(cpu.X, ObjectOffset);
    LDQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    if (BNE()) goto Chk_BB;
    LDQ_abs_xy(cpu.A, PseudoRandomBitReg + 1, cpu.X);
    LDQ_abs(cpu.Y, SecondaryHardMode);
    AND_PRGTable(CannonBitmasks, cpu.Y);
    CMPQ_imm(cpu.A, 0x06);
    if (BCS()) goto Chk_BB;
    CopyAY();
    LDQ_abs_xy(cpu.A, Cannon_PageLoc, cpu.Y);
    if (BEQ()) goto Chk_BB;
    LDQ_abs_xy(cpu.A, Cannon_Timer, cpu.Y);
    if (BEQ()) goto FireCannon;
    SBC_imm(0x00);
    STA_abs_xy(Cannon_Timer, cpu.Y);
    
    goto Chk_BB;

FireCannon:
    LDQ_abs(cpu.A, TimerControl);
    if (BNE()) goto Chk_BB;
    LDQ_imm(cpu.A, 0x0e);
    STA_abs_xy(Cannon_Timer, cpu.Y);
    LDQ_abs_xy(cpu.A, Cannon_PageLoc, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    LDQ_abs_xy(cpu.A, Cannon_X_Position, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    LDQ_abs_xy(cpu.A, Cannon_Y_Position, cpu.Y);
    SEC();
    SBC_imm(0x08);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Enemy_Y_HighPos, cpu.X);
    STQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    LSR_acc();
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    LDQ_imm(cpu.A, 0x09);
    STA_abs_xy(Enemy_BoundBoxCtrl, cpu.X);
    LDQ_imm(cpu.A, BulletBill_CannonVar);
    STQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    
    goto Next3Slt;
Chk_BB:
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, BulletBill_CannonVar);
    if (BNE()) goto Next3Slt;
    CALL(OffscreenBoundsCheck);
    LDQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    if (BEQ()) goto Next3Slt;
    CALL(GetEnemyOffscreenBits);
    CALL(BulletBillHandler);
Next3Slt:
    DEX();
    if (BPL()) goto ThreeSChk;

ExCannon:
    RET();

BulletBillHandler:
    LDQ_abs(cpu.A, TimerControl);
    if (BNE()) goto RunBBSubs;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    if (BNE()) goto ChkDSte;
    LDQ_abs(cpu.A, Enemy_OffscreenBits);
    AND_imm(0b00001100);
    CMPQ_imm(cpu.A, 0b00001100);
    if (BEQ()) goto KillBB;
    LDQ_imm(cpu.Y, 0x01);
    CALL(PlayerEnemyDiff);
    if (BMI()) goto SetupBB;
    INY();
SetupBB:
    STQ_zp_xy(cpu.Y, Enemy_MovingDir, cpu.X);
    DEY();
    LDQ_PRGTable(cpu.A, BulletBillXSpdData, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
    LDQ_zp(cpu.A, 0x00);
    ADC_imm(0x28);
    CMPQ_imm(cpu.A, 0x50);
    if (BCC()) goto KillBB;
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    LDQ_imm(cpu.A, 0x0a);
    STA_abs_xy(EnemyFrameTimer, cpu.X);
    LDQ_imm(cpu.A, Sfx_Blast);
    STQ_zp(cpu.A, Square2SoundQueue);
ChkDSte:
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b00100000);
    if (BEQ()) goto BBFly;
    CALL(MoveD_EnemyVertically);
BBFly:
    CALL(MoveEnemyHorizontally);
RunBBSubs:
    CALL(GetEnemyOffscreenBits);
    CALL(RelativeEnemyPosition);
    CALL(GetEnemyBoundBox);
    CALL(PlayerEnemyCollision);
    
    goto EnemyGfxHandler;
KillBB:
    CALL(EraseEnemyObject);
    RET();

SpawnHammerObj:
    LDQ_abs(cpu.A, PseudoRandomBitReg + 1);
    AND_imm(0b00000111);
    if (BNE()) goto SetMOfs;
    LDQ_abs(cpu.A, PseudoRandomBitReg + 1);
    AND_imm(0b00001000);
SetMOfs:
    CopyAY();
    LDQ_abs_xy(cpu.A, Misc_State, cpu.Y);
    if (BNE()) goto NoHammer;
    LDQ_PRGTable(cpu.X, HammerEnemyOfsData, cpu.Y);
    LDQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    if (BNE()) goto NoHammer;
    LDQ_zp(cpu.X, ObjectOffset);
    CopyXA();
    STA_abs_xy(HammerEnemyOffset, cpu.Y);
    LDQ_imm(cpu.A, 0x90);
    STA_abs_xy(Misc_State, cpu.Y);
    LDQ_imm(cpu.A, 0x07);
    STA_abs_xy(Misc_BoundBoxCtrl, cpu.Y);
    SEC();
    RET();
NoHammer:
    LDQ_zp(cpu.X, ObjectOffset);
    CLC();
    RET();

ProcHammerObj:
    LDQ_abs(cpu.A, TimerControl);
    if (BNE()) goto RunHSubs;
    LDQ_zp_xy(cpu.A, Misc_State, cpu.X);
    AND_imm(0b01111111);
    LDQ_abs_xy(cpu.Y, HammerEnemyOffset, cpu.X);
    CMPQ_imm(cpu.A, 0x02);
    if (BEQ()) goto SetHSpd;
    if (BCS()) goto SetHPos;
    CopyXA();
    CLC();
    ADC_imm(0x0d);
    CopyAX();
    LDQ_imm(cpu.A, 0x10);
    STQ_zp(cpu.A, 0x00);
    LDQ_imm(cpu.A, 0x0f);
    STQ_zp(cpu.A, 0x01);
    LDQ_imm(cpu.A, 0x04);
    STQ_zp(cpu.A, 0x02);
    LDQ_imm(cpu.A, 0x00);
    CALL(ImposeGravity);
    CALL(MoveObjectHorizontally);
    LDQ_zp(cpu.X, ObjectOffset);
    
    goto RunAllH;
SetHSpd:
    LDQ_imm(cpu.A, 0xfe);
    STQ_zp_xy(cpu.A, Misc_Y_Speed, cpu.X);
    LDQ_abs_xy(cpu.A, Enemy_State, cpu.Y);
    AND_imm(0b11110111);
    STA_abs_xy(Enemy_State, cpu.Y);
    LDQ_zp_xy(cpu.X, Enemy_MovingDir, cpu.Y);
    DEX();
    LDQ_PRGTable(cpu.A, HammerXSpdData, cpu.X);
    LDQ_zp(cpu.X, ObjectOffset);
    STQ_zp_xy(cpu.A, Misc_X_Speed, cpu.X);
SetHPos:
    DEC_zp_x(Misc_State);
    LDQ_abs_xy(cpu.A, Enemy_X_Position, cpu.Y);
    CLC();
    ADC_imm(0x02);
    STQ_zp_xy(cpu.A, Misc_X_Position, cpu.X);
    LDQ_abs_xy(cpu.A, Enemy_PageLoc, cpu.Y);
    ADC_imm(0x00);
    STQ_zp_xy(cpu.A, Misc_PageLoc, cpu.X);
    LDQ_abs_xy(cpu.A, Enemy_Y_Position, cpu.Y);
    SEC();
    SBC_imm(0x0a);
    STQ_zp_xy(cpu.A, Misc_Y_Position, cpu.X);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Misc_Y_HighPos, cpu.X);
    if (BNE()) goto RunHSubs;

RunAllH:
    CALL(PlayerHammerCollision);
RunHSubs:
    CALL(GetMiscOffscreenBits);
    CALL(RelativeMiscPosition);
    CALL(GetMiscBoundBox);
    CALL(DrawHammer);
    RET();

CoinBlock:
    CALL(FindEmptyMiscSlot);
    LDQ_zp_xy(cpu.A, Block_PageLoc, cpu.X);
    STA_abs_xy(Misc_PageLoc, cpu.Y);
    LDQ_zp_xy(cpu.A, Block_X_Position, cpu.X);
    ORA_imm(0x05);
    STA_abs_xy(Misc_X_Position, cpu.Y);
    LDQ_zp_xy(cpu.A, Block_Y_Position, cpu.X);
    SBC_imm(0x10);
    STA_abs_xy(Misc_Y_Position, cpu.Y);
    
    goto JCoinC;

SetupJumpCoin:
    CALL(FindEmptyMiscSlot);
    LDQ_abs_xy(cpu.A, Block_PageLoc2, cpu.X);
    STA_abs_xy(Misc_PageLoc, cpu.Y);
    LDQ_zp(cpu.A, 0x06);
    ASL_acc();
    ASL_acc();
    ASL_acc();
    ASL_acc();
    ORA_imm(0x05);
    STA_abs_xy(Misc_X_Position, cpu.Y);
    LDQ_zp(cpu.A, 0x02);
    ADC_imm(0x20);
    STA_abs_xy(Misc_Y_Position, cpu.Y);
JCoinC:
    LDQ_imm(cpu.A, 0xfb);
    STA_abs_xy(Misc_Y_Speed, cpu.Y);
    LDQ_imm(cpu.A, 0x01);
    STA_abs_xy(Misc_Y_HighPos, cpu.Y);
    STA_abs_xy(Misc_State, cpu.Y);
    STQ_zp(cpu.A, Square2SoundQueue);
    STQ_zp(cpu.X, ObjectOffset);
    CALL(GiveOneCoin);
    INC_abs(CoinTallyFor1Ups);
    RET();

FindEmptyMiscSlot:
    LDQ_imm(cpu.Y, 0x08);
FMiscLoop:
    LDQ_abs_xy(cpu.A, Misc_State, cpu.Y);
    if (BEQ()) goto UseMiscS;
    DEY();
    CMPQ_imm(cpu.Y, 0x05);
    if (BNE()) goto FMiscLoop;
    LDQ_imm(cpu.Y, 0x08);
UseMiscS:
    STQ_abs(cpu.Y, JumpCoinMiscOffset);
    RET();

MiscObjectsCore:
    LDQ_imm(cpu.X, 0x08);
MiscLoop:
    STQ_zp(cpu.X, ObjectOffset);
    LDQ_zp_xy(cpu.A, Misc_State, cpu.X);
    if (BEQ()) goto MiscLoopBack;
    ASL_acc();
    if (BCC()) goto ProcJumpCoin;
    CALL(ProcHammerObj);
    
    goto MiscLoopBack;

ProcJumpCoin:
    LDQ_zp_xy(cpu.Y, Misc_State, cpu.X);
    DEY();
    if (BEQ()) goto JCoinRun;
    INC_zp_x(Misc_State);
    LDQ_zp_xy(cpu.A, Misc_X_Position, cpu.X);
    CLC();
    ADC_abs(ScrollAmount);
    STQ_zp_xy(cpu.A, Misc_X_Position, cpu.X);
    LDQ_zp_xy(cpu.A, Misc_PageLoc, cpu.X);
    ADC_imm(0x00);
    STQ_zp_xy(cpu.A, Misc_PageLoc, cpu.X);
    LDQ_zp_xy(cpu.A, Misc_State, cpu.X);
    CMPQ_imm(cpu.A, 0x30);
    if (BNE()) goto RunJCSubs;
    LDQ_imm(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, Misc_State, cpu.X);
    
    goto MiscLoopBack;
JCoinRun:
    CopyXA();
    CLC();
    ADC_imm(0x0d);
    CopyAX();
    LDQ_imm(cpu.A, 0x50);
    STQ_zp(cpu.A, 0x00);
    LDQ_imm(cpu.A, 0x06);
    STQ_zp(cpu.A, 0x02);
    LSR_acc();
    STQ_zp(cpu.A, 0x01);
    LDQ_imm(cpu.A, 0x00);
    CALL(ImposeGravity);
    LDQ_zp(cpu.X, ObjectOffset);
    LDQ_zp_xy(cpu.A, Misc_Y_Speed, cpu.X);
    CMPQ_imm(cpu.A, 0x05);
    if (BNE()) goto RunJCSubs;
    INC_zp_x(Misc_State);
RunJCSubs:
    CALL(RelativeMiscPosition);
    CALL(GetMiscOffscreenBits);
    CALL(GetMiscBoundBox);
    CALL(JCoinGfxHandler);

MiscLoopBack:
    DEX();
    if (BPL()) goto MiscLoop;
    RET();

GiveOneCoin:
    LDQ_imm(cpu.A, 0x01);
    STQ_abs(cpu.A, DigitModifier + 5);
    LDQ_abs(cpu.X, CurrentPlayer);
    LDQ_PRGTable(cpu.Y, CoinTallyOffsets, cpu.X);
    CALL(DigitsMathRoutine);
    INC_abs(CoinTally);
    LDQ_abs(cpu.A, CoinTally);
    CMPQ_imm(cpu.A, 100);
    if (BNE()) goto CoinPoints;
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, CoinTally);
    INC_abs(NumberofLives);
    LDQ_imm(cpu.A, Sfx_ExtraLife);
    STQ_zp(cpu.A, Square2SoundQueue);

CoinPoints:
    LDQ_imm(cpu.A, 0x02);
    STQ_abs(cpu.A, DigitModifier + 4);

AddToScore:
    LDQ_abs(cpu.X, CurrentPlayer);
    LDQ_PRGTable(cpu.Y, ScoreOffsets, cpu.X);
    CALL(DigitsMathRoutine);

GetSBNybbles:
    LDQ_abs(cpu.Y, CurrentPlayer);
    LDQ_PRGTable(cpu.A, StatusBarNybbles, cpu.Y);

UpdateNumber:
    CALL(PrintStatusBarNumbers);
    LDQ_abs(cpu.Y, VRAM_Buffer1_Offset);
    LDQ_abs_xy(cpu.A, VRAM_Buffer1 - 6, cpu.Y);
    if (BNE()) goto NoZSup;
    LDQ_imm(cpu.A, 0x24);
    STA_abs_xy(VRAM_Buffer1 - 6, cpu.Y);
NoZSup:
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

SetupPowerUp:
    LDQ_imm(cpu.A, PowerUpObject);
    STQ_zp(cpu.A, Enemy_ID + 5);
    LDQ_zp_xy(cpu.A, Block_PageLoc, cpu.X);
    STQ_zp(cpu.A, Enemy_PageLoc + 5);
    LDQ_zp_xy(cpu.A, Block_X_Position, cpu.X);
    STQ_zp(cpu.A, Enemy_X_Position + 5);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp(cpu.A, Enemy_Y_HighPos + 5);
    LDQ_zp_xy(cpu.A, Block_Y_Position, cpu.X);
    SEC();
    SBC_imm(0x08);
    STQ_zp(cpu.A, Enemy_Y_Position + 5);
PwrUpJmp:
    LDQ_imm(cpu.A, 0x01);
    STQ_zp(cpu.A, Enemy_State + 5);
    STQ_zp(cpu.A, Enemy_Flag + 5);
    LDQ_imm(cpu.A, 0x03);
    STQ_abs(cpu.A, Enemy_BoundBoxCtrl + 5);
    LDQ_zp(cpu.A, PowerUpType);
    CMPQ_imm(cpu.A, 0x02);
    if (BCS()) goto PutBehind;
    LDQ_abs(cpu.A, PlayerStatus);
    CMPQ_imm(cpu.A, 0x02);
    if (BCC()) goto StrType;
    LSR_acc();
StrType:
    STQ_zp(cpu.A, PowerUpType);
PutBehind:
    LDQ_imm(cpu.A, 0b00100000);
    STQ_abs(cpu.A, Enemy_SprAttrib + 5);
    LDQ_imm(cpu.A, Sfx_GrowPowerUp);
    STQ_zp(cpu.A, Square2SoundQueue);
    RET();

PowerUpObjHandler:
    LDQ_imm(cpu.X, 0x05);
    STQ_zp(cpu.X, ObjectOffset);
    LDQ_zp(cpu.A, Enemy_State + 5);
    if (BEQ()) goto ExitPUp;
    ASL_acc();
    if (BCC()) goto GrowThePowerUp;
    LDQ_abs(cpu.A, TimerControl);
    if (BNE()) goto RunPUSubs;
    LDQ_zp(cpu.A, PowerUpType);
    if (BEQ()) goto ShroomM;
    CMPQ_imm(cpu.A, 0x03);
    if (BEQ()) goto ShroomM;
    CMPQ_imm(cpu.A, 0x02);
    if (BNE()) goto RunPUSubs;
    CALL(MoveJumpingEnemy);
    CALL(EnemyJump);
    
    goto RunPUSubs;
ShroomM:
    CALL(MoveNormalEnemy);
    CALL(EnemyToBGCollisionDet);
    
    goto RunPUSubs;

GrowThePowerUp:
    LDQ_zp(cpu.A, FrameCounter);
    AND_imm(0x03);
    if (BNE()) goto ChkPUSte;
    DEC_zp(Enemy_Y_Position + 5);
    LDQ_zp(cpu.A, Enemy_State + 5);
    INC_zp(Enemy_State + 5);
    CMPQ_imm(cpu.A, 0x11);
    if (BCC()) goto ChkPUSte;
    LDQ_imm(cpu.A, 0x10);
    STQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
    LDQ_imm(cpu.A, 0b10000000);
    STQ_zp(cpu.A, Enemy_State + 5);
    ASL_acc();
    STQ_abs(cpu.A, Enemy_SprAttrib + 5);
    ROL_acc();
    STQ_zp_xy(cpu.A, Enemy_MovingDir, cpu.X);
ChkPUSte:
    LDQ_zp(cpu.A, Enemy_State + 5);
    CMPQ_imm(cpu.A, 0x06);
    if (BCC()) goto ExitPUp;

RunPUSubs:
    CALL(RelativeEnemyPosition);
    CALL(GetEnemyOffscreenBits);
    CALL(GetEnemyBoundBox);
    CALL(DrawPowerUp);
    CALL(PlayerEnemyCollision);
    CALL(OffscreenBoundsCheck);
ExitPUp:
    RET();

PlayerHeadCollision:
    PHA();
    LDQ_imm(cpu.A, 0x11);
    LDQ_abs(cpu.X, SprDataOffset_Ctrl);
    LDQ_abs(cpu.Y, PlayerSize);
    if (BNE()) goto DBlockSte;
    LDQ_imm(cpu.A, 0x12);
DBlockSte:
    STQ_zp_xy(cpu.A, Block_State, cpu.X);
    CALL(DestroyBlockMetatile);
    LDQ_abs(cpu.X, SprDataOffset_Ctrl);
    LDQ_zp(cpu.A, 0x02);
    STA_abs_xy(Block_Orig_YPos, cpu.X);
    CopyAY();
    LDQ_zp(cpu.A, 0x06);
    STA_abs_xy(Block_BBuf_Low, cpu.X);
    LDA_Dyn_ind_y(0x06);
    CALL(BlockBumpedChk);
    STQ_zp(cpu.A, 0x00);
    LDQ_abs(cpu.Y, PlayerSize);
    if (BNE()) goto ChkBrick;
    CopyYA();
ChkBrick:
    if (BCC()) goto PutMTileB;
    LDQ_imm(cpu.Y, 0x11);
    STQ_zp_xy(cpu.Y, Block_State, cpu.X);
    LDQ_imm(cpu.A, 0xc4);
    LDQ_zp(cpu.Y, 0x00);
    CMPQ_imm(cpu.Y, 0x58);
    if (BEQ()) goto StartBTmr;
    CMPQ_imm(cpu.Y, 0x5d);
    if (BNE()) goto PutMTileB;

StartBTmr:
    LDQ_abs(cpu.A, BrickCoinTimerFlag);
    if (BNE()) goto ContBTmr;
    LDQ_imm(cpu.A, 0x0b);
    STQ_abs(cpu.A, BrickCoinTimer);
    INC_abs(BrickCoinTimerFlag);
ContBTmr:
    LDQ_abs(cpu.A, BrickCoinTimer);
    if (BNE()) goto PutOldMT;
    LDQ_imm(cpu.Y, 0xc4);
PutOldMT:
    CopyYA();
PutMTileB:
    STA_abs_xy(Block_Metatile, cpu.X);
    CALL(InitBlock_XY_Pos);
    LDQ_zp(cpu.Y, 0x02);
    LDQ_imm(cpu.A, 0x23);
    STA_ind_y(0x06);
    LDQ_imm(cpu.A, 0x10);
    STQ_abs(cpu.A, BlockBounceTimer);
    PLA();
    STQ_zp(cpu.A, 0x05);
    LDQ_imm(cpu.Y, 0x00);
    LDQ_abs(cpu.A, CrouchingFlag);
    if (BNE()) goto SmallBP;
    LDQ_abs(cpu.A, PlayerSize);
    if (BEQ()) goto BigBP;

SmallBP:
    INY();
BigBP:
    LDQ_zp(cpu.A, Player_Y_Position);
    CLC();
    ADC_PRGTable(BlockYPosAdderData, cpu.Y);
    AND_imm(0xf0);
    STQ_zp_xy(cpu.A, Block_Y_Position, cpu.X);
    LDQ_zp_xy(cpu.Y, Block_State, cpu.X);
    CMPQ_imm(cpu.Y, 0x11);
    if (BEQ()) goto Unbreak;
    CALL(BrickShatter);
    
    goto InvOBit;
Unbreak:
    CALL(BumpBlock);
InvOBit:
    LDQ_abs(cpu.A, SprDataOffset_Ctrl);
    EOR_imm(0x01);
    STQ_abs(cpu.A, SprDataOffset_Ctrl);
    RET();

InitBlock_XY_Pos:
    LDQ_zp(cpu.A, Player_X_Position);
    CLC();
    ADC_imm(0x08);
    AND_imm(0xf0);
    STQ_zp_xy(cpu.A, Block_X_Position, cpu.X);
    LDQ_zp(cpu.A, Player_PageLoc);
    ADC_imm(0x00);
    STQ_zp_xy(cpu.A, Block_PageLoc, cpu.X);
    STA_abs_xy(Block_PageLoc2, cpu.X);
    LDQ_zp(cpu.A, Player_Y_HighPos);
    STQ_zp_xy(cpu.A, Block_Y_HighPos, cpu.X);
    RET();

BumpBlock:
    CALL(CheckTopOfBlock);
    LDQ_imm(cpu.A, Sfx_Bump);
    STQ_zp(cpu.A, Square1SoundQueue);
    LDQ_imm(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, Block_X_Speed, cpu.X);
    STA_abs_xy(Block_Y_MoveForce, cpu.X);
    STQ_zp(cpu.A, Player_Y_Speed);
    LDQ_imm(cpu.A, 0xfe);
    STQ_zp_xy(cpu.A, Block_Y_Speed, cpu.X);
    LDQ_zp(cpu.A, 0x05);
    CALL(BlockBumpedChk);
    if (BCC()) goto ExitBlockChk;
    CopyYA();
    CMPQ_imm(cpu.A, 0x09);
    if (BCC()) goto BlockCode;
    SBC_imm(0x05);
BlockCode:
    switch (cpu.A) {
        case 0:
        case 4: goto MushFlowerBlock;
        case 1:
        case 2:
        case 7: goto CoinBlock;
        case 3:
        case 8: goto ExtraLifeMushBlock;
        case 5: goto VineBlock;
        case 6: goto StarBlock;
    }
    __builtin_unreachable();

MushFlowerBlock:
    LDQ_imm(cpu.A, 0x00);
    bitFlag = true;

StarBlock:
    if (!bitFlag)
        LDQ_imm(cpu.A, 0x02);
    
    bitFlag = true;

ExtraLifeMushBlock:
    if (!bitFlag)
        LDQ_imm(cpu.A, 0x03);
    else
        bitFlag = false;

    STQ_zp(cpu.A, 0x39);
    
    goto SetupPowerUp;

VineBlock:
    LDQ_imm(cpu.X, 0x05);
    LDQ_abs(cpu.Y, SprDataOffset_Ctrl);
    CALL(Setup_Vine);

ExitBlockChk:
    RET();

BlockBumpedChk:
    LDQ_imm(cpu.Y, 0x0d);
BumpChkLoop:
    CMPA_PRGTable(BrickQBlockMetatiles, cpu.Y);
    if (BEQ()) goto MatchBump;
    DEY();
    if (BPL()) goto BumpChkLoop;
    CLC();
MatchBump:
    RET();

BrickShatter:
    CALL(CheckTopOfBlock);
    LDQ_imm(cpu.A, Sfx_BrickShatter);
    STA_abs_xy(Block_RepFlag, cpu.X);
    STQ_zp(cpu.A, NoiseSoundQueue);
    CALL(SpawnBrickChunks);
    LDQ_imm(cpu.A, 0xfe);
    STQ_zp(cpu.A, Player_Y_Speed);
    LDQ_imm(cpu.A, 0x05);
    STQ_abs(cpu.A, DigitModifier + 5);
    CALL(AddToScore);
    LDQ_abs(cpu.X, SprDataOffset_Ctrl);
    RET();

CheckTopOfBlock:
    LDQ_abs(cpu.X, SprDataOffset_Ctrl);
    LDQ_zp(cpu.Y, 0x02);
    if (BEQ()) goto TopEx;
    CopyYA();
    SEC();
    SBC_imm(0x10);
    STQ_zp(cpu.A, 0x02);
    CopyAY();
    LDA_Dyn_ind_y(0x06);
    CMPQ_imm(cpu.A, 0xc2);
    if (BNE()) goto TopEx;
    LDQ_imm(cpu.A, 0x00);
    STA_ind_y(0x06);
    CALL(RemoveCoin_Axe);
    LDQ_abs(cpu.X, SprDataOffset_Ctrl);
    CALL(SetupJumpCoin);
TopEx:
    RET();

SpawnBrickChunks:
    LDQ_zp_xy(cpu.A, Block_X_Position, cpu.X);
    STA_abs_xy(Block_Orig_XPos, cpu.X);
    LDQ_imm(cpu.A, 0xf0);
    STQ_zp_xy(cpu.A, Block_X_Speed, cpu.X);
    STQ_zp_xy(cpu.A, Block_X_Speed + 2, cpu.X);
    LDQ_imm(cpu.A, 0xfa);
    STQ_zp_xy(cpu.A, Block_Y_Speed, cpu.X);
    LDQ_imm(cpu.A, 0xfc);
    STQ_zp_xy(cpu.A, Block_Y_Speed + 2, cpu.X);
    LDQ_imm(cpu.A, 0x00);
    STA_abs_xy(Block_Y_MoveForce, cpu.X);
    STA_abs_xy(Block_Y_MoveForce + 2, cpu.X);
    LDQ_zp_xy(cpu.A, Block_PageLoc, cpu.X);
    STQ_zp_xy(cpu.A, Block_PageLoc + 2, cpu.X);
    LDQ_zp_xy(cpu.A, Block_X_Position, cpu.X);
    STQ_zp_xy(cpu.A, Block_X_Position + 2, cpu.X);
    LDQ_zp_xy(cpu.A, Block_Y_Position, cpu.X);
    CLC();
    ADC_imm(0x08);
    STQ_zp_xy(cpu.A, Block_Y_Position + 2, cpu.X);
    LDQ_imm(cpu.A, 0xfa);
    STQ_zp_xy(cpu.A, Block_Y_Speed, cpu.X);
    RET();

BlockObjectsCore:
    LDQ_zp_xy(cpu.A, Block_State, cpu.X);
    if (BEQ()) goto UpdSte;
    AND_imm(0x0f);
    PHA();
    CopyAY();
    CopyXA();
    CLC();
    ADC_imm(0x09);
    CopyAX();
    DEY();
    if (BEQ()) goto BouncingBlockHandler;
    CALL(ImposeGravityBlock);
    CALL(MoveObjectHorizontally);
    CopyXA();
    CLC();
    ADC_imm(0x02);
    CopyAX();
    CALL(ImposeGravityBlock);
    CALL(MoveObjectHorizontally);
    LDQ_zp(cpu.X, ObjectOffset);
    CALL(RelativeBlockPosition);
    CALL(GetBlockOffscreenBits);
    CALL(DrawBrickChunks);
    PLA();
    LDQ_zp_xy(cpu.Y, Block_Y_HighPos, cpu.X);
    if (BEQ()) goto UpdSte;
    PHA();
    LDQ_imm(cpu.A, 0xf0);
    CMPA_zp_xy(Block_Y_Position + 2, cpu.X);
    if (BCS()) goto ChkTop;
    STQ_zp_xy(cpu.A, Block_Y_Position + 2, cpu.X);
ChkTop:
    LDQ_zp_xy(cpu.A, Block_Y_Position, cpu.X);
    CMPQ_imm(cpu.A, 0xf0);
    PLA();
    if (BCC()) goto UpdSte;
    if (BCS()) goto KillBlock;

BouncingBlockHandler:
    CALL(ImposeGravityBlock);
    LDQ_zp(cpu.X, ObjectOffset);
    CALL(RelativeBlockPosition);
    CALL(GetBlockOffscreenBits);
    CALL(DrawBlock);
    LDQ_zp_xy(cpu.A, Block_Y_Position, cpu.X);
    AND_imm(0x0f);
    CMPQ_imm(cpu.A, 0x05);
    PLA();
    if (BCS()) goto UpdSte;
    LDQ_imm(cpu.A, 0x01);
    STA_abs_xy(Block_RepFlag, cpu.X);
KillBlock:
    LDQ_imm(cpu.A, 0x00);
UpdSte:
    STQ_zp_xy(cpu.A, Block_State, cpu.X);
    RET();

BlockObjMT_Updater:
    LDQ_imm(cpu.X, 0x01);
UpdateLoop:
    STQ_zp(cpu.X, ObjectOffset);
    LDQ_abs(cpu.A, VRAM_Buffer1);
    if (BNE()) goto NextBUpd;
    LDQ_abs_xy(cpu.A, Block_RepFlag, cpu.X);
    if (BEQ()) goto NextBUpd;
    LDQ_abs_xy(cpu.A, Block_BBuf_Low, cpu.X);
    STQ_zp(cpu.A, 0x06);
    LDQ_imm(cpu.A, 0x05);
    STQ_zp(cpu.A, 0x07);
    LDQ_abs_xy(cpu.A, Block_Orig_YPos, cpu.X);
    STQ_zp(cpu.A, 0x02);
    CopyAY();
    LDQ_abs_xy(cpu.A, Block_Metatile, cpu.X);
    STA_ind_y(0x06);
    CALL(ReplaceBlockMetatile);
    LDQ_imm(cpu.A, 0x00);
    STA_abs_xy(Block_RepFlag, cpu.X);
NextBUpd:
    DEX();
    if (BPL()) goto UpdateLoop;
    RET();

MoveEnemyHorizontally:
    INX();
    CALL(MoveObjectHorizontally);
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

MovePlayerHorizontally:
    LDQ_abs(cpu.A, JumpspringAnimCtrl);
    if (BNE()) goto ExXMove;
    CopyAX();

MoveObjectHorizontally:
    LDQ_zp_xy(cpu.A, SprObject_X_Speed, cpu.X);
    ASL_acc();
    ASL_acc();
    ASL_acc();
    ASL_acc();
    STQ_zp(cpu.A, 0x01);
    LDQ_zp_xy(cpu.A, SprObject_X_Speed, cpu.X);
    LSR_acc();
    LSR_acc();
    LSR_acc();
    LSR_acc();
    CMPQ_imm(cpu.A, 0x08);
    if (BCC()) goto SaveXSpd;
    ORA_imm(0b11110000);
SaveXSpd:
    STQ_zp(cpu.A, 0x00);
    LDQ_imm(cpu.Y, 0x00);
    CMPQ_imm(cpu.A, 0x00);
    if (BPL()) goto UseAdder;
    DEY();
UseAdder:
    STQ_zp(cpu.Y, 0x02);
    LDQ_abs_xy(cpu.A, SprObject_X_MoveForce, cpu.X);
    CLC();
    ADC_zp(0x01);
    STA_abs_xy(SprObject_X_MoveForce, cpu.X);
    LDQ_imm(cpu.A, 0x00);
    ROL_acc();
    PHA();
    ROR_acc();
    LDQ_zp_xy(cpu.A, SprObject_X_Position, cpu.X);
    ADC_zp(0x00);
    STQ_zp_xy(cpu.A, SprObject_X_Position, cpu.X);
    LDQ_zp_xy(cpu.A, SprObject_PageLoc, cpu.X);
    ADC_zp(0x02);
    STQ_zp_xy(cpu.A, SprObject_PageLoc, cpu.X);
    PLA();
    CLC();
    ADC_zp(0x00);
ExXMove:
    RET();

MovePlayerVertically:
    LDQ_imm(cpu.X, 0x00);
    LDQ_abs(cpu.A, TimerControl);
    if (BNE()) goto NoJSChk;
    LDQ_abs(cpu.A, JumpspringAnimCtrl);
    if (BNE()) goto ExXMove;

NoJSChk:
    LDQ_abs(cpu.A, VerticalForce);
    STQ_zp(cpu.A, 0x00);
    LDQ_imm(cpu.A, 0x04);
    
    goto ImposeGravitySprObj;

MoveD_EnemyVertically:
    LDQ_imm(cpu.Y, 0x3d);
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    CMPQ_imm(cpu.A, 0x05);
    if (BNE()) goto ContVMove;

MoveFallingPlatform:
    LDQ_imm(cpu.Y, 0x20);
ContVMove:
    
    goto SetHiMax;

MoveRedPTroopaDown:
    LDQ_imm(cpu.Y, 0x00);
    
    goto MoveRedPTroopa;

MoveRedPTroopaUp:
    LDQ_imm(cpu.Y, 0x01);

MoveRedPTroopa:
    INX();
    LDQ_imm(cpu.A, 0x03);
    STQ_zp(cpu.A, 0x00);
    LDQ_imm(cpu.A, 0x06);
    STQ_zp(cpu.A, 0x01);
    LDQ_imm(cpu.A, 0x02);
    STQ_zp(cpu.A, 0x02);
    CopyYA();
    
    goto RedPTroopaGrav;

MoveDropPlatform:
    LDQ_imm(cpu.Y, 0x7f);
    if (BNE()) goto SetMdMax;

MoveEnemySlowVert:
    LDQ_imm(cpu.Y, 0x0f);
SetMdMax:
    LDQ_imm(cpu.A, 0x02);
    if (BNE()) goto SetXMoveAmt;

MoveJ_EnemyVertically:
    LDQ_imm(cpu.Y, 0x1c);
SetHiMax:
    LDQ_imm(cpu.A, 0x03);
SetXMoveAmt:
    STQ_zp(cpu.Y, 0x00);
    INX();
    CALL(ImposeGravitySprObj);
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

ImposeGravityBlock:
    LDQ_imm(cpu.Y, 0x01);
    LDQ_imm(cpu.A, 0x50);
    STQ_zp(cpu.A, 0x00);
    LDQ_PRGTable(cpu.A, MaxSpdBlockData, cpu.Y);

ImposeGravitySprObj:
    STQ_zp(cpu.A, 0x02);
    LDQ_imm(cpu.A, 0x00);
    
    goto ImposeGravity;

MovePlatformDown:
    LDQ_imm(cpu.A, 0x00);
    bitFlag = true;

MovePlatformUp:
    if (!bitFlag)
        LDQ_imm(cpu.A, 0x01);
    else
        bitFlag = false;

    PHA();
    LDQ_zp_xy(cpu.Y, Enemy_ID, cpu.X);
    INX();
    LDQ_imm(cpu.A, 0x05);
    CMPQ_imm(cpu.Y, 0x29);
    
#ifndef REMOVE_RESIDUAL
    if (BNE()) goto SetDplSpd;
    LDQ_imm(cpu.A, 0x09);
SetDplSpd:
#endif
    STQ_zp(cpu.A, 0x00);
    LDQ_imm(cpu.A, 0x0a);
    STQ_zp(cpu.A, 0x01);
    LDQ_imm(cpu.A, 0x03);
    STQ_zp(cpu.A, 0x02);
    PLA();
    CopyAY();

RedPTroopaGrav:
    CALL(ImposeGravity);
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

ImposeGravity:
    PHA();
    LDQ_abs_xy(cpu.A, SprObject_YMF_Dummy, cpu.X);
    CLC();
    ADC_abs_x(SprObject_Y_MoveForce);
    STA_abs_xy(SprObject_YMF_Dummy, cpu.X);
    LDQ_imm(cpu.Y, 0x00);
    LDQ_zp_xy(cpu.A, SprObject_Y_Speed, cpu.X);
    if (BPL()) goto AlterYP;
    DEY();
AlterYP:
    STQ_zp(cpu.Y, 0x07);
    ADC_zp_x(SprObject_Y_Position);
    STQ_zp_xy(cpu.A, SprObject_Y_Position, cpu.X);
    LDQ_zp_xy(cpu.A, SprObject_Y_HighPos, cpu.X);
    ADC_zp(0x07);
    STQ_zp_xy(cpu.A, SprObject_Y_HighPos, cpu.X);
    LDQ_abs_xy(cpu.A, SprObject_Y_MoveForce, cpu.X);
    CLC();
    ADC_zp(0x00);
    STA_abs_xy(SprObject_Y_MoveForce, cpu.X);
    LDQ_zp_xy(cpu.A, SprObject_Y_Speed, cpu.X);
    ADC_imm(0x00);
    STQ_zp_xy(cpu.A, SprObject_Y_Speed, cpu.X);
    CMPQ_zp(cpu.A, 0x02);
    if (BMI()) goto ChkUpM;
    LDQ_abs_xy(cpu.A, SprObject_Y_MoveForce, cpu.X);
    CMPQ_imm(cpu.A, 0x80);
    if (BCC()) goto ChkUpM;
    LDQ_zp(cpu.A, 0x02);
    STQ_zp_xy(cpu.A, SprObject_Y_Speed, cpu.X);
    LDQ_imm(cpu.A, 0x00);
    STA_abs_xy(SprObject_Y_MoveForce, cpu.X);
ChkUpM:
    PLA();
    if (BEQ()) goto ExVMove;
    LDQ_zp(cpu.A, 0x02);
    EOR_imm(0b11111111);
    CopyAY();
    INY();
    STQ_zp(cpu.Y, 0x07);
    LDQ_abs_xy(cpu.A, SprObject_Y_MoveForce, cpu.X);
    SEC();
    SBC_zp(0x01);
    STA_abs_xy(SprObject_Y_MoveForce, cpu.X);
    LDQ_zp_xy(cpu.A, SprObject_Y_Speed, cpu.X);
    SBC_imm(0x00);
    STQ_zp_xy(cpu.A, SprObject_Y_Speed, cpu.X);
    CMPQ_zp(cpu.A, 0x07);

#ifndef REMOVE_RESIDUAL
    if (BPL()) goto ExVMove;
    LDQ_abs_xy(cpu.A, SprObject_Y_MoveForce, cpu.X);
    CMPQ_imm(cpu.A, 0x80);
    if (BCS()) goto ExVMove;
    LDQ_zp(cpu.A, 0x07);
    STQ_zp_xy(cpu.A, SprObject_Y_Speed, cpu.X);
    LDQ_imm(cpu.A, 0xff);
    STA_abs_xy(SprObject_Y_MoveForce, cpu.X);
#endif

ExVMove:
    RET();

EnemiesAndLoopsCore:
    LDQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    PHA();
    ASL_acc();
    if (BCS()) goto ChkBowserF;
    PLA();
    if (BEQ()) goto ChkAreaTsk;
    
    goto RunEnemyObjectsCore;
ChkAreaTsk:
    LDQ_abs(cpu.A, AreaParserTaskNum);
    AND_imm(0x07);
    CMPQ_imm(cpu.A, 0x07);
    if (BEQ()) goto ExitELCore;
    
    goto ProcLoopCommand;
ChkBowserF:
    PLA();
    AND_imm(0b00001111);
    CopyAY();
    LDQ_abs_xy(cpu.A, Enemy_Flag, cpu.Y);
    if (BNE()) goto ExitELCore;
    STQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
ExitELCore:
    RET();

ExecGameLoopback:
    LDQ_zp(cpu.A, Player_PageLoc);
    SEC();
    SBC_imm(0x04);
    STQ_zp(cpu.A, Player_PageLoc);
    LDQ_abs(cpu.A, CurrentPageLoc);
    SEC();
    SBC_imm(0x04);
    STQ_abs(cpu.A, CurrentPageLoc);
    LDQ_abs(cpu.A, ScreenLeft_PageLoc);
    SEC();
    SBC_imm(0x04);
    STQ_abs(cpu.A, ScreenLeft_PageLoc);
    LDQ_abs(cpu.A, ScreenRight_PageLoc);
    SEC();
    SBC_imm(0x04);
    STQ_abs(cpu.A, ScreenRight_PageLoc);
    LDQ_abs(cpu.A, AreaObjectPageLoc);
    SEC();
    SBC_imm(0x04);
    STQ_abs(cpu.A, AreaObjectPageLoc);
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, EnemyObjectPageSel);
    STQ_abs(cpu.A, AreaObjectPageSel);
    STQ_abs(cpu.A, EnemyDataOffset);
    STQ_abs(cpu.A, EnemyObjectPageLoc);
    LDQ_PRGTable(cpu.A, AreaDataOfsLoopback, cpu.Y);
    STQ_abs(cpu.A, AreaDataOffset);
    RET();

ProcLoopCommand:
    LDQ_abs(cpu.A, LoopCommand);
    if (BEQ()) goto ChkEnemyFrenzy;
    LDQ_abs(cpu.A, CurrentColumnPos);
    if (BNE()) goto ChkEnemyFrenzy;
    LDQ_imm(cpu.Y, 0x0b);
FindLoop:
    DEY();
    if (BMI()) goto ChkEnemyFrenzy;
    LDQ_abs(cpu.A, WorldNumber);
    CMPA_PRGTable(LoopCmdWorldNumber, cpu.Y);
    if (BNE()) goto FindLoop;
    LDQ_abs(cpu.A, CurrentPageLoc);
    CMPA_PRGTable(LoopCmdPageNumber, cpu.Y);
    if (BNE()) goto FindLoop;
    LDQ_zp(cpu.A, Player_Y_Position);
    CMPA_PRGTable(LoopCmdYPosition, cpu.Y);
    if (BNE()) goto WrongChk;
    LDQ_zp(cpu.A, Player_State);
    CMPQ_imm(cpu.A, 0x00);
    if (BNE()) goto WrongChk;
    LDQ_abs(cpu.A, WorldNumber);
    CMPQ_imm(cpu.A, World7);
    if (BNE()) goto InitMLp;
    INC_abs(MultiLoopCorrectCntr);
IncMLoop:
    INC_abs(MultiLoopPassCntr);
    LDQ_abs(cpu.A, MultiLoopPassCntr);
    CMPQ_imm(cpu.A, 0x03);
    if (BNE()) goto InitLCmd;
    LDQ_abs(cpu.A, MultiLoopCorrectCntr);
    CMPQ_imm(cpu.A, 0x03);
    if (BEQ()) goto InitMLp;
    if (BNE()) goto DoLpBack;

WrongChk:
    LDQ_abs(cpu.A, WorldNumber);
    CMPQ_imm(cpu.A, World7);
    if (BEQ()) goto IncMLoop;

DoLpBack:
    CALL(ExecGameLoopback);
    CALL(KillAllEnemies);
InitMLp:
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, MultiLoopPassCntr);
    STQ_abs(cpu.A, MultiLoopCorrectCntr);
InitLCmd:
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, LoopCommand);

ChkEnemyFrenzy:
    LDQ_abs(cpu.A, EnemyFrenzyQueue);
    if (BEQ()) goto ProcessEnemyData;
    STQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    LDQ_imm(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    STQ_abs(cpu.A, EnemyFrenzyQueue);
    
    goto InitEnemyObject;

ProcessEnemyData:
    LDQ_abs(cpu.Y, EnemyDataOffset);
    LDA_Dyn_ind_y(EnemyData);
    CMPQ_imm(cpu.A, 0xff);
    if (BNE()) goto CheckEndofBuffer;
    
    goto CheckFrenzyBuffer;

CheckEndofBuffer:
    AND_imm(0b00001111);
    CMPQ_imm(cpu.A, 0x0e);
    if (BEQ()) goto CheckRightBounds;
    CMPQ_imm(cpu.X, 0x05);
    if (BCC()) goto CheckRightBounds;
    INY();
    LDA_Dyn_ind_y(MusicData);
    AND_imm(0b00111111);
    CMPQ_imm(cpu.A, 0x2e);
    if (BEQ()) goto CheckRightBounds;
    RET();

CheckRightBounds:
    LDQ_abs(cpu.A, ScreenRight_X_Pos);
    CLC();
    ADC_imm(0x30);
    AND_imm(0b11110000);
    STQ_zp(cpu.A, 0x07);
    LDQ_abs(cpu.A, ScreenRight_PageLoc);
    ADC_imm(0x00);
    STQ_zp(cpu.A, 0x06);
    LDQ_abs(cpu.Y, EnemyDataOffset);
    INY();
    LDA_Dyn_ind_y(EnemyData);
    ASL_acc();
    if (BCC()) goto CheckPageCtrlRow;
    LDQ_abs(cpu.A, EnemyObjectPageSel);
    if (BNE()) goto CheckPageCtrlRow;
    INC_abs(EnemyObjectPageSel);
    INC_abs(EnemyObjectPageLoc);

CheckPageCtrlRow:
    DEY();
    LDA_Dyn_ind_y(EnemyData);
    AND_imm(0x0f);
    CMPQ_imm(cpu.A, 0x0f);
    if (BNE()) goto PositionEnemyObj;
    LDQ_abs(cpu.A, EnemyObjectPageSel);
    if (BNE()) goto PositionEnemyObj;
    INY();
    LDA_Dyn_ind_y(EnemyData);
    AND_imm(0b00111111);
    STQ_abs(cpu.A, EnemyObjectPageLoc);
    INC_abs(EnemyDataOffset);
    INC_abs(EnemyDataOffset);
    INC_abs(EnemyObjectPageSel);
    
    goto ProcLoopCommand;

PositionEnemyObj:
    LDQ_abs(cpu.A, EnemyObjectPageLoc);
    STQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    LDA_Dyn_ind_y(EnemyData);
    AND_imm(0b11110000);
    STQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    CMPQ_abs(cpu.A, ScreenRight_X_Pos);
    LDQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    SBC_abs(ScreenRight_PageLoc);
    if (BCS()) goto CheckRightExtBounds;
    LDA_Dyn_ind_y(EnemyData);
    AND_imm(0b00001111);
    CMPQ_imm(cpu.A, 0x0e);
    if (BEQ()) goto ParseRow0e;
    
    goto CheckThreeBytes;

CheckRightExtBounds:
    LDQ_zp(cpu.A, 0x07);
    CMPA_zp_xy(Enemy_X_Position, cpu.X);
    LDQ_zp(cpu.A, 0x06);
    SBC_zp_x(Enemy_PageLoc);
    if (BCC()) goto CheckFrenzyBuffer;
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Enemy_Y_HighPos, cpu.X);
    LDA_Dyn_ind_y(EnemyData);
    ASL_acc();
    ASL_acc();
    ASL_acc();
    ASL_acc();
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CMPQ_imm(cpu.A, 0xe0);
    if (BEQ()) goto ParseRow0e;
    INY();
    LDA_Dyn_ind_y(EnemyData);
    AND_imm(0b01000000);
    if (BEQ()) goto CheckForEnemyGroup;
    LDQ_abs(cpu.A, SecondaryHardMode);
    if (BEQ()) goto Inc2B;

CheckForEnemyGroup:
    LDA_Dyn_ind_y(EnemyData);
    AND_imm(0b00111111);
    CMPQ_imm(cpu.A, 0x37);
    if (BCC()) goto BuzzyBeetleMutate;
    CMPQ_imm(cpu.A, 0x3f);
    if (BCC()) goto DoGroup;

BuzzyBeetleMutate:
    CMPQ_imm(cpu.A, Goomba);
    if (BNE()) goto StrID;
    LDQ_abs(cpu.Y, PrimaryHardMode);
    if (BEQ()) goto StrID;
    LDQ_imm(cpu.A, BuzzyBeetle);
StrID:
    STQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    CALL(InitEnemyObject);
    LDQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    if (BNE()) goto Inc2B;
    RET();

CheckFrenzyBuffer:
    LDQ_abs(cpu.A, EnemyFrenzyBuffer);
    if (BNE()) goto StrFre;
    LDQ_abs(cpu.A, VineFlagOffset);
    CMPQ_imm(cpu.A, 0x01);
    if (BNE()) goto ExEPar;
    LDQ_imm(cpu.A, VineObject);
StrFre:
    STQ_zp_xy(cpu.A, Enemy_ID, cpu.X);

InitEnemyObject:
    LDQ_imm(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    CALL(CheckpointEnemyID);
ExEPar:
    RET();

DoGroup:
    goto HandleGroupEnemies;

ParseRow0e:
    INY();
    INY();
    LDA_Dyn_ind_y(EnemyData);
    LSR_acc();
    LSR_acc();
    LSR_acc();
    LSR_acc();
    LSR_acc();
    CMPQ_abs(cpu.A, WorldNumber);
    if (BNE()) goto NotUse;
    DEY();
    LDA_Dyn_ind_y(EnemyData);
    STQ_abs(cpu.A, AreaPointer);
    INY();
    LDA_Dyn_ind_y(EnemyData);
    AND_imm(0b00011111);
    STQ_abs(cpu.A, EntrancePage);
NotUse:
    
    goto Inc3B;

CheckThreeBytes:
    LDQ_abs(cpu.Y, EnemyDataOffset);
    LDA_Dyn_ind_y(EnemyData);
    AND_imm(0b00001111);
    CMPQ_imm(cpu.A, 0x0e);
    if (BNE()) goto Inc2B;

Inc3B:
    INC_abs(EnemyDataOffset);
Inc2B:
    INC_abs(EnemyDataOffset);
    INC_abs(EnemyDataOffset);
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, EnemyObjectPageSel);
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

CheckpointEnemyID:
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, 0x15);
    if (BCS()) goto InitEnemyRoutines;
    CopyAY();
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    ADC_imm(0x08);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    LDQ_imm(cpu.A, 0x01);
    STA_abs_xy(EnemyOffscrBitsMasked, cpu.X);
    CopyYA();

InitEnemyRoutines:
    switch (cpu.A) {
        case 0:
        case 1:
        case 2: goto InitNormalEnemy;
        case 3: goto InitRedKoopa;
        case 4:
#ifndef REMOVE_RESIDUAL
        // unused NoInitCode routines
        case 9:
        case 19:
        case 25:
        case 26:
        case 32:
        case 33:
        case 34:
        case 35:
        case 48:
        case 49:
        case 50:
        case 51:
#endif
        case 52: goto NoInitCode;
        case 5: goto InitHammerBro;
        case 6: goto InitGoomba;
        case 7: goto InitBloober;
        case 8: goto InitBulletBill;
        case 10:
        case 11: goto InitCheepCheep;
        case 12: goto InitPodoboo;
        case 13: goto InitPiranhaPlant;
        case 14: goto InitJumpGPTroopa;
        case 15: goto InitRedPTroopa;
        case 16: goto InitHorizFlySwimEnemy;
        case 17: goto InitLakitu;
        case 18:
        case 20:
        case 21:
        case 22:
        case 23: goto InitEnemyFrenzy;
        case 24: goto EndFrenzy;
        case 27:
        case 28:
        case 29:
        case 30: goto InitShortFirebar;
        case 31: goto InitLongFirebar;
        case 36: goto InitBalPlatform;
        case 37: goto InitVertPlatform;
        case 38: goto LargeLiftUp;
        case 39: goto LargeLiftDown;
        case 40:
        case 42: goto InitHoriPlatform;
        case 41: goto InitDropPlatform;
        case 43: goto PlatLiftUp;
        case 44: goto PlatLiftDown;
        case 45: goto InitBowser;
        case 46: goto PwrUpJmp;
        case 47: goto Setup_Vine;
        case 53: goto InitRetainerObj;
#ifndef REMOVE_RESIDUAL
        case 54: goto EndOfEnemyInitCode;
#endif
    }
    __builtin_unreachable();

NoInitCode:
    RET();

InitGoomba:
    CALL(InitNormalEnemy);
    
    goto SmallBBox;

InitPodoboo:
    LDQ_imm(cpu.A, 0x02);
    STQ_zp_xy(cpu.A, Enemy_Y_HighPos, cpu.X);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    LSR_acc();
    STA_abs_xy(EnemyIntervalTimer, cpu.X);
    LSR_acc();
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    
    goto SmallBBox;

InitRetainerObj:
    LDQ_imm(cpu.A, 0xb8);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    RET();

InitNormalEnemy:
    LDQ_imm(cpu.Y, 0x01);
    LDQ_abs(cpu.A, PrimaryHardMode);
    if (BNE()) goto GetESpd;
    DEY();
GetESpd:
    LDQ_PRGTable(cpu.A, NormalXSpdData, cpu.Y);
SetESpd:
    STQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
    
    goto TallBBox;

InitRedKoopa:
    CALL(InitNormalEnemy);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    RET();

InitHammerBro:
    LDQ_imm(cpu.A, 0x00);
    STA_abs_xy(HammerThrowingTimer, cpu.X);
    STQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
    LDQ_abs(cpu.Y, SecondaryHardMode);
    LDQ_PRGTable(cpu.A, HBroWalkingTimerData, cpu.Y);
    STA_abs_xy(EnemyIntervalTimer, cpu.X);
    LDQ_imm(cpu.A, 0x0b);
    
    goto SetBBox;

InitHorizFlySwimEnemy:
    LDQ_imm(cpu.A, 0x00);
    
    goto SetESpd;

InitBloober:
    LDQ_imm(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, BlooperMoveSpeed, cpu.X);
SmallBBox:
    LDQ_imm(cpu.A, 0x09);
    if (BNE()) goto SetBBox;

InitRedPTroopa:
    LDQ_imm(cpu.Y, 0x30);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    STA_abs_xy(RedPTroopaOrigXPos, cpu.X);

#ifndef REMOVE_RESIDUAL
    if (BPL()) goto GetCent;
    LDQ_imm(cpu.Y, 0xe0);
    
GetCent:
#endif
    CopyYA();
    ADC_zp_x(Enemy_Y_Position);
    STQ_zp_xy(cpu.A, RedPTroopaCenterYPos, cpu.X);
TallBBox:
    LDQ_imm(cpu.A, 0x03);
SetBBox:
    STA_abs_xy(Enemy_BoundBoxCtrl, cpu.X);
    LDQ_imm(cpu.A, 0x02);
    STQ_zp_xy(cpu.A, Enemy_MovingDir, cpu.X);
InitVStf:
    LDQ_imm(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, Enemy_Y_Speed, cpu.X);
    STA_abs_xy(Enemy_Y_MoveForce, cpu.X);
    RET();

InitBulletBill:
    LDQ_imm(cpu.A, 0x02);
    STQ_zp_xy(cpu.A, Enemy_MovingDir, cpu.X);
    LDQ_imm(cpu.A, 0x09);
    STA_abs_xy(Enemy_BoundBoxCtrl, cpu.X);
    RET();

InitCheepCheep:
    CALL(SmallBBox);
    LDQ_abs_xy(cpu.A, PseudoRandomBitReg, cpu.X);
    AND_imm(0b00010000);
    STQ_zp_xy(cpu.A, CheepCheepMoveMFlag, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    STA_abs_xy(CheepCheepOrigYPos, cpu.X);
    RET();

InitLakitu:
    LDQ_abs(cpu.A, EnemyFrenzyBuffer);
    if (BNE()) goto KillLakitu;

SetupLakitu:
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, LakituReappearTimer);
    CALL(InitHorizFlySwimEnemy);
    goto TallBBox2;

KillLakitu:
    goto EraseEnemyObject;

LakituAndSpinyHandler:
    LDQ_abs(cpu.A, FrenzyEnemyTimer);
    if (BNE()) goto ExLSHand;
    CMPQ_imm(cpu.X, 0x05);
    if (BCS()) goto ExLSHand;
    LDQ_imm(cpu.A, 0x80);
    STQ_abs(cpu.A, FrenzyEnemyTimer);
    LDQ_imm(cpu.Y, 0x04);
ChkLak:
    LDQ_abs_xy(cpu.A, Enemy_ID, cpu.Y);
    CMPQ_imm(cpu.A, Lakitu);
    if (BEQ()) goto CreateSpiny;
    DEY();
    if (BPL()) goto ChkLak;
    INC_abs(LakituReappearTimer);
    LDQ_abs(cpu.A, LakituReappearTimer);
    CMPQ_imm(cpu.A, 0x07);
    if (BCC()) goto ExLSHand;
    LDQ_imm(cpu.X, 0x04);
ChkNoEn:
    LDQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    if (BEQ()) goto CreateL;
    DEX();
    if (BPL()) goto ChkNoEn;

#ifndef REMOVE_RESIDUAL
    if (BMI()) goto RetEOfs;
#endif

CreateL:
    LDQ_imm(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    LDQ_imm(cpu.A, Lakitu);
    STQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CALL(SetupLakitu);
    LDQ_imm(cpu.A, 0x20);
    CALL(PutAtRightExtent);

#ifndef REMOVE_RESIDUAL
RetEOfs:
#endif
    LDQ_zp(cpu.X, ObjectOffset);

ExLSHand:
    RET();

CreateSpiny:
    LDQ_zp(cpu.A, Player_Y_Position);
    CMPQ_imm(cpu.A, 0x2c);
    if (BCC()) goto ExLSHand;
    LDQ_abs_xy(cpu.A, Enemy_State, cpu.Y);
    if (BNE()) goto ExLSHand;
    LDQ_abs_xy(cpu.A, Enemy_PageLoc, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    LDQ_abs_xy(cpu.A, Enemy_X_Position, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Enemy_Y_HighPos, cpu.X);
    LDQ_abs_xy(cpu.A, Enemy_Y_Position, cpu.Y);
    SEC();
    SBC_imm(0x08);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    LDQ_abs_xy(cpu.A, PseudoRandomBitReg, cpu.X);
    AND_imm(0b00000011);
    CopyAY();
    LDQ_imm(cpu.X, 0x02);
DifLoop:
    LDQ_PRGTable(cpu.A, PRDiffAdjustData, cpu.Y);
    STQ_zp_xy(cpu.A, 0x01, cpu.X);
    INY();
    INY();
    INY();
    INY();
    DEX();
    if (BPL()) goto DifLoop;
    LDQ_zp(cpu.X, ObjectOffset);
    CALL(PlayerLakituDiff);
    LDQ_zp(cpu.Y, Player_X_Speed);
    CMPQ_imm(cpu.Y, 0x08);
    if (BCS()) goto SetSpSpd;
    CopyAY();
    LDQ_abs_xy(cpu.A, PseudoRandomBitReg + 1, cpu.X);
    AND_imm(0b00000011);
    if (BEQ()) goto UsePosv;
    CopyYA();
    EOR_imm(0b11111111);
    CopyAY();
    INY();
UsePosv:
    CopyYA();
SetSpSpd:
    CALL(SmallBBox);
    LDQ_imm(cpu.Y, 0x02);
    STQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
    CMPQ_imm(cpu.A, 0x00);
    if (BMI()) goto SpinyRte;
    DEY();
SpinyRte:
    STQ_zp_xy(cpu.Y, Enemy_MovingDir, cpu.X);
    LDQ_imm(cpu.A, 0xfd);
    STQ_zp_xy(cpu.A, Enemy_Y_Speed, cpu.X);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    LDQ_imm(cpu.A, 0x05);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
ChpChpEx:
    RET();

InitLongFirebar:
    CALL(DuplicateEnemyObj);

InitShortFirebar:
    LDQ_imm(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, FirebarSpinState_Low, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    SEC();
    SBC_imm(0x1b);
    CopyAY();
    LDQ_PRGTable(cpu.A, FirebarSpinSpdData, cpu.Y);
    STA_abs_xy(FirebarSpinSpeed, cpu.X);
    LDQ_PRGTable(cpu.A, FirebarSpinDirData, cpu.Y);
    STQ_zp_xy(cpu.A, FirebarSpinDirection, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CLC();
    ADC_imm(0x04);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    CLC();
    ADC_imm(0x04);
    STQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    ADC_imm(0x00);
    STQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    
    goto TallBBox2;

InitFlyingCheepCheep:
    LDQ_abs(cpu.A, FrenzyEnemyTimer);
    if (BNE()) goto ChpChpEx;
    CALL(SmallBBox);
    LDQ_abs_xy(cpu.A, PseudoRandomBitReg + 1, cpu.X);
    AND_imm(0b00000011);
    CopyAY();
    LDQ_PRGTable(cpu.A, FlyCCTimerData, cpu.Y);
    STQ_abs(cpu.A, FrenzyEnemyTimer);
    LDQ_imm(cpu.Y, 0x03);
    LDQ_abs(cpu.A, SecondaryHardMode);
    if (BEQ()) goto MaxCC;
    INY();
MaxCC:
    STQ_zp(cpu.Y, 0x00);
    CMPQ_zp(cpu.X, 0x00);
    if (BCS()) goto ChpChpEx;
    LDQ_abs_xy(cpu.A, PseudoRandomBitReg, cpu.X);
    AND_imm(0b00000011);
    STQ_zp(cpu.A, 0x00);
    STQ_zp(cpu.A, 0x01);
    LDQ_imm(cpu.A, 0xfb);
    STQ_zp_xy(cpu.A, Enemy_Y_Speed, cpu.X);
    LDQ_imm(cpu.A, 0x00);
    LDQ_zp(cpu.Y, Player_X_Speed);
    if (BEQ()) goto GSeed;
    LDQ_imm(cpu.A, 0x04);
    CMPQ_imm(cpu.Y, 0x19);
    if (BCC()) goto GSeed;
    ASL_acc();
GSeed:
    PHA();
    CLC();
    ADC_zp(0x00);
    STQ_zp(cpu.A, 0x00);
    LDQ_abs_xy(cpu.A, PseudoRandomBitReg + 1, cpu.X);
    AND_imm(0b00000011);
    if (BEQ()) goto RSeed;
    LDQ_abs_xy(cpu.A, PseudoRandomBitReg + 2, cpu.X);
    AND_imm(0b00001111);
    STQ_zp(cpu.A, 0x00);
RSeed:
    PLA();
    CLC();
    ADC_zp(0x01);
    CopyAY();
    LDQ_PRGTable(cpu.A, FlyCCXSpeedData, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Enemy_MovingDir, cpu.X);
    LDQ_zp(cpu.A, Player_X_Speed);
    if (BNE()) goto D2XPos1;
    LDQ_zp(cpu.Y, 0x00);
    CopyYA();
    AND_imm(0b00000010);
    if (BEQ()) goto D2XPos1;
    LDQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
    EOR_imm(0xff);
    CLC();
    ADC_imm(0x01);
    STQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
    INC_zp_x(Enemy_MovingDir);
D2XPos1:
    CopyYA();
    AND_imm(0b00000010);
    if (BEQ()) goto D2XPos2;
    LDQ_zp(cpu.A, Player_X_Position);
    CLC();
    ADC_PRGTable(FlyCCXPositionData, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    LDQ_zp(cpu.A, Player_PageLoc);
    ADC_imm(0x00);
    
    goto FinCCSt;
D2XPos2:
    LDQ_zp(cpu.A, Player_X_Position);
    SEC();
    SBC_PRGTable(FlyCCXPositionData, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    LDQ_zp(cpu.A, Player_PageLoc);
    SBC_imm(0x00);
FinCCSt:
    STQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    STQ_zp_xy(cpu.A, Enemy_Y_HighPos, cpu.X);
    LDQ_imm(cpu.A, 0xf8);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    RET();

InitBowser:
    CALL(DuplicateEnemyObj);
    STQ_abs(cpu.X, BowserFront_Offset);
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, BowserBodyControls);
    STQ_abs(cpu.A, BridgeCollapseOffset);
    LDQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    STQ_abs(cpu.A, BowserOrigXPos);
    LDQ_imm(cpu.A, 0xdf);
    STQ_abs(cpu.A, BowserFireBreathTimer);
    STQ_zp_xy(cpu.A, Enemy_MovingDir, cpu.X);
    LDQ_imm(cpu.A, 0x20);
    STQ_abs(cpu.A, BowserFeetCounter);
    STA_abs_xy(EnemyFrameTimer, cpu.X);
    LDQ_imm(cpu.A, 0x05);
    STQ_abs(cpu.A, BowserHitPoints);
    LSR_acc();
    STQ_abs(cpu.A, BowserMovementSpeed);
    RET();

DuplicateEnemyObj:
    LDQ_imm(cpu.Y, 0xff);
FSLoop:
    INY();
    LDQ_abs_xy(cpu.A, Enemy_Flag, cpu.Y);
    if (BNE()) goto FSLoop;
    STQ_abs(cpu.Y, DuplicateObj_Offset);
    CopyXA();
    ORA_imm(0b10000000);
    STA_abs_xy(Enemy_Flag, cpu.Y);
    LDQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    STA_abs_xy(Enemy_PageLoc, cpu.Y);
    LDQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    STA_abs_xy(Enemy_X_Position, cpu.Y);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    STA_abs_xy(Enemy_Y_HighPos, cpu.Y);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    STA_abs_xy(Enemy_Y_Position, cpu.Y);
FlmEx:
    RET();

InitBowserFlame:
    LDQ_abs(cpu.A, FrenzyEnemyTimer);
    if (BNE()) goto FlmEx;
    STA_abs_xy(Enemy_Y_MoveForce, cpu.X);
    LDQ_zp(cpu.A, NoiseSoundQueue);
    ORA_imm(Sfx_BowserFlame);
    STQ_zp(cpu.A, NoiseSoundQueue);
    LDQ_abs(cpu.Y, BowserFront_Offset);
    LDQ_abs_xy(cpu.A, Enemy_ID, cpu.Y);
    CMPQ_imm(cpu.A, Bowser);
    if (BEQ()) goto SpawnFromMouth;
    CALL(SetFlameTimer);
    CLC();
    ADC_imm(0x20);
    LDQ_abs(cpu.Y, SecondaryHardMode);
    if (BEQ()) goto SetFrT;
    SEC();
    SBC_imm(0x10);
SetFrT:
    STQ_abs(cpu.A, FrenzyEnemyTimer);
    LDQ_abs_xy(cpu.A, PseudoRandomBitReg, cpu.X);
    AND_imm(0b00000011);
    STA_abs_xy(BowserFlamePRandomOfs, cpu.X);
    CopyAY();
    LDQ_PRGTable(cpu.A, FlameYPosData, cpu.Y);

PutAtRightExtent:
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    LDQ_abs(cpu.A, ScreenRight_X_Pos);
    CLC();
    ADC_imm(0x20);
    STQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    LDQ_abs(cpu.A, ScreenRight_PageLoc);
    ADC_imm(0x00);
    STQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    
    goto FinishFlame;

SpawnFromMouth:
    LDQ_abs_xy(cpu.A, Enemy_X_Position, cpu.Y);
    SEC();
    SBC_imm(0x0e);
    STQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    LDQ_abs_xy(cpu.A, Enemy_PageLoc, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    LDQ_abs_xy(cpu.A, Enemy_Y_Position, cpu.Y);
    CLC();
    ADC_imm(0x08);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    LDQ_abs_xy(cpu.A, PseudoRandomBitReg, cpu.X);
    AND_imm(0b00000011);
    STA_abs_xy(Enemy_YMF_Dummy, cpu.X);
    CopyAY();
    LDQ_PRGTable(cpu.A, FlameYPosData, cpu.Y);
    LDQ_imm(cpu.Y, 0x00);
    CMPA_zp_xy(Enemy_Y_Position, cpu.X);
    if (BCC()) goto SetMF;
    INY();
SetMF:
    LDQ_PRGTable(cpu.A, FlameYMFAdderData, cpu.Y);
    STA_abs_xy(Enemy_Y_MoveForce, cpu.X);
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, EnemyFrenzyBuffer);

FinishFlame:
    LDQ_imm(cpu.A, 0x08);
    STA_abs_xy(Enemy_BoundBoxCtrl, cpu.X);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Enemy_Y_HighPos, cpu.X);
    STQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    LSR_acc();
    STA_abs_xy(Enemy_X_MoveForce, cpu.X);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    RET();

InitFireworks:
    LDQ_abs(cpu.A, FrenzyEnemyTimer);
    if (BNE()) goto ExitFWk;
    LDQ_imm(cpu.A, 0x20);
    STQ_abs(cpu.A, FrenzyEnemyTimer);
    DEC_abs(FireworksCounter);
    LDQ_imm(cpu.Y, 0x06);
StarFChk:
    DEY();
    LDQ_abs_xy(cpu.A, Enemy_ID, cpu.Y);
    CMPQ_imm(cpu.A, StarFlagObject);
    if (BNE()) goto StarFChk;
    LDQ_abs_xy(cpu.A, Enemy_X_Position, cpu.Y);
    SEC();
    SBC_imm(0x30);
    PHA();
    LDQ_abs_xy(cpu.A, Enemy_PageLoc, cpu.Y);
    SBC_imm(0x00);
    STQ_zp(cpu.A, 0x00);
    LDQ_abs(cpu.A, FireworksCounter);
    CLC();
    ADC_abs_y(Enemy_State);
    CopyAY();
    PLA();
    CLC();
    ADC_PRGTable(FireworksXPosData, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    LDQ_zp(cpu.A, 0x00);
    ADC_imm(0x00);
    STQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    LDQ_PRGTable(cpu.A, FireworksYPosData, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Enemy_Y_HighPos, cpu.X);
    STQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    LSR_acc();
    STQ_zp_xy(cpu.A, ExplosionGfxCounter, cpu.X);
    LDQ_imm(cpu.A, 0x08);
    STQ_zp_xy(cpu.A, ExplosionTimerCounter, cpu.X);
ExitFWk:
    RET();

BulletBillCheepCheep:
    LDQ_abs(cpu.A, FrenzyEnemyTimer);
    if (BNE()) goto ExF17;
    LDQ_abs(cpu.A, AreaType);
    if (BNE()) goto DoBulletBills;
    CMPQ_imm(cpu.X, 0x03);
    if (BCS()) goto ExF17;
    LDQ_imm(cpu.Y, 0x00);
    LDQ_abs_xy(cpu.A, PseudoRandomBitReg, cpu.X);
    CMPQ_imm(cpu.A, 0xaa);
    if (BCC()) goto ChkW2;
    INY();
ChkW2:
    LDQ_abs(cpu.A, WorldNumber);
    CMPQ_imm(cpu.A, World2);
    if (BEQ()) goto Get17ID;
    INY();
Get17ID:
    CopyYA();
    AND_imm(0b00000001);
    CopyAY();
    LDQ_PRGTable(cpu.A, SwimCC_IDData, cpu.Y);
Set17ID:
    STQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    LDQ_abs(cpu.A, BitMFilter);
    CMPQ_imm(cpu.A, 0xff);
    if (BNE()) goto GetRBit;
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, BitMFilter);
GetRBit:
    LDQ_abs_xy(cpu.A, PseudoRandomBitReg, cpu.X);
    AND_imm(0b00000111);
ChkRBit:
    CopyAY();
    LDQ_PRGTable(cpu.A, Bitmasks, cpu.Y);
    BIT_abs(BitMFilter);
    if (BEQ()) goto AddFBit;
    INY();
    CopyYA();
    AND_imm(0b00000111);
    
    goto ChkRBit;
AddFBit:
    ORA_abs(BitMFilter);
    STQ_abs(cpu.A, BitMFilter);
    LDQ_PRGTable(cpu.A, Enemy17YPosData, cpu.Y);
    CALL(PutAtRightExtent);
    STA_abs_xy(Enemy_YMF_Dummy, cpu.X);
    LDQ_imm(cpu.A, 0x20);
    STQ_abs(cpu.A, FrenzyEnemyTimer);
    
    goto CheckpointEnemyID;

DoBulletBills:
    LDQ_imm(cpu.Y, 0xff);
BB_SLoop:
    INY();
    CMPQ_imm(cpu.Y, 0x05);
    if (BCS()) goto FireBulletBill;
    LDQ_abs_xy(cpu.A, Enemy_Flag, cpu.Y);
    if (BEQ()) goto BB_SLoop;
    LDQ_abs_xy(cpu.A, Enemy_ID, cpu.Y);
    CMPQ_imm(cpu.A, BulletBill_FrenzyVar);
    if (BNE()) goto BB_SLoop;

ExF17:
    RET();

FireBulletBill:
    LDQ_zp(cpu.A, Square2SoundQueue);
    ORA_imm(Sfx_Blast);
    STQ_zp(cpu.A, Square2SoundQueue);
    LDQ_imm(cpu.A, BulletBill_FrenzyVar);
    if (BNE()) goto Set17ID;

HandleGroupEnemies:
    LDQ_imm(cpu.Y, 0x00);
    SEC();
    SBC_imm(0x37);
    PHA();
    CMPQ_imm(cpu.A, 0x04);
    if (BCS()) goto SnglID;
    PHA();
    LDQ_imm(cpu.Y, Goomba);
    LDQ_abs(cpu.A, PrimaryHardMode);
    if (BEQ()) goto PullID;
    LDQ_imm(cpu.Y, BuzzyBeetle);
PullID:
    PLA();
SnglID:
    STQ_zp(cpu.Y, 0x01);
    LDQ_imm(cpu.Y, 0xb0);
    AND_imm(0x02);
    if (BEQ()) goto SetYGp;
    LDQ_imm(cpu.Y, 0x70);
SetYGp:
    STQ_zp(cpu.Y, 0x00);
    LDQ_abs(cpu.A, ScreenRight_PageLoc);
    STQ_zp(cpu.A, 0x02);
    LDQ_abs(cpu.A, ScreenRight_X_Pos);
    STQ_zp(cpu.A, 0x03);
    LDQ_imm(cpu.Y, 0x02);
    PLA();
    LSR_acc();
    if (BCC()) goto CntGrp;
    INY();
CntGrp:
    STQ_abs(cpu.Y, NumberofGroupEnemies);
GrLoop:
    LDQ_imm(cpu.X, 0xff);
GSltLp:
    INX();
    CMPQ_imm(cpu.X, 0x05);
    if (BCS()) goto NextED;
    LDQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    if (BNE()) goto GSltLp;
    LDQ_zp(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    LDQ_zp(cpu.A, 0x02);
    STQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    LDQ_zp(cpu.A, 0x03);
    STQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    CLC();
    ADC_imm(0x18);
    STQ_zp(cpu.A, 0x03);
    LDQ_zp(cpu.A, 0x02);
    ADC_imm(0x00);
    STQ_zp(cpu.A, 0x02);
    LDQ_zp(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Enemy_Y_HighPos, cpu.X);
    STQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    CALL(CheckpointEnemyID);
    DEC_abs(NumberofGroupEnemies);
    if (BNE()) goto GrLoop;

NextED:
    
    goto Inc2B;

InitPiranhaPlant:
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, PiranhaPlant_Y_Speed, cpu.X);
    LSR_acc();
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    STQ_zp_xy(cpu.A, PiranhaPlant_MoveFlag, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    STA_abs_xy(PiranhaPlantDownYPos, cpu.X);
    SEC();
    SBC_imm(0x18);
    STA_abs_xy(PiranhaPlantUpYPos, cpu.X);
    LDQ_imm(cpu.A, 0x09);
    
    goto SetBBox2;

InitEnemyFrenzy:
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    STQ_abs(cpu.A, EnemyFrenzyBuffer);
    SEC();
    SBC_imm(0x12);
    switch (cpu.A) {
        case 0: goto LakituAndSpinyHandler;
#ifndef REMOVE_RESIDUAL
        case 1: goto NoFrenzyCode;
#endif
        case 2: goto InitFlyingCheepCheep;
        case 3: goto InitBowserFlame;
        case 4: goto InitFireworks;
        case 5: goto BulletBillCheepCheep;
    }
    __builtin_unreachable();

#ifndef REMOVE_RESIDUAL
NoFrenzyCode:
    RET();
#endif

EndFrenzy:
    LDQ_imm(cpu.Y, 0x05);
LakituChk:
    LDQ_abs_xy(cpu.A, Enemy_ID, cpu.Y);
    CMPQ_imm(cpu.A, Lakitu);
    if (BNE()) goto NextFSlot;
    LDQ_imm(cpu.A, 0x01);
    STA_abs_xy(Enemy_State, cpu.Y);
NextFSlot:
    DEY();
    if (BPL()) goto LakituChk;
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, EnemyFrenzyBuffer);
    STQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    RET();

InitJumpGPTroopa:
    LDQ_imm(cpu.A, 0x02);
    STQ_zp_xy(cpu.A, Enemy_MovingDir, cpu.X);
    LDQ_imm(cpu.A, 0xf8);
    STQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
TallBBox2:
    LDQ_imm(cpu.A, 0x03);
SetBBox2:
    STA_abs_xy(Enemy_BoundBoxCtrl, cpu.X);
    RET();

InitBalPlatform:
    DEC_zp_x(Enemy_Y_Position);
    DEC_zp_x(Enemy_Y_Position);
    LDQ_abs(cpu.Y, SecondaryHardMode);
    if (BNE()) goto AlignP;
    LDQ_imm(cpu.Y, 0x02);
    CALL(PosPlatform);
AlignP:
    LDQ_imm(cpu.Y, 0xff);
    LDQ_abs(cpu.A, BalPlatformAlignment);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    if (BPL()) goto SetBPA;
    CopyXA();
    CopyAY();
SetBPA:
    STQ_abs(cpu.Y, BalPlatformAlignment);
    LDQ_imm(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, Enemy_MovingDir, cpu.X);
    CopyAY();
    CALL(PosPlatform);

InitDropPlatform:
    LDQ_imm(cpu.A, 0xff);
    STA_abs_xy(PlatformCollisionFlag, cpu.X);
    
    goto CommonPlatCode;

InitHoriPlatform:
    LDQ_imm(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, XMoveSecondaryCounter, cpu.X);
    
    goto CommonPlatCode;

InitVertPlatform:
    LDQ_imm(cpu.Y, 0x40);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    if (BPL()) goto SetYO;
    EOR_imm(0xff);
    CLC();
    ADC_imm(0x01);
    LDQ_imm(cpu.Y, 0xc0);
SetYO:
    STA_abs_xy(YPlatformTopYPos, cpu.X);
    CopyYA();
    CLC();
    ADC_zp_x(Enemy_Y_Position);
    STQ_zp_xy(cpu.A, YPlatformCenterYPos, cpu.X);

CommonPlatCode:
    CALL(InitVStf);
SPBBox:
    LDQ_imm(cpu.A, 0x05);
    LDQ_abs(cpu.Y, AreaType);
    CMPQ_imm(cpu.Y, 0x03);
    if (BEQ()) goto CasPBB;
    LDQ_abs(cpu.Y, SecondaryHardMode);
    if (BNE()) goto CasPBB;
    LDQ_imm(cpu.A, 0x06);
CasPBB:
    STA_abs_xy(Enemy_BoundBoxCtrl, cpu.X);
    RET();

LargeLiftUp:
    CALL(PlatLiftUp);
    
    goto LargeLiftBBox;

LargeLiftDown:
    CALL(PlatLiftDown);

LargeLiftBBox:
    goto SPBBox;

PlatLiftUp:
    LDQ_imm(cpu.A, 0x10);
    STA_abs_xy(Enemy_Y_MoveForce, cpu.X);
    LDQ_imm(cpu.A, 0xff);
    STQ_zp_xy(cpu.A, Enemy_Y_Speed, cpu.X);
    
    goto CommonSmallLift;

PlatLiftDown:
    LDQ_imm(cpu.A, 0xf0);
    STA_abs_xy(Enemy_Y_MoveForce, cpu.X);
    LDQ_imm(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, Enemy_Y_Speed, cpu.X);

CommonSmallLift:
    LDQ_imm(cpu.Y, 0x01);
    CALL(PosPlatform);
    LDQ_imm(cpu.A, 0x04);
    STA_abs_xy(Enemy_BoundBoxCtrl, cpu.X);
    RET();

PosPlatform:
    LDQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    CLC();
    ADC_PRGTable(PlatPosDataLow, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    ADC_PRGTable(PlatPosDataHigh, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    RET();

#ifndef REMOVE_RESIDUAL
EndOfEnemyInitCode:
    RET();
#endif

RunEnemyObjectsCore:
    LDQ_zp(cpu.X, ObjectOffset);
    LDQ_imm(cpu.A, 0x00);
    LDQ_zp_xy(cpu.Y, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.Y, 0x15);
    if (BCC()) goto JmpEO;
    CopyYA();
    SBC_imm(0x14);
JmpEO:
    switch (cpu.A) {
        case 0: goto RunNormalEnemies;
        case 1: goto RunBowserFlame;
        case 2: goto RunFireworks;
        case 3:
#ifndef REMOVE_RESIDUAL
        case 4:
        case 5:
        case 6:
        case 15:
#endif
        case 28:
        case 31: goto NoRunCode;
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
#ifndef REMOVE_RESIDUAL
        case 12:
        case 13:
        case 14:
#endif
            goto RunFirebarObj;
        
        case 16:
        case 17:
        case 18:
        case 19:
        case 20:
        case 21:
        case 22: goto RunLargePlatform;
        case 23:
        case 24: goto RunSmallPlatform;
        case 25: goto RunBowser;
        case 26: goto PowerUpObjHandler;
        case 27: goto VineObjectHandler;
        case 29: goto RunStarFlagObj;
        case 30: goto JumpspringHandler;
        case 32: goto WarpZoneObject;
        case 33: goto RunRetainerObj;
    }
    __builtin_unreachable();

NoRunCode:
    RET();

RunRetainerObj:
    CALL(GetEnemyOffscreenBits);
    CALL(RelativeEnemyPosition);
    
    goto EnemyGfxHandler;

RunNormalEnemies:
    LDQ_imm(cpu.A, 0x00);
    STA_abs_xy(Enemy_SprAttrib, cpu.X);
    CALL(GetEnemyOffscreenBits);
    CALL(RelativeEnemyPosition);
    CALL(EnemyGfxHandler);
    CALL(GetEnemyBoundBox);
    CALL(EnemyToBGCollisionDet);
    CALL(EnemiesCollision);
    CALL(PlayerEnemyCollision);
    LDQ_abs(cpu.Y, TimerControl);
    if (BNE()) goto SkipMove;
    CALL(EnemyMovementSubs);

SkipMove:
    goto OffscreenBoundsCheck;

EnemyMovementSubs:
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    switch (cpu.A) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 6:
        case 18: goto MoveNormalEnemy;
        case 5: goto ProcHammerBro;
        case 7: goto MoveBloober;
        case 8: goto MoveBulletBill;
#ifndef REMOVE_RESIDUAL
        case 9:
        case 19: goto NoMoveCode;
#endif
        case 10:
        case 11: goto MoveSwimmingCheepCheep;
        case 12: goto MovePodoboo;
        case 13: goto MovePiranhaPlant;
        case 14: goto MoveJumpingEnemy;
        case 15: goto ProcMoveRedPTroopa;
        case 16: goto MoveFlyGreenPTroopa;
        case 17: goto MoveLakitu;
        case 20: goto MoveFlyingCheepCheep;
    }
    __builtin_unreachable();

#ifndef REMOVE_RESIDUAL
NoMoveCode:
    RET();
#endif

RunBowserFlame:
    CALL(ProcBowserFlame);
    CALL(GetEnemyOffscreenBits);
    CALL(RelativeEnemyPosition);
    CALL(GetEnemyBoundBox);
    CALL(PlayerEnemyCollision);
    
    goto OffscreenBoundsCheck;

RunFirebarObj:
    CALL(ProcFirebar);
    
    goto OffscreenBoundsCheck;

RunSmallPlatform:
    CALL(GetEnemyOffscreenBits);
    CALL(RelativeEnemyPosition);
    CALL(SmallPlatformBoundBox);
    CALL(SmallPlatformCollision);
    CALL(RelativeEnemyPosition);
    CALL(DrawSmallPlatform);
    CALL(MoveSmallPlatform);
    
    goto OffscreenBoundsCheck;

RunLargePlatform:
    CALL(GetEnemyOffscreenBits);
    CALL(RelativeEnemyPosition);
    CALL(LargePlatformBoundBox);
    CALL(LargePlatformCollision);
    LDQ_abs(cpu.A, TimerControl);
    if (BNE()) goto SkipPT;
    CALL(LargePlatformSubroutines);
SkipPT:
    CALL(RelativeEnemyPosition);
    CALL(DrawLargePlatform);
    
    goto OffscreenBoundsCheck;

LargePlatformSubroutines:
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    SEC();
    SBC_imm(0x24);
    switch (cpu.A) {
        case 0: goto BalancePlatform;
        case 1: goto YMovingPlatform;
        case 2:
        case 3: goto MoveLargeLiftPlat;
        case 4: goto XMovingPlatform;
        case 5: goto DropPlatform;
        case 6: goto RightPlatform;
    }
    __builtin_unreachable();

EraseEnemyObject:
    LDQ_imm(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    STQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    STA_abs_xy(FloateyNum_Control, cpu.X);
    STA_abs_xy(EnemyIntervalTimer, cpu.X);
    STA_abs_xy(ShellChainCounter, cpu.X);
    STA_abs_xy(Enemy_SprAttrib, cpu.X);
    STA_abs_xy(EnemyFrameTimer, cpu.X);
    RET();

MovePodoboo:
    LDQ_abs_xy(cpu.A, EnemyIntervalTimer, cpu.X);
    if (BNE()) goto PdbM;
    CALL(InitPodoboo);
    LDQ_abs_xy(cpu.A, PseudoRandomBitReg + 1, cpu.X);
    ORA_imm(0b10000000);
    STA_abs_xy(Enemy_Y_MoveForce, cpu.X);
    AND_imm(0b00001111);
    ORA_imm(0x06);
    STA_abs_xy(EnemyIntervalTimer, cpu.X);
    LDQ_imm(cpu.A, 0xf9);
    STQ_zp_xy(cpu.A, Enemy_Y_Speed, cpu.X);

PdbM:
    goto MoveJ_EnemyVertically;

ProcHammerBro:
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b00100000);
    if (BEQ()) goto ChkJH;
    
    goto MoveDefeatedEnemy;
ChkJH:
    LDQ_zp_xy(cpu.A, HammerBroJumpTimer, cpu.X);
    if (BEQ()) goto HammerBroJumpCode;
    DEC_zp_x(HammerBroJumpTimer);
    LDQ_abs(cpu.A, Enemy_OffscreenBits);
    AND_imm(0b00001100);
    if (BNE()) goto MoveHammerBroXDir;
    LDQ_abs_xy(cpu.A, HammerThrowingTimer, cpu.X);
    if (BNE()) goto DecHT;
    LDQ_abs(cpu.Y, SecondaryHardMode);
    LDQ_PRGTable(cpu.A, HammerThrowTmrData, cpu.Y);
    STA_abs_xy(HammerThrowingTimer, cpu.X);
    CALL(SpawnHammerObj);
    if (BCC()) goto DecHT;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    ORA_imm(0b00001000);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    
    goto MoveHammerBroXDir;
DecHT:
    DEC_abs_x(HammerThrowingTimer);
    
    goto MoveHammerBroXDir;

HammerBroJumpCode:
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b00000111);
    CMPQ_imm(cpu.A, 0x01);
    if (BEQ()) goto MoveHammerBroXDir;
    LDQ_imm(cpu.A, 0x00);
    STQ_zp(cpu.A, 0x00);
    LDQ_imm(cpu.Y, 0xfa);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    if (BMI()) goto SetHJ;
    LDQ_imm(cpu.Y, 0xfd);
    CMPQ_imm(cpu.A, 0x70);
    INC_zp(0x00);
    if (BCC()) goto SetHJ;
    DEC_zp(0x00);
    LDQ_abs_xy(cpu.A, PseudoRandomBitReg + 1, cpu.X);
    AND_imm(0x01);
    if (BNE()) goto SetHJ;
    LDQ_imm(cpu.Y, 0xfa);
SetHJ:
    STQ_zp_xy(cpu.Y, Enemy_Y_Speed, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    ORA_imm(0x01);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    LDQ_zp(cpu.A, 0x00);
    AND_abs_xy(PseudoRandomBitReg + 2, cpu.X);
    CopyAY();
    LDQ_abs(cpu.A, SecondaryHardMode);
    if (BNE()) goto HJump;
    CopyAY();
HJump:
    LDQ_PRGTable(cpu.A, HammerBroJumpLData, cpu.Y);
    STA_abs_xy(EnemyFrameTimer, cpu.X);
    LDQ_abs_xy(cpu.A, PseudoRandomBitReg + 1, cpu.X);
    ORA_imm(0b11000000);
    STQ_zp_xy(cpu.A, HammerBroJumpTimer, cpu.X);

MoveHammerBroXDir:
    LDQ_imm(cpu.Y, 0xfc);
    LDQ_zp(cpu.A, FrameCounter);
    AND_imm(0b01000000);
    if (BNE()) goto Shimmy;
    LDQ_imm(cpu.Y, 0x04);
Shimmy:
    STQ_zp_xy(cpu.Y, Enemy_X_Speed, cpu.X);
    LDQ_imm(cpu.Y, 0x01);
    CALL(PlayerEnemyDiff);
    if (BMI()) goto SetShim;
    INY();
    LDQ_abs_xy(cpu.A, EnemyIntervalTimer, cpu.X);
    if (BNE()) goto SetShim;
    LDQ_imm(cpu.A, 0xf8);
    STQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
SetShim:
    STQ_zp_xy(cpu.Y, Enemy_MovingDir, cpu.X);

MoveNormalEnemy:
    LDQ_imm(cpu.Y, 0x00);
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b01000000);
    if (BNE()) goto FallE;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    ASL_acc();
    if (BCS()) goto SteadM;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b00100000);
    if (BNE()) goto MoveDefeatedEnemy;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b00000111);
    if (BEQ()) goto SteadM;
    CMPQ_imm(cpu.A, 0x05);
    if (BEQ()) goto FallE;
    CMPQ_imm(cpu.A, 0x03);
    if (BCS()) goto ReviveStunned;

FallE:
    CALL(MoveD_EnemyVertically);
    LDQ_imm(cpu.Y, 0x00);
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    CMPQ_imm(cpu.A, 0x02);
    if (BEQ()) goto MEHor;
    AND_imm(0b01000000);
    if (BEQ()) goto SteadM;
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, PowerUpObject);
    if (BEQ()) goto SteadM;
    if (BNE()) goto SlowM;

MEHor:
    
    goto MoveEnemyHorizontally;

SlowM:
    LDQ_imm(cpu.Y, 0x01);
SteadM:
    LDQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
    PHA();
    if (BPL()) goto AddHS;
    INY();
    INY();
AddHS:
    CLC();
    ADC_PRGTable(XSpeedAdderData, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
    CALL(MoveEnemyHorizontally);
    PLA();
    STQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
    RET();

ReviveStunned:
    LDQ_abs_xy(cpu.A, EnemyIntervalTimer, cpu.X);
    if (BNE()) goto ChkKillGoomba;
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    LDQ_zp(cpu.A, FrameCounter);
    AND_imm(0x01);
    CopyAY();
    INY();
    STQ_zp_xy(cpu.Y, Enemy_MovingDir, cpu.X);
    DEY();
    LDQ_abs(cpu.A, PrimaryHardMode);
    if (BEQ()) goto SetRSpd;
    INY();
    INY();
SetRSpd:
    LDQ_PRGTable(cpu.A, RevivedXSpeed, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
    RET();

MoveDefeatedEnemy:
    CALL(MoveD_EnemyVertically);
    
    goto MoveEnemyHorizontally;

ChkKillGoomba:
    CMPQ_imm(cpu.A, 0x0e);
    if (BNE()) goto NKGmba;
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, Goomba);
    if (BNE()) goto NKGmba;
    CALL(EraseEnemyObject);
NKGmba:
    RET();

MoveJumpingEnemy:
    CALL(MoveJ_EnemyVertically);
    
    goto MoveEnemyHorizontally;

ProcMoveRedPTroopa:
    LDQ_zp_xy(cpu.A, Enemy_Y_Speed, cpu.X);
    ORA_abs_xy(Enemy_Y_MoveForce, cpu.X);
    if (BNE()) goto MoveRedPTUpOrDown;
    STA_abs_xy(Enemy_YMF_Dummy, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CMPA_abs_xy(RedPTroopaOrigXPos, cpu.X);
    if (BCS()) goto MoveRedPTUpOrDown;
    LDQ_zp(cpu.A, FrameCounter);
    AND_imm(0b00000111);
    if (BNE()) goto NoIncPT;
    INC_zp_x(Enemy_Y_Position);
NoIncPT:
    RET();

MoveRedPTUpOrDown:
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CMPA_zp_xy(RedPTroopaCenterYPos, cpu.X);
    if (BCC()) goto MovPTDwn;
    
    goto MoveRedPTroopaUp;
MovPTDwn:
    
    goto MoveRedPTroopaDown;

MoveFlyGreenPTroopa:
    CALL(XMoveCntr_GreenPTroopa);
    CALL(MoveWithXMCntrs);
    LDQ_imm(cpu.Y, 0x01);
    LDQ_zp(cpu.A, FrameCounter);
    AND_imm(0b00000011);
    if (BNE()) goto NoMGPT;
    LDQ_zp(cpu.A, FrameCounter);
    AND_imm(0b01000000);
    if (BNE()) goto YSway;
    LDQ_imm(cpu.Y, 0xff);
YSway:
    STQ_zp(cpu.Y, 0x00);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CLC();
    ADC_zp(0x00);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
NoMGPT:
    RET();

XMoveCntr_GreenPTroopa:
    LDQ_imm(cpu.A, 0x13);

XMoveCntr_Platform:
    STQ_zp(cpu.A, 0x01);
    LDQ_zp(cpu.A, FrameCounter);
    AND_imm(0b00000011);
    if (BNE()) goto NoIncXM;
    LDQ_zp_xy(cpu.Y, XMoveSecondaryCounter, cpu.X);
    LDQ_zp_xy(cpu.A, XMovePrimaryCounter, cpu.X);
    LSR_acc();
    if (BCS()) goto DecSeXM;
    CMPQ_zp(cpu.Y, 0x01);
    if (BEQ()) goto IncPXM;
    INC_zp_x(XMoveSecondaryCounter);
NoIncXM:
    RET();
IncPXM:
    INC_zp_x(XMovePrimaryCounter);
    RET();
DecSeXM:
    CopyYA();
    if (BEQ()) goto IncPXM;
    DEC_zp_x(XMoveSecondaryCounter);
    RET();

MoveWithXMCntrs:
    LDQ_zp_xy(cpu.A, XMoveSecondaryCounter, cpu.X);
    PHA();
    LDQ_imm(cpu.Y, 0x01);
    LDQ_zp_xy(cpu.A, XMovePrimaryCounter, cpu.X);
    AND_imm(0b00000010);
    if (BNE()) goto XMRight;
    LDQ_zp_xy(cpu.A, XMoveSecondaryCounter, cpu.X);
    EOR_imm(0xff);
    CLC();
    ADC_imm(0x01);
    STQ_zp_xy(cpu.A, XMoveSecondaryCounter, cpu.X);
    LDQ_imm(cpu.Y, 0x02);
XMRight:
    STQ_zp_xy(cpu.Y, Enemy_MovingDir, cpu.X);
    CALL(MoveEnemyHorizontally);
    STQ_zp(cpu.A, 0x00);
    PLA();
    STQ_zp_xy(cpu.A, XMoveSecondaryCounter, cpu.X);
    RET();

MoveBloober:
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b00100000);
    if (BNE()) goto MoveDefeatedBloober;
    LDQ_abs(cpu.Y, SecondaryHardMode);
    LDQ_abs_xy(cpu.A, PseudoRandomBitReg + 1, cpu.X);
    AND_PRGTable(BlooberBitmasks, cpu.Y);
    if (BNE()) goto BlooberSwim;
    CopyXA();
    LSR_acc();
    if (BCC()) goto FBLeft;
    LDQ_zp(cpu.Y, Player_MovingDir);
    if (BCS()) goto SBMDir;

FBLeft:
    LDQ_imm(cpu.Y, 0x02);
    CALL(PlayerEnemyDiff);
    if (BPL()) goto SBMDir;
    DEY();
SBMDir:
    STQ_zp_xy(cpu.Y, Enemy_MovingDir, cpu.X);

BlooberSwim:
    CALL(ProcSwimmingB);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    SEC();
    SBC_abs_x(Enemy_Y_MoveForce);
    CMPQ_imm(cpu.A, 0x20);
    if (BCC()) goto SwimX;
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
SwimX:
    LDQ_zp_xy(cpu.Y, Enemy_MovingDir, cpu.X);
    DEY();
    if (BNE()) goto LeftSwim;
    LDQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    CLC();
    ADC_zp_x(BlooperMoveSpeed);
    STQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    ADC_imm(0x00);
    STQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    RET();

LeftSwim:
    LDQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    SEC();
    SBC_zp_x(BlooperMoveSpeed);
    STQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    SBC_imm(0x00);
    STQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    RET();

MoveDefeatedBloober:
    goto MoveEnemySlowVert;

ProcSwimmingB:
    LDQ_zp_xy(cpu.A, BlooperMoveCounter, cpu.X);
    AND_imm(0b00000010);
    if (BNE()) goto ChkForFloatdown;
    LDQ_zp(cpu.A, FrameCounter);
    AND_imm(0b00000111);
    PHA();
    LDQ_zp_xy(cpu.A, BlooperMoveCounter, cpu.X);
    LSR_acc();
    if (BCS()) goto SlowSwim;
    PLA();
    if (BNE()) goto BSwimE;
    LDQ_abs_xy(cpu.A, Enemy_Y_MoveForce, cpu.X);
    CLC();
    ADC_imm(0x01);
    STA_abs_xy(Enemy_Y_MoveForce, cpu.X);
    STQ_zp_xy(cpu.A, BlooperMoveSpeed, cpu.X);
    CMPQ_imm(cpu.A, 0x02);
    if (BNE()) goto BSwimE;
    INC_zp_x(BlooperMoveCounter);
BSwimE:
    RET();

SlowSwim:
    PLA();
    if (BNE()) goto NoSSw;
    LDQ_abs_xy(cpu.A, Enemy_Y_MoveForce, cpu.X);
    SEC();
    SBC_imm(0x01);
    STA_abs_xy(Enemy_Y_MoveForce, cpu.X);
    STQ_zp_xy(cpu.A, BlooperMoveSpeed, cpu.X);
    if (BNE()) goto NoSSw;
    INC_zp_x(BlooperMoveCounter);
    LDQ_imm(cpu.A, 0x02);
    STA_abs_xy(EnemyIntervalTimer, cpu.X);
NoSSw:
    RET();

ChkForFloatdown:
    LDQ_abs_xy(cpu.A, EnemyIntervalTimer, cpu.X);
    if (BEQ()) goto ChkNearPlayer;

Floatdown:
    LDQ_zp(cpu.A, FrameCounter);
    LSR_acc();
    if (BCS()) goto NoFD;
    INC_zp_x(Enemy_Y_Position);
NoFD:
    RET();

ChkNearPlayer:
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    ADC_imm(0x10);
    CMPQ_zp(cpu.A, Player_Y_Position);
    if (BCC()) goto Floatdown;
    LDQ_imm(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, BlooperMoveCounter, cpu.X);
    RET();

MoveBulletBill:
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b00100000);
    if (BEQ()) goto NotDefB;
    
    goto MoveJ_EnemyVertically;
NotDefB:
    LDQ_imm(cpu.A, 0xe8);
    STQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
    
    goto MoveEnemyHorizontally;

MoveSwimmingCheepCheep:
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b00100000);
    if (BEQ()) goto CCSwim;
    
    goto MoveEnemySlowVert;
CCSwim:
    STQ_zp(cpu.A, 0x03);
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    SEC();
    SBC_imm(0x0a);
    CopyAY();
    LDQ_PRGTable(cpu.A, SwimCCXMoveData, cpu.Y);
    STQ_zp(cpu.A, 0x02);
    LDQ_abs_xy(cpu.A, Enemy_X_MoveForce, cpu.X);
    SEC();
    SBC_zp(0x02);
    STA_abs_xy(Enemy_X_MoveForce, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    SBC_imm(0x00);
    STQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    SBC_imm(0x00);
    STQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    LDQ_imm(cpu.A, 0x20);
    STQ_zp(cpu.A, 0x02);
    CMPQ_imm(cpu.X, 0x02);
    if (BCC()) goto ExSwCC;
    LDQ_zp_xy(cpu.A, CheepCheepMoveMFlag, cpu.X);
    CMPQ_imm(cpu.A, 0x10);
    if (BCC()) goto CCSwimUpwards;
    LDQ_abs_xy(cpu.A, Enemy_YMF_Dummy, cpu.X);
    CLC();
    ADC_zp(0x02);
    STA_abs_xy(Enemy_YMF_Dummy, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    ADC_zp(0x03);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_Y_HighPos, cpu.X);
    ADC_imm(0x00);
    
    goto ChkSwimYPos;

CCSwimUpwards:
    LDQ_abs_xy(cpu.A, Enemy_YMF_Dummy, cpu.X);
    SEC();
    SBC_zp(0x02);
    STA_abs_xy(Enemy_YMF_Dummy, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    SBC_zp(0x03);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_Y_HighPos, cpu.X);
    SBC_imm(0x00);

ChkSwimYPos:
    STQ_zp_xy(cpu.A, Enemy_Y_HighPos, cpu.X);
    LDQ_imm(cpu.Y, 0x00);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    SEC();
    SBC_abs_x(CheepCheepOrigYPos);
    if (BPL()) goto YPDiff;
    LDQ_imm(cpu.Y, 0x10);
    EOR_imm(0xff);
    CLC();
    ADC_imm(0x01);
YPDiff:
    CMPQ_imm(cpu.A, 0x0f);
    if (BCC()) goto ExSwCC;
    CopyYA();
    STQ_zp_xy(cpu.A, CheepCheepMoveMFlag, cpu.X);
ExSwCC:
    RET();

ProcFirebar:
    CALL(GetEnemyOffscreenBits);
    LDQ_abs(cpu.A, Enemy_OffscreenBits);
    AND_imm(0b00001000);
    if (BNE()) goto SkipFBar;
    LDQ_abs(cpu.A, TimerControl);
    if (BNE()) goto SusFbar;
    LDQ_abs_xy(cpu.A, FirebarSpinSpeed, cpu.X);
    CALL(FirebarSpin);
    AND_imm(0b00011111);
    STQ_zp_xy(cpu.A, FirebarSpinState_High, cpu.X);
SusFbar:
    LDQ_zp_xy(cpu.A, FirebarSpinState_High, cpu.X);
    LDQ_zp_xy(cpu.Y, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.Y, 0x1f);
    if (BCC()) goto SetupGFB;
    CMPQ_imm(cpu.A, 0x08);
    if (BEQ()) goto SkpFSte;
    CMPQ_imm(cpu.A, 0x18);
    if (BNE()) goto SetupGFB;

SkpFSte:
    CLC();
    ADC_imm(0x01);
    STQ_zp_xy(cpu.A, FirebarSpinState_High, cpu.X);
SetupGFB:
    STQ_zp(cpu.A, 0xef);
    CALL(RelativeEnemyPosition);
    CALL(GetFirebarPosition);
    LDQ_abs_xy(cpu.Y, Enemy_SprDataOffset, cpu.X);
    LDQ_abs(cpu.A, Enemy_Rel_YPos);
    STA_abs_xy(Sprite_Y_Position, cpu.Y);
    STQ_zp(cpu.A, 0x07);
    LDQ_abs(cpu.A, Enemy_Rel_XPos);
    STA_abs_xy(Sprite_X_Position, cpu.Y);
    STQ_zp(cpu.A, 0x06);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp(cpu.A, 0x00);
    CALL(FirebarCollision);
    LDQ_imm(cpu.Y, 0x05);
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, 0x1f);
    if (BCC()) goto SetMFbar;
    LDQ_imm(cpu.Y, 0x0b);
SetMFbar:
    STQ_zp(cpu.Y, 0xed);
    LDQ_imm(cpu.A, 0x00);
    STQ_zp(cpu.A, 0x00);
DrawFbar:
    LDQ_zp(cpu.A, 0xef);
    CALL(GetFirebarPosition);
    CALL(DrawFirebar_Collision);
    LDQ_zp(cpu.A, 0x00);
    CMPQ_imm(cpu.A, 0x04);
    if (BNE()) goto NextFbar;
    LDQ_abs(cpu.Y, DuplicateObj_Offset);
    LDQ_abs_xy(cpu.A, Enemy_SprDataOffset, cpu.Y);
    STQ_zp(cpu.A, 0x06);
NextFbar:
    INC_zp(0x00);
    LDQ_zp(cpu.A, 0x00);
    CMPQ_zp(cpu.A, 0xed);
    if (BCC()) goto DrawFbar;

SkipFBar:
    RET();

DrawFirebar_Collision:
    LDQ_zp(cpu.A, 0x03);
    STQ_zp(cpu.A, 0x05);
    LDQ_zp(cpu.Y, 0x06);
    LDQ_zp(cpu.A, 0x01);
    LSR_zp(0x05);
    if (BCS()) goto AddHA;
    EOR_imm(0xff);
    ADC_imm(0x01);
AddHA:
    CLC();
    ADC_abs(Enemy_Rel_XPos);
    STA_abs_xy(Sprite_X_Position, cpu.Y);
    STQ_zp(cpu.A, 0x06);
    CMPQ_abs(cpu.A, Enemy_Rel_XPos);
    if (BCS()) goto SubtR1;
    LDQ_abs(cpu.A, Enemy_Rel_XPos);
    SEC();
    SBC_zp(0x06);
    
    goto ChkFOfs;
SubtR1:
    SEC();
    SBC_abs(Enemy_Rel_XPos);
ChkFOfs:
    CMPQ_imm(cpu.A, 0x59);
    if (BCC()) goto VAHandl;
    LDQ_imm(cpu.A, 0xf8);
    if (BNE()) goto SetVFbr;

VAHandl:
    LDQ_abs(cpu.A, Enemy_Rel_YPos);
    CMPQ_imm(cpu.A, 0xf8);
    if (BEQ()) goto SetVFbr;
    LDQ_zp(cpu.A, 0x02);
    LSR_zp(0x05);
    if (BCS()) goto AddVA;
    EOR_imm(0xff);
    ADC_imm(0x01);
AddVA:
    CLC();
    ADC_abs(Enemy_Rel_YPos);
SetVFbr:
    STA_abs_xy(Sprite_Y_Position, cpu.Y);
    STQ_zp(cpu.A, 0x07);

FirebarCollision:
    CALL(DrawFirebar);
    CopyYA();
    PHA();
    LDQ_abs(cpu.A, StarInvincibleTimer);
    ORA_abs(TimerControl);
    if (BNE()) goto NoColFB;
    STQ_zp(cpu.A, 0x05);
    LDQ_zp(cpu.Y, Player_Y_HighPos);
    DEY();
    if (BNE()) goto NoColFB;
    LDQ_zp(cpu.Y, Player_Y_Position);
    LDQ_abs(cpu.A, PlayerSize);
    if (BNE()) goto AdjSm;
    LDQ_abs(cpu.A, CrouchingFlag);
    if (BEQ()) goto BigJp;

AdjSm:
    INC_zp(0x05);
    INC_zp(0x05);
    CopyYA();
    CLC();
    ADC_imm(0x18);
    CopyAY();
BigJp:
    CopyYA();
FBCLoop:
    SEC();
    SBC_zp(0x07);
    if (BPL()) goto ChkVFBD;
    EOR_imm(0xff);
    CLC();
    ADC_imm(0x01);
ChkVFBD:
    CMPQ_imm(cpu.A, 0x08);
    if (BCS()) goto Chk2Ofs;
    LDQ_zp(cpu.A, 0x06);
    CMPQ_imm(cpu.A, 0xf0);
    if (BCS()) goto Chk2Ofs;
    LDQ_abs(cpu.A, Sprite_X_Position + 4);
    CLC();
    ADC_imm(0x04);
    STQ_zp(cpu.A, 0x04);
    SEC();
    SBC_zp(0x06);
    if (BPL()) goto ChkFBCl;
    EOR_imm(0xff);
    CLC();
    ADC_imm(0x01);
ChkFBCl:
    CMPQ_imm(cpu.A, 0x08);
    if (BCC()) goto ChgSDir;

Chk2Ofs:
    LDQ_zp(cpu.A, 0x05);
    CMPQ_imm(cpu.A, 0x02);
    if (BEQ()) goto NoColFB;
    LDQ_zp(cpu.Y, 0x05);
    LDQ_zp(cpu.A, Player_Y_Position);
    CLC();
    ADC_PRGTable(FirebarYPos, cpu.Y);
    INC_zp(0x05);
    
    goto FBCLoop;
ChgSDir:
    LDQ_imm(cpu.X, 0x01);
    LDQ_zp(cpu.A, 0x04);
    CMPQ_zp(cpu.A, 0x06);
    if (BCS()) goto SetSDir;
    INX();
SetSDir:
    STQ_zp(cpu.X, Enemy_MovingDir);
    LDQ_imm(cpu.X, 0x00);
    LDQ_zp(cpu.A, 0x00);
    PHA();
    CALL(InjurePlayer);
    PLA();
    STQ_zp(cpu.A, 0x00);
NoColFB:
    PLA();
    CLC();
    ADC_imm(0x04);
    STQ_zp(cpu.A, 0x06);
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

GetFirebarPosition:
    PHA();
    AND_imm(0b00001111);
    CMPQ_imm(cpu.A, 0x09);
    if (BCC()) goto GetHAdder;
    EOR_imm(0b00001111);
    CLC();
    ADC_imm(0x01);
GetHAdder:
    STQ_zp(cpu.A, 0x01);
    LDQ_zp(cpu.Y, 0x00);
    LDQ_PRGTable(cpu.A, FirebarTblOffsets, cpu.Y);
    CLC();
    ADC_zp(0x01);
    CopyAY();
    LDQ_PRGTable(cpu.A, FirebarPosLookupTbl, cpu.Y);
    STQ_zp(cpu.A, 0x01);
    PLA();
    PHA();
    CLC();
    ADC_imm(0x08);
    AND_imm(0b00001111);
    CMPQ_imm(cpu.A, 0x09);
    if (BCC()) goto GetVAdder;
    EOR_imm(0b00001111);
    CLC();
    ADC_imm(0x01);
GetVAdder:
    STQ_zp(cpu.A, 0x02);
    LDQ_zp(cpu.Y, 0x00);
    LDQ_PRGTable(cpu.A, FirebarTblOffsets, cpu.Y);
    CLC();
    ADC_zp(0x02);
    CopyAY();
    LDQ_PRGTable(cpu.A, FirebarPosLookupTbl, cpu.Y);
    STQ_zp(cpu.A, 0x02);
    PLA();
    LSR_acc();
    LSR_acc();
    LSR_acc();
    CopyAY();
    LDQ_PRGTable(cpu.A, FirebarMirrorData, cpu.Y);
    STQ_zp(cpu.A, 0x03);
    RET();

MoveFlyingCheepCheep:
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b00100000);
    if (BEQ()) goto FlyCC;
    LDQ_imm(cpu.A, 0x00);
    STA_abs_xy(Enemy_SprAttrib, cpu.X);
    
    goto MoveJ_EnemyVertically;
FlyCC:
    CALL(MoveEnemyHorizontally);
    LDQ_imm(cpu.Y, 0x0d);
    LDQ_imm(cpu.A, 0x05);
    CALL(SetXMoveAmt);
    LDQ_abs_xy(cpu.A, Enemy_Y_MoveForce, cpu.X);
    LSR_acc();
    LSR_acc();
    LSR_acc();
    LSR_acc();
    CopyAY();
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    SEC();
    SBC_PRGTable(PRandomSubtracter, cpu.Y);
    if (BPL()) goto AddCCF;
    EOR_imm(0xff);
    CLC();
    ADC_imm(0x01);
AddCCF:
    CMPQ_imm(cpu.A, 0x08);
    if (BCS()) goto BPGet;
    LDQ_abs_xy(cpu.A, Enemy_Y_MoveForce, cpu.X);
    CLC();
    ADC_imm(0x10);
    STA_abs_xy(Enemy_Y_MoveForce, cpu.X);
    LSR_acc();
    LSR_acc();
    LSR_acc();
    LSR_acc();
    CopyAY();
BPGet:
    LDQ_PRGTable(cpu.A, FlyCCBPriority, cpu.Y);
    STA_abs_xy(Enemy_SprAttrib, cpu.X);
    RET();

MoveLakitu:
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b00100000);
    if (BEQ()) goto ChkLS;
    
    goto MoveD_EnemyVertically;
ChkLS:
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    if (BEQ()) goto Fr12S;
    LDQ_imm(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, LakituMoveDirection, cpu.X);
    STQ_abs(cpu.A, EnemyFrenzyBuffer);
    LDQ_imm(cpu.A, 0x10);
    if (BNE()) goto SetLSpd;

Fr12S:
    LDQ_imm(cpu.A, Spiny);
    STQ_abs(cpu.A, EnemyFrenzyBuffer);
    LDQ_imm(cpu.Y, 0x02);
LdLDa:
    LDQ_PRGTable(cpu.A, LakituDiffAdj, cpu.Y);
    STA_abs_xy(0x0001, cpu.Y);
    DEY();
    if (BPL()) goto LdLDa;
    CALL(PlayerLakituDiff);
SetLSpd:
    STQ_zp_xy(cpu.A, LakituMoveSpeed, cpu.X);
    LDQ_imm(cpu.Y, 0x01);
    LDQ_zp_xy(cpu.A, LakituMoveDirection, cpu.X);
    AND_imm(0x01);
    if (BNE()) goto SetLMov;
    LDQ_zp_xy(cpu.A, LakituMoveSpeed, cpu.X);
    EOR_imm(0xff);
    CLC();
    ADC_imm(0x01);
    STQ_zp_xy(cpu.A, LakituMoveSpeed, cpu.X);
    INY();
SetLMov:
    STQ_zp_xy(cpu.Y, Enemy_MovingDir, cpu.X);
    
    goto MoveEnemyHorizontally;

PlayerLakituDiff:
    LDQ_imm(cpu.Y, 0x00);
    CALL(PlayerEnemyDiff);
    if (BPL()) goto ChkLakDif;
    INY();
    LDQ_zp(cpu.A, 0x00);
    EOR_imm(0xff);
    CLC();
    ADC_imm(0x01);
    STQ_zp(cpu.A, 0x00);
ChkLakDif:
    LDQ_zp(cpu.A, 0x00);
    CMPQ_imm(cpu.A, 0x3c);
    if (BCC()) goto ChkPSpeed;
    LDQ_imm(cpu.A, 0x3c);
    STQ_zp(cpu.A, 0x00);
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, Lakitu);
    if (BNE()) goto ChkPSpeed;
    CopyYA();
    CMPA_zp_xy(LakituMoveDirection, cpu.X);
    if (BEQ()) goto ChkPSpeed;
    LDQ_zp_xy(cpu.A, LakituMoveDirection, cpu.X);
    if (BEQ()) goto SetLMovD;
    DEC_zp_x(LakituMoveSpeed);
    LDQ_zp_xy(cpu.A, LakituMoveSpeed, cpu.X);
    if (BNE()) goto ExMoveLak;

SetLMovD:
    CopyYA();
    STQ_zp_xy(cpu.A, LakituMoveDirection, cpu.X);
ChkPSpeed:
    LDQ_zp(cpu.A, 0x00);
    AND_imm(0b00111100);
    LSR_acc();
    LSR_acc();
    STQ_zp(cpu.A, 0x00);
    LDQ_imm(cpu.Y, 0x00);
    LDQ_zp(cpu.A, Player_X_Speed);
    if (BEQ()) goto SubDifAdj;
    LDQ_abs(cpu.A, ScrollAmount);
    if (BEQ()) goto SubDifAdj;
    INY();
    LDQ_zp(cpu.A, Player_X_Speed);
    CMPQ_imm(cpu.A, 0x19);
    if (BCC()) goto ChkSpinyO;
    LDQ_abs(cpu.A, ScrollAmount);
    CMPQ_imm(cpu.A, 0x02);
    if (BCC()) goto ChkSpinyO;
    INY();
ChkSpinyO:
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, Spiny);
    if (BNE()) goto ChkEmySpd;
    LDQ_zp(cpu.A, Player_X_Speed);
    if (BNE()) goto SubDifAdj;

ChkEmySpd:
    LDQ_zp_xy(cpu.A, Enemy_Y_Speed, cpu.X);
    if (BNE()) goto SubDifAdj;
    LDQ_imm(cpu.Y, 0x00);
SubDifAdj:
    LDQ_abs_xy(cpu.A, 0x0001, cpu.Y);
    LDQ_zp(cpu.Y, 0x00);
SPixelLak:
    SEC();
    SBC_imm(0x01);
    DEY();
    if (BPL()) goto SPixelLak;

ExMoveLak:
    RET();

BridgeCollapse:
    LDQ_abs(cpu.X, BowserFront_Offset);
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, Bowser);
    if (BNE()) goto SetM2;
    STQ_zp(cpu.X, ObjectOffset);
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    if (BEQ()) goto RemoveBridge;
    AND_imm(0b01000000);
    if (BEQ()) goto SetM2;
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CMPQ_imm(cpu.A, 0xe0);
    if (BCC()) goto MoveD_Bowser;

SetM2:
    LDQ_imm(cpu.A, Silence);
    STQ_zp(cpu.A, EventMusicQueue);
    INC_abs(OperMode_Task);
    
    goto KillAllEnemies;

MoveD_Bowser:
    CALL(MoveEnemySlowVert);
    
    goto BowserGfxHandler;

RemoveBridge:
    DEC_abs(BowserFeetCounter);
    if (BNE()) goto NoBFall;
    LDQ_imm(cpu.A, 0x04);
    STQ_abs(cpu.A, BowserFeetCounter);
    LDQ_abs(cpu.A, BowserBodyControls);
    EOR_imm(0x01);
    STQ_abs(cpu.A, BowserBodyControls);
    LDQ_imm(cpu.A, 0x22);
    STQ_zp(cpu.A, 0x05);
    LDQ_abs(cpu.Y, BridgeCollapseOffset);
    LDQ_PRGTable(cpu.A, BridgeCollapseData, cpu.Y);
    STQ_zp(cpu.A, 0x04);
    LDQ_abs(cpu.Y, VRAM_Buffer1_Offset);
    INY();
    LDQ_imm(cpu.X, 0x0c);
    CALL(RemBridge);
    LDQ_zp(cpu.X, ObjectOffset);
    CALL(MoveVOffset);
    LDQ_imm(cpu.A, Sfx_Blast);
    STQ_zp(cpu.A, Square2SoundQueue);
    LDQ_imm(cpu.A, Sfx_BrickShatter);
    STQ_zp(cpu.A, NoiseSoundQueue);
    INC_abs(BridgeCollapseOffset);
    LDQ_abs(cpu.A, BridgeCollapseOffset);
    CMPQ_imm(cpu.A, 0x0f);
    if (BNE()) goto NoBFall;
    CALL(InitVStf);
    LDQ_imm(cpu.A, 0b01000000);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    LDQ_imm(cpu.A, Sfx_BowserFall);
    STQ_zp(cpu.A, Square2SoundQueue);
NoBFall:
    
    goto BowserGfxHandler;

RunBowser:
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b00100000);
    if (BEQ()) goto BowserControl;
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CMPQ_imm(cpu.A, 0xe0);
    if (BCC()) goto MoveD_Bowser;

KillAllEnemies:
    LDQ_imm(cpu.X, 0x04);
KillLoop:
    CALL(EraseEnemyObject);
    DEX();
    if (BPL()) goto KillLoop;
    STQ_abs(cpu.A, EnemyFrenzyBuffer);
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

BowserControl:
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, EnemyFrenzyBuffer);
    LDQ_abs(cpu.A, TimerControl);
    if (BEQ()) goto ChkMouth;
    
    goto SkipToFB;
ChkMouth:
    LDQ_abs(cpu.A, BowserBodyControls);
    if (BPL()) goto FeetTmr;
    
    goto HammerChk;
FeetTmr:
    DEC_abs(BowserFeetCounter);
    if (BNE()) goto ResetMDr;
    LDQ_imm(cpu.A, 0x20);
    STQ_abs(cpu.A, BowserFeetCounter);
    LDQ_abs(cpu.A, BowserBodyControls);
    EOR_imm(0b00000001);
    STQ_abs(cpu.A, BowserBodyControls);
ResetMDr:
    LDQ_zp(cpu.A, FrameCounter);
    AND_imm(0b00001111);
    if (BNE()) goto B_FaceP;
    LDQ_imm(cpu.A, 0x02);
    STQ_zp_xy(cpu.A, Enemy_MovingDir, cpu.X);
B_FaceP:
    LDQ_abs_xy(cpu.A, EnemyFrameTimer, cpu.X);
    if (BEQ()) goto GetPRCmp;
    CALL(PlayerEnemyDiff);
    if (BPL()) goto GetPRCmp;
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Enemy_MovingDir, cpu.X);
    LDQ_imm(cpu.A, 0x02);
    STQ_abs(cpu.A, BowserMovementSpeed);
    LDQ_imm(cpu.A, 0x20);
    STA_abs_xy(EnemyFrameTimer, cpu.X);
    STQ_abs(cpu.A, BowserFireBreathTimer);
    LDQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    CMPQ_imm(cpu.A, 0xc8);
    if (BCS()) goto HammerChk;

GetPRCmp:
    LDQ_zp(cpu.A, FrameCounter);
    AND_imm(0b00000011);
    if (BNE()) goto HammerChk;
    LDQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    CMPQ_abs(cpu.A, BowserOrigXPos);
    if (BNE()) goto GetDToO;
    LDQ_abs_xy(cpu.A, PseudoRandomBitReg, cpu.X);
    AND_imm(0b00000011);
    CopyAY();
    LDQ_PRGTable(cpu.A, PRandomRange, cpu.Y);
    STQ_abs(cpu.A, MaxRangeFromOrigin);
GetDToO:
    LDQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    CLC();
    ADC_abs(BowserMovementSpeed);
    STQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    LDQ_zp_xy(cpu.Y, Enemy_MovingDir, cpu.X);
    CMPQ_imm(cpu.Y, 0x01);
    if (BEQ()) goto HammerChk;
    LDQ_imm(cpu.Y, 0xff);
    SEC();
    SBC_abs(BowserOrigXPos);
    if (BPL()) goto CompDToO;
    EOR_imm(0xff);
    CLC();
    ADC_imm(0x01);
    LDQ_imm(cpu.Y, 0x01);
CompDToO:
    CMPQ_abs(cpu.A, MaxRangeFromOrigin);
    if (BCC()) goto HammerChk;
    STQ_abs(cpu.Y, BowserMovementSpeed);
HammerChk:
    LDQ_abs_xy(cpu.A, EnemyFrameTimer, cpu.X);
    if (BNE()) goto MakeBJump;
    CALL(MoveEnemySlowVert);
    LDQ_abs(cpu.A, WorldNumber);
    CMPQ_imm(cpu.A, World6);
    if (BCC()) goto SetHmrTmr;
    LDQ_zp(cpu.A, FrameCounter);
    AND_imm(0b00000011);
    if (BNE()) goto SetHmrTmr;
    CALL(SpawnHammerObj);
SetHmrTmr:
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CMPQ_imm(cpu.A, 0x80);
    if (BCC()) goto ChkFireB;
    LDQ_abs_xy(cpu.A, PseudoRandomBitReg, cpu.X);
    AND_imm(0b00000011);
    CopyAY();
    LDQ_PRGTable(cpu.A, PRandomRange, cpu.Y);
    STA_abs_xy(EnemyFrameTimer, cpu.X);
SkipToFB:
    
    goto ChkFireB;
MakeBJump:
    CMPQ_imm(cpu.A, 0x01);
    if (BNE()) goto ChkFireB;
    DEC_zp_x(Enemy_Y_Position);
    CALL(InitVStf);
    LDQ_imm(cpu.A, 0xfe);
    STQ_zp_xy(cpu.A, Enemy_Y_Speed, cpu.X);
ChkFireB:
    LDQ_abs(cpu.A, WorldNumber);
    CMPQ_imm(cpu.A, World8);
    if (BEQ()) goto SpawnFBr;
    CMPQ_imm(cpu.A, World6);
    if (BCS()) goto BowserGfxHandler;

SpawnFBr:
    LDQ_abs(cpu.A, BowserFireBreathTimer);
    if (BNE()) goto BowserGfxHandler;
    LDQ_imm(cpu.A, 0x20);
    STQ_abs(cpu.A, BowserFireBreathTimer);
    LDQ_abs(cpu.A, BowserBodyControls);
    EOR_imm(0b10000000);
    STQ_abs(cpu.A, BowserBodyControls);
    if (BMI()) goto ChkFireB;
    CALL(SetFlameTimer);
    LDQ_abs(cpu.Y, SecondaryHardMode);
    if (BEQ()) goto SetFBTmr;
    SEC();
    SBC_imm(0x10);
SetFBTmr:
    STQ_abs(cpu.A, BowserFireBreathTimer);
    LDQ_imm(cpu.A, BowserFlame);
    STQ_abs(cpu.A, EnemyFrenzyBuffer);

BowserGfxHandler:
    CALL(ProcessBowserHalf);
    LDQ_imm(cpu.Y, 0x10);
    LDQ_zp_xy(cpu.A, Enemy_MovingDir, cpu.X);
    LSR_acc();
    if (BCC()) goto CopyFToR;
    LDQ_imm(cpu.Y, 0xf0);
CopyFToR:
    CopyYA();
    CLC();
    ADC_zp_x(Enemy_X_Position);
    LDQ_abs(cpu.Y, DuplicateObj_Offset);
    STA_abs_xy(Enemy_X_Position, cpu.Y);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CLC();
    ADC_imm(0x08);
    STA_abs_xy(Enemy_Y_Position, cpu.Y);
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    STA_abs_xy(Enemy_State, cpu.Y);
    LDQ_zp_xy(cpu.A, Enemy_MovingDir, cpu.X);
    STA_abs_xy(Enemy_MovingDir, cpu.Y);
    LDQ_zp(cpu.A, ObjectOffset);
    PHA();
    LDQ_abs(cpu.X, DuplicateObj_Offset);
    STQ_zp(cpu.X, ObjectOffset);
    LDQ_imm(cpu.A, Bowser);
    STQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CALL(ProcessBowserHalf);
    PLA();
    STQ_zp(cpu.A, ObjectOffset);
    CopyAX();
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, BowserGfxFlag);
ExBGfxH:
    RET();

ProcessBowserHalf:
    INC_abs(BowserGfxFlag);
    CALL(RunRetainerObj);
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    if (BNE()) goto ExBGfxH;
    LDQ_imm(cpu.A, 0x0a);
    STA_abs_xy(Enemy_BoundBoxCtrl, cpu.X);
    CALL(GetEnemyBoundBox);
    
    goto PlayerEnemyCollision;

SetFlameTimer:
    LDQ_abs(cpu.Y, BowserFlameTimerCtrl);
    INC_abs(BowserFlameTimerCtrl);
    LDQ_abs(cpu.A, BowserFlameTimerCtrl);
    AND_imm(0b00000111);
    STQ_abs(cpu.A, BowserFlameTimerCtrl);
    LDQ_PRGTable(cpu.A, FlameTimerData, cpu.Y);
ExFl:
    RET();

ProcBowserFlame:
    LDQ_abs(cpu.A, TimerControl);
    if (BNE()) goto SetGfxF;
    LDQ_imm(cpu.A, 0x40);
    LDQ_abs(cpu.Y, SecondaryHardMode);
    if (BEQ()) goto SFlmX;
    LDQ_imm(cpu.A, 0x60);
SFlmX:
    STQ_zp(cpu.A, 0x00);
    LDQ_abs_xy(cpu.A, Enemy_X_MoveForce, cpu.X);
    SEC();
    SBC_zp(0x00);
    STA_abs_xy(Enemy_X_MoveForce, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    SBC_imm(0x01);
    STQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    SBC_imm(0x00);
    STQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    LDQ_abs_xy(cpu.Y, BowserFlamePRandomOfs, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CMPA_PRGTable(FlameYPosData, cpu.Y);
    if (BEQ()) goto SetGfxF;
    CLC();
    ADC_abs_x(Enemy_Y_MoveForce);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
SetGfxF:
    CALL(RelativeEnemyPosition);
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    if (BNE()) goto ExFl;
    LDQ_imm(cpu.A, 0x51);
    STQ_zp(cpu.A, 0x00);
    LDQ_imm(cpu.Y, 0x02);
    LDQ_zp(cpu.A, FrameCounter);
    AND_imm(0b00000010);
    if (BEQ()) goto FlmeAt;
    LDQ_imm(cpu.Y, 0x82);
FlmeAt:
    STQ_zp(cpu.Y, 0x01);
    LDQ_abs_xy(cpu.Y, Enemy_SprDataOffset, cpu.X);
    LDQ_imm(cpu.X, 0x00);

DrawFlameLoop:
    LDQ_abs(cpu.A, Enemy_Rel_YPos);
    STA_abs_xy(Sprite_Y_Position, cpu.Y);
    LDQ_zp(cpu.A, 0x00);
    STA_abs_xy(Sprite_Tilenumber, cpu.Y);
    INC_zp(0x00);
    LDQ_zp(cpu.A, 0x01);
    STA_abs_xy(Sprite_Attributes, cpu.Y);
    LDQ_abs(cpu.A, Enemy_Rel_XPos);
    STA_abs_xy(Sprite_X_Position, cpu.Y);
    CLC();
    ADC_imm(0x08);
    STQ_abs(cpu.A, Enemy_Rel_XPos);
    INY();
    INY();
    INY();
    INY();
    INX();
    CMPQ_imm(cpu.X, 0x03);
    if (BCC()) goto DrawFlameLoop;
    LDQ_zp(cpu.X, ObjectOffset);
    CALL(GetEnemyOffscreenBits);
    LDQ_abs_xy(cpu.Y, Enemy_SprDataOffset, cpu.X);
    LDQ_abs(cpu.A, Enemy_OffscreenBits);
    LSR_acc();
    PHA();
    if (BCC()) goto M3FOfs;
    LDQ_imm(cpu.A, 0xf8);
    STA_abs_xy(Sprite_Y_Position + 12, cpu.Y);
M3FOfs:
    PLA();
    LSR_acc();
    PHA();
    if (BCC()) goto M2FOfs;
    LDQ_imm(cpu.A, 0xf8);
    STA_abs_xy(Sprite_Y_Position + 8, cpu.Y);
M2FOfs:
    PLA();
    LSR_acc();
    PHA();
    if (BCC()) goto M1FOfs;
    LDQ_imm(cpu.A, 0xf8);
    STA_abs_xy(Sprite_Y_Position + 4, cpu.Y);
M1FOfs:
    PLA();
    LSR_acc();
    if (BCC()) goto ExFlmeD;
    LDQ_imm(cpu.A, 0xf8);
    STA_abs_xy(Sprite_Y_Position, cpu.Y);
ExFlmeD:
    RET();

RunFireworks:
    DEC_zp_x(ExplosionTimerCounter);
    if (BNE()) goto SetupExpl;
    LDQ_imm(cpu.A, 0x08);
    STQ_zp_xy(cpu.A, ExplosionTimerCounter, cpu.X);
    INC_zp_x(ExplosionGfxCounter);
    LDQ_zp_xy(cpu.A, ExplosionGfxCounter, cpu.X);
    CMPQ_imm(cpu.A, 0x03);
    if (BCS()) goto FireworksSoundScore;

SetupExpl:
    CALL(RelativeEnemyPosition);
    LDQ_abs(cpu.A, Enemy_Rel_YPos);
    STQ_abs(cpu.A, Fireball_Rel_YPos);
    LDQ_abs(cpu.A, Enemy_Rel_XPos);
    STQ_abs(cpu.A, Fireball_Rel_XPos);
    LDQ_abs_xy(cpu.Y, Enemy_SprDataOffset, cpu.X);
    LDQ_zp_xy(cpu.A, ExplosionGfxCounter, cpu.X);
    CALL(DrawExplosion_Fireworks);
    RET();

FireworksSoundScore:
    LDQ_imm(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    LDQ_imm(cpu.A, Sfx_Blast);
    STQ_zp(cpu.A, Square2SoundQueue);
    LDQ_imm(cpu.A, 0x05);
    STQ_abs(cpu.A, DigitModifier + 4);
    
    goto EndAreaPoints;

RunStarFlagObj:
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, EnemyFrenzyBuffer);
    LDQ_abs(cpu.A, StarFlagTaskControl);
    CMPQ_imm(cpu.A, 0x05);
    if (BCS()) goto StarFlagExit;
    switch (cpu.A) {
        case 0: goto StarFlagExit;
        case 1: goto GameTimerFireworks;
        case 2: goto AwardGameTimerPoints;
        case 3: goto RaiseFlagSetoffFWorks;
        case 4: goto DelayToAreaEnd;
    }
    __builtin_unreachable();

GameTimerFireworks:
    LDQ_imm(cpu.Y, 0x05);
    LDQ_abs(cpu.A, GameTimerDisplay + 2);
    CMPQ_imm(cpu.A, 0x01);
    if (BEQ()) goto SetFWC;
    LDQ_imm(cpu.Y, 0x03);
    CMPQ_imm(cpu.A, 0x03);
    if (BEQ()) goto SetFWC;
    LDQ_imm(cpu.Y, 0x00);
    CMPQ_imm(cpu.A, 0x06);
    if (BEQ()) goto SetFWC;
    LDQ_imm(cpu.A, 0xff);
SetFWC:
    STQ_abs(cpu.A, FireworksCounter);
    STQ_zp_xy(cpu.Y, Enemy_State, cpu.X);

IncrementSFTask1:
    INC_abs(StarFlagTaskControl);

StarFlagExit:
    RET();

AwardGameTimerPoints:
    LDQ_abs(cpu.A, GameTimerDisplay);
    ORA_abs(GameTimerDisplay + 1);
    ORA_abs(GameTimerDisplay + 2);
    if (BEQ()) goto IncrementSFTask1;
    LDQ_zp(cpu.A, FrameCounter);
    AND_imm(0b00000100);
    if (BEQ()) goto NoTTick;
    LDQ_imm(cpu.A, Sfx_TimerTick);
    STQ_zp(cpu.A, Square2SoundQueue);
NoTTick:
    LDQ_imm(cpu.Y, 0x23);
    LDQ_imm(cpu.A, 0xff);
    STQ_abs(cpu.A, DigitModifier + 5);
    CALL(DigitsMathRoutine);
    LDQ_imm(cpu.A, 0x05);
    STQ_abs(cpu.A, DigitModifier + 5);

EndAreaPoints:
    LDQ_imm(cpu.Y, 0x0b);
    LDQ_abs(cpu.A, CurrentPlayer);
    if (BEQ()) goto ELPGive;
    LDQ_imm(cpu.Y, 0x11);
ELPGive:
    CALL(DigitsMathRoutine);
    LDQ_abs(cpu.A, CurrentPlayer);
    ASL_acc();
    ASL_acc();
    ASL_acc();
    ASL_acc();
    ORA_imm(0b00000100);
    
    goto UpdateNumber;

RaiseFlagSetoffFWorks:
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CMPQ_imm(cpu.A, 0x72);
    if (BCC()) goto SetoffF;
    DEC_zp_x(Enemy_Y_Position);
    
    goto DrawStarFlag;
SetoffF:
    LDQ_abs(cpu.A, FireworksCounter);
    if (BEQ()) goto DrawFlagSetTimer;
    if (BMI()) goto DrawFlagSetTimer;
    LDQ_imm(cpu.A, Fireworks);
    STQ_abs(cpu.A, EnemyFrenzyBuffer);

DrawStarFlag:
    CALL(RelativeEnemyPosition);
    LDQ_abs_xy(cpu.Y, Enemy_SprDataOffset, cpu.X);
    LDQ_imm(cpu.X, 0x03);
DSFLoop:
    LDQ_abs(cpu.A, Enemy_Rel_YPos);
    CLC();
    ADC_PRGTable(StarFlagYPosAdder, cpu.X);
    STA_abs_xy(Sprite_Y_Position, cpu.Y);
    LDQ_PRGTable(cpu.A, StarFlagTileData, cpu.X);
    STA_abs_xy(Sprite_Tilenumber, cpu.Y);
    LDQ_imm(cpu.A, 0x22);
    STA_abs_xy(Sprite_Attributes, cpu.Y);
    LDQ_abs(cpu.A, Enemy_Rel_XPos);
    CLC();
    ADC_PRGTable(StarFlagXPosAdder, cpu.X);
    STA_abs_xy(Sprite_X_Position, cpu.Y);
    INY();
    INY();
    INY();
    INY();
    DEX();
    if (BPL()) goto DSFLoop;
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

DrawFlagSetTimer:
    CALL(DrawStarFlag);
    LDQ_imm(cpu.A, 0x06);
    STA_abs_xy(EnemyIntervalTimer, cpu.X);

IncrementSFTask2:
    INC_abs(StarFlagTaskControl);
    RET();

DelayToAreaEnd:
    CALL(DrawStarFlag);
    LDQ_abs_xy(cpu.A, EnemyIntervalTimer, cpu.X);
    if (BNE()) goto StarFlagExit2;
    LDQ_abs(cpu.A, EventMusicBuffer);
    if (BEQ()) goto IncrementSFTask2;

StarFlagExit2:
    RET();

MovePiranhaPlant:
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    if (BNE()) goto PutinPipe;
    LDQ_abs_xy(cpu.A, EnemyFrameTimer, cpu.X);
    if (BNE()) goto PutinPipe;
    LDQ_zp_xy(cpu.A, PiranhaPlant_MoveFlag, cpu.X);
    if (BNE()) goto SetupToMovePPlant;
    LDQ_zp_xy(cpu.A, PiranhaPlant_Y_Speed, cpu.X);
    if (BMI()) goto ReversePlantSpeed;
    CALL(PlayerEnemyDiff);
    if (BPL()) goto ChkPlayerNearPipe;
    LDQ_zp(cpu.A, 0x00);
    EOR_imm(0xff);
    CLC();
    ADC_imm(0x01);
    STQ_zp(cpu.A, 0x00);

ChkPlayerNearPipe:
    LDQ_zp(cpu.A, 0x00);
    CMPQ_imm(cpu.A, 0x21);
    if (BCC()) goto PutinPipe;

ReversePlantSpeed:
    LDQ_zp_xy(cpu.A, PiranhaPlant_Y_Speed, cpu.X);
    EOR_imm(0xff);
    CLC();
    ADC_imm(0x01);
    STQ_zp_xy(cpu.A, PiranhaPlant_Y_Speed, cpu.X);
    INC_zp_x(PiranhaPlant_MoveFlag);

SetupToMovePPlant:
    LDQ_abs_xy(cpu.A, PiranhaPlantDownYPos, cpu.X);
    LDQ_zp_xy(cpu.Y, PiranhaPlant_Y_Speed, cpu.X);
    if (BPL()) goto RiseFallPiranhaPlant;
    LDQ_abs_xy(cpu.A, PiranhaPlantUpYPos, cpu.X);

RiseFallPiranhaPlant:
    STQ_zp(cpu.A, 0x00);
    LDQ_zp(cpu.A, FrameCounter);
    LSR_acc();
    if (BCC()) goto PutinPipe;
    LDQ_abs(cpu.A, TimerControl);
    if (BNE()) goto PutinPipe;
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CLC();
    ADC_zp_x(PiranhaPlant_Y_Speed);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CMPQ_zp(cpu.A, 0x00);
    if (BNE()) goto PutinPipe;
    LDQ_imm(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, PiranhaPlant_MoveFlag, cpu.X);
    LDQ_imm(cpu.A, 0x40);
    STA_abs_xy(EnemyFrameTimer, cpu.X);

PutinPipe:
    LDQ_imm(cpu.A, 0b00100000);
    STA_abs_xy(Enemy_SprAttrib, cpu.X);
    RET();

FirebarSpin:
    STQ_zp(cpu.A, 0x07);
    LDQ_zp_xy(cpu.A, FirebarSpinDirection, cpu.X);
    if (BNE()) goto SpinCounterClockwise;
    LDQ_imm(cpu.Y, 0x18);
    LDQ_zp_xy(cpu.A, FirebarSpinState_Low, cpu.X);
    CLC();
    ADC_zp(0x07);
    STQ_zp_xy(cpu.A, FirebarSpinState_Low, cpu.X);
    LDQ_zp_xy(cpu.A, FirebarSpinState_High, cpu.X);
    ADC_imm(0x00);
    RET();

SpinCounterClockwise:
    LDQ_imm(cpu.Y, 0x08);
    LDQ_zp_xy(cpu.A, FirebarSpinState_Low, cpu.X);
    SEC();
    SBC_zp(0x07);
    STQ_zp_xy(cpu.A, FirebarSpinState_Low, cpu.X);
    LDQ_zp_xy(cpu.A, FirebarSpinState_High, cpu.X);
    SBC_imm(0x00);
    RET();

BalancePlatform:
    LDQ_zp_xy(cpu.A, Enemy_Y_HighPos, cpu.X);
    CMPQ_imm(cpu.A, 0x03);
    if (BNE()) goto DoBPl;
    
    goto EraseEnemyObject;
DoBPl:
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    if (BPL()) goto CheckBalPlatform;
    RET();

CheckBalPlatform:
    CopyAY();
    LDQ_abs_xy(cpu.A, PlatformCollisionFlag, cpu.X);
    STQ_zp(cpu.A, 0x00);
    LDQ_zp_xy(cpu.A, Enemy_MovingDir, cpu.X);
    if (BEQ()) goto ChkForFall;
    
    goto PlatformFall;

ChkForFall:
    LDQ_imm(cpu.A, 0x2d);
    CMPA_zp_xy(Enemy_Y_Position, cpu.X);
    if (BCC()) goto ChkOtherForFall;
    CMPQ_zp(cpu.Y, 0x00);
    if (BEQ()) goto MakePlatformFall;
    CLC();
    ADC_imm(0x02);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    
    goto StopPlatforms;

MakePlatformFall:
    goto InitPlatformFall;

ChkOtherForFall:
    CMPA_abs_xy(Enemy_Y_Position, cpu.Y);
    if (BCC()) goto ChkToMoveBalPlat;
    CMPQ_zp(cpu.X, 0x00);
    if (BEQ()) goto MakePlatformFall;
    CLC();
    ADC_imm(0x02);
    STA_abs_xy(Enemy_Y_Position, cpu.Y);
    
    goto StopPlatforms;

ChkToMoveBalPlat:
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    PHA();
    LDQ_abs_xy(cpu.A, PlatformCollisionFlag, cpu.X);
    if (BPL()) goto ColFlg;
    LDQ_abs_xy(cpu.A, Enemy_Y_MoveForce, cpu.X);
    CLC();
    ADC_imm(0x05);
    STQ_zp(cpu.A, 0x00);
    LDQ_zp_xy(cpu.A, Enemy_Y_Speed, cpu.X);
    ADC_imm(0x00);
    if (BMI()) goto PlatDn;
    if (BNE()) goto PlatUp;
    LDQ_zp(cpu.A, 0x00);
    CMPQ_imm(cpu.A, 0x0b);
    if (BCC()) goto PlatSt;
    if (BCS()) goto PlatUp;

ColFlg:
    CMPQ_zp(cpu.A, ObjectOffset);
    if (BEQ()) goto PlatDn;

PlatUp:
    CALL(MovePlatformUp);
    
    goto DoOtherPlatform;
PlatSt:
    CALL(StopPlatforms);
    
    goto DoOtherPlatform;
PlatDn:
    CALL(MovePlatformDown);

DoOtherPlatform:
    LDQ_zp_xy(cpu.Y, Enemy_State, cpu.X);
    PLA();
    SEC();
    SBC_zp_x(Enemy_Y_Position);
    CLC();
    ADC_abs_y(Enemy_Y_Position);
    STA_abs_xy(Enemy_Y_Position, cpu.Y);
    LDQ_abs_xy(cpu.A, PlatformCollisionFlag, cpu.X);
    if (BMI()) goto DrawEraseRope;
    CopyAX();
    CALL(PositionPlayerOnVPlat);

DrawEraseRope:
    LDQ_zp(cpu.Y, ObjectOffset);
    LDQ_abs_xy(cpu.A, Enemy_Y_Speed, cpu.Y);
    ORA_abs_xy(Enemy_Y_MoveForce, cpu.Y);
    if (BEQ()) goto ExitRp;
    LDQ_abs(cpu.X, VRAM_Buffer1_Offset);
    CMPQ_imm(cpu.X, 0x20);
    if (BCS()) goto ExitRp;
    LDQ_abs_xy(cpu.A, Enemy_Y_Speed, cpu.Y);
    PHA();
    PHA();
    CALL(SetupPlatformRope);
    LDQ_zp(cpu.A, 0x01);
    STA_abs_xy(VRAM_Buffer1, cpu.X);
    LDQ_zp(cpu.A, 0x00);
    STA_abs_xy(VRAM_Buffer1 + 1, cpu.X);
    LDQ_imm(cpu.A, 0x02);
    STA_abs_xy(VRAM_Buffer1 + 2, cpu.X);
    LDQ_abs_xy(cpu.A, Enemy_Y_Speed, cpu.Y);
    if (BMI()) goto EraseR1;
    LDQ_imm(cpu.A, 0xa2);
    STA_abs_xy(VRAM_Buffer1 + 3, cpu.X);
    LDQ_imm(cpu.A, 0xa3);
    STA_abs_xy(VRAM_Buffer1 + 4, cpu.X);
    
    goto OtherRope;
EraseR1:
    LDQ_imm(cpu.A, 0x24);
    STA_abs_xy(VRAM_Buffer1 + 3, cpu.X);
    STA_abs_xy(VRAM_Buffer1 + 4, cpu.X);

OtherRope:
    LDQ_abs_xy(cpu.A, Enemy_State, cpu.Y);
    CopyAY();
    PLA();
    EOR_imm(0xff);
    CALL(SetupPlatformRope);
    LDQ_zp(cpu.A, 0x01);
    STA_abs_xy(VRAM_Buffer1 + 5, cpu.X);
    LDQ_zp(cpu.A, 0x00);
    STA_abs_xy(VRAM_Buffer1 + 6, cpu.X);
    LDQ_imm(cpu.A, 0x02);
    STA_abs_xy(VRAM_Buffer1 + 7, cpu.X);
    PLA();
    if (BPL()) goto EraseR2;
    LDQ_imm(cpu.A, 0xa2);
    STA_abs_xy(VRAM_Buffer1 + 8, cpu.X);
    LDQ_imm(cpu.A, 0xa3);
    STA_abs_xy(VRAM_Buffer1 + 9, cpu.X);
    
    goto EndRp;
EraseR2:
    LDQ_imm(cpu.A, 0x24);
    STA_abs_xy(VRAM_Buffer1 + 8, cpu.X);
    STA_abs_xy(VRAM_Buffer1 + 9, cpu.X);
EndRp:
    LDQ_imm(cpu.A, 0x00);
    STA_abs_xy(VRAM_Buffer1 + 10, cpu.X);
    LDQ_abs(cpu.A, VRAM_Buffer1_Offset);
    CLC();
    ADC_imm(10);
    STQ_abs(cpu.A, VRAM_Buffer1_Offset);
ExitRp:
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

SetupPlatformRope:
    PHA();
    LDQ_abs_xy(cpu.A, Enemy_X_Position, cpu.Y);
    CLC();
    ADC_imm(0x08);
    LDQ_abs(cpu.X, SecondaryHardMode);
    if (BNE()) goto GetLRp;
    CLC();
    ADC_imm(0x10);
GetLRp:
    PHA();
    LDQ_abs_xy(cpu.A, Enemy_PageLoc, cpu.Y);
    ADC_imm(0x00);
    STQ_zp(cpu.A, 0x02);
    PLA();
    AND_imm(0b11110000);
    LSR_acc();
    LSR_acc();
    LSR_acc();
    STQ_zp(cpu.A, 0x00);
    LDQ_zp_xy(cpu.X, Enemy_Y_Position, cpu.Y);
    PLA();
    if (BPL()) goto GetHRp;
    CopyXA();
    CLC();
    ADC_imm(0x08);
    CopyAX();
GetHRp:
    CopyXA();
    LDQ_abs(cpu.X, VRAM_Buffer1_Offset);
    ASL_acc();
    ROL_acc();
    PHA();
    ROL_acc();
    AND_imm(0b00000011);
    ORA_imm(0b00100000);
    STQ_zp(cpu.A, 0x01);
    LDQ_zp(cpu.A, 0x02);
    AND_imm(0x01);
    ASL_acc();
    ASL_acc();
    ORA_zp(0x01);
    STQ_zp(cpu.A, 0x01);
    PLA();
    AND_imm(0b11100000);
    CLC();
    ADC_zp(0x00);
    STQ_zp(cpu.A, 0x00);
    LDQ_abs_xy(cpu.A, Enemy_Y_Position, cpu.Y);
    CMPQ_imm(cpu.A, 0xe8);
    if (BCC()) goto ExPRp;
    LDQ_zp(cpu.A, 0x00);
    AND_imm(0b10111111);
    STQ_zp(cpu.A, 0x00);
ExPRp:
    RET();

InitPlatformFall:
    CopyYA();
    CopyAX();
    CALL(GetEnemyOffscreenBits);
    LDQ_imm(cpu.A, 0x06);
    CALL(SetupFloateyNumber);
    LDQ_abs(cpu.A, Player_Rel_XPos);
    STA_abs_xy(FloateyNum_X_Pos, cpu.X);
    LDQ_zp(cpu.A, Player_Y_Position);
    STA_abs_xy(FloateyNum_Y_Pos, cpu.X);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Enemy_MovingDir, cpu.X);

StopPlatforms:
    CALL(InitVStf);
    STA_abs_xy(Enemy_Y_Speed, cpu.Y);
    STA_abs_xy(Enemy_Y_MoveForce, cpu.Y);
    RET();

PlatformFall:
    CopyYA();
    PHA();
    CALL(MoveFallingPlatform);
    PLA();
    CopyAX();
    CALL(MoveFallingPlatform);
    LDQ_zp(cpu.X, ObjectOffset);
    LDQ_abs_xy(cpu.A, PlatformCollisionFlag, cpu.X);
    if (BMI()) goto ExPF;
    CopyAX();
    CALL(PositionPlayerOnVPlat);
ExPF:
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

YMovingPlatform:
    LDQ_zp_xy(cpu.A, Enemy_Y_Speed, cpu.X);
    ORA_abs_xy(Enemy_Y_MoveForce, cpu.X);
    if (BNE()) goto ChkYCenterPos;
    STA_abs_xy(Enemy_YMF_Dummy, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CMPA_abs_xy(YPlatformTopYPos, cpu.X);
    if (BCS()) goto ChkYCenterPos;
    LDQ_zp(cpu.A, FrameCounter);
    AND_imm(0b00000111);
    if (BNE()) goto SkipIY;
    INC_zp_x(Enemy_Y_Position);
SkipIY:
    
    goto ChkYPCollision;

ChkYCenterPos:
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CMPA_zp_xy(YPlatformCenterYPos, cpu.X);
    if (BCC()) goto YMDown;
    CALL(MovePlatformUp);
    
    goto ChkYPCollision;
YMDown:
    CALL(MovePlatformDown);

ChkYPCollision:
    LDQ_abs_xy(cpu.A, PlatformCollisionFlag, cpu.X);
    if (BMI()) goto ExYPl;
    CALL(PositionPlayerOnVPlat);
ExYPl:
    RET();

XMovingPlatform:
    LDQ_imm(cpu.A, 0x0e);
    CALL(XMoveCntr_Platform);
    CALL(MoveWithXMCntrs);
    LDQ_abs_xy(cpu.A, PlatformCollisionFlag, cpu.X);
    if (BMI()) goto ExXMP;

PositionPlayerOnHPlat:
    LDQ_zp(cpu.A, Player_X_Position);
    CLC();
    ADC_zp(0x00);
    STQ_zp(cpu.A, Player_X_Position);
    LDQ_zp(cpu.A, Player_PageLoc);
    LDQ_zp(cpu.Y, 0x00);
    if (BMI()) goto PPHSubt;
    ADC_imm(0x00);
    
    goto SetPVar;
PPHSubt:
    SBC_imm(0x00);
SetPVar:
    STQ_zp(cpu.A, Player_PageLoc);
    STQ_abs(cpu.Y, Platform_X_Scroll);
    CALL(PositionPlayerOnVPlat);
ExXMP:
    RET();

DropPlatform:
    LDQ_abs_xy(cpu.A, PlatformCollisionFlag, cpu.X);
    if (BMI()) goto ExDPl;
    CALL(MoveDropPlatform);
    CALL(PositionPlayerOnVPlat);
ExDPl:
    RET();

RightPlatform:
    CALL(MoveEnemyHorizontally);
    STQ_zp(cpu.A, 0x00);
    LDQ_abs_xy(cpu.A, PlatformCollisionFlag, cpu.X);
    if (BMI()) goto ExRPl;
    LDQ_imm(cpu.A, 0x10);
    STQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
    CALL(PositionPlayerOnHPlat);
ExRPl:
    RET();

MoveLargeLiftPlat:
    CALL(MoveLiftPlatforms);
    
    goto ChkYPCollision;

MoveSmallPlatform:
    CALL(MoveLiftPlatforms);
    
    goto ChkSmallPlatCollision;

MoveLiftPlatforms:
    LDQ_abs(cpu.A, TimerControl);
    if (BNE()) goto ExLiftP;
    LDQ_abs_xy(cpu.A, Enemy_YMF_Dummy, cpu.X);
    CLC();
    ADC_abs_x(Enemy_Y_MoveForce);
    STA_abs_xy(Enemy_YMF_Dummy, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    ADC_zp_x(Enemy_Y_Speed);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    RET();

ChkSmallPlatCollision:
    LDQ_abs_xy(cpu.A, PlatformCollisionFlag, cpu.X);
    if (BEQ()) goto ExLiftP;
    CALL(PositionPlayerOnS_Plat);
ExLiftP:
    RET();

OffscreenBoundsCheck:
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, FlyingCheepCheep);
    if (BEQ()) goto ExScrnBd;
    LDQ_abs(cpu.A, ScreenLeft_X_Pos);
    LDQ_zp_xy(cpu.Y, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.Y, HammerBro);
    if (BEQ()) goto LimitB;
    CMPQ_imm(cpu.Y, PiranhaPlant);
    if (BNE()) goto ExtendLB;

LimitB:
    ADC_imm(0x38);
ExtendLB:
    SBC_imm(0x48);
    STQ_zp(cpu.A, 0x01);
    LDQ_abs(cpu.A, ScreenLeft_PageLoc);
    SBC_imm(0x00);
    STQ_zp(cpu.A, 0x00);
    LDQ_abs(cpu.A, ScreenRight_X_Pos);
    ADC_imm(0x48);
    STQ_zp(cpu.A, 0x03);
    LDQ_abs(cpu.A, ScreenRight_PageLoc);
    ADC_imm(0x00);
    STQ_zp(cpu.A, 0x02);
    LDQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    CMPQ_zp(cpu.A, 0x01);
    LDQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    SBC_zp(0x00);
    if (BMI()) goto TooFar;
    LDQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    CMPQ_zp(cpu.A, 0x03);
    LDQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    SBC_zp(0x02);
    if (BMI()) goto ExScrnBd;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    CMPQ_imm(cpu.A, HammerBro);
    if (BEQ()) goto ExScrnBd;
    CMPQ_imm(cpu.Y, PiranhaPlant);
    if (BEQ()) goto ExScrnBd;
    CMPQ_imm(cpu.Y, FlagpoleFlagObject);
    if (BEQ()) goto ExScrnBd;
    CMPQ_imm(cpu.Y, StarFlagObject);
    if (BEQ()) goto ExScrnBd;
    CMPQ_imm(cpu.Y, JumpspringObject);
    if (BEQ()) goto ExScrnBd;

TooFar:
    CALL(EraseEnemyObject);
ExScrnBd:
    RET();

FireballEnemyCollision:
    LDQ_zp_xy(cpu.A, Fireball_State, cpu.X);
    if (BEQ()) goto ExitFBallEnemy;
    ASL_acc();
    if (BCS()) goto ExitFBallEnemy;
    LDQ_zp(cpu.A, FrameCounter);
    LSR_acc();
    if (BCS()) goto ExitFBallEnemy;
    CopyXA();
    ASL_acc();
    ASL_acc();
    CLC();
    ADC_imm(0x1c);
    CopyAY();
    LDQ_imm(cpu.X, 0x04);

FireballEnemyCDLoop:
    STQ_zp(cpu.X, 0x01);
    CopyYA();
    PHA();
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b00100000);
    if (BNE()) goto NoFToECol;
    LDQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    if (BEQ()) goto NoFToECol;
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, 0x24);
    if (BCC()) goto GoombaDie;
    CMPQ_imm(cpu.A, 0x2b);
    if (BCC()) goto NoFToECol;

GoombaDie:
    CMPQ_imm(cpu.A, Goomba);
    if (BNE()) goto NotGoomba;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    CMPQ_imm(cpu.A, 0x02);
    if (BCS()) goto NoFToECol;

NotGoomba:
    LDQ_abs_xy(cpu.A, EnemyOffscrBitsMasked, cpu.X);
    if (BNE()) goto NoFToECol;
    CopyXA();
    ASL_acc();
    ASL_acc();
    CLC();
    ADC_imm(0x04);
    CopyAX();
    CALL(SprObjectCollisionCore);
    LDQ_zp(cpu.X, ObjectOffset);
    if (BCC()) goto NoFToECol;
    LDQ_imm(cpu.A, 0b10000000);
    STQ_zp_xy(cpu.A, Fireball_State, cpu.X);
    LDQ_zp(cpu.X, 0x01);
    CALL(HandleEnemyFBallCol);
NoFToECol:
    PLA();
    CopyAY();
    LDQ_zp(cpu.X, 0x01);
    DEX();
    if (BPL()) goto FireballEnemyCDLoop;

ExitFBallEnemy:
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

HandleEnemyFBallCol:
    CALL(RelativeEnemyPosition);
    LDQ_zp(cpu.X, 0x01);
    LDQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    if (BPL()) goto ChkBuzzyBeetle;
    AND_imm(0b00001111);
    CopyAX();
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, Bowser);
    if (BEQ()) goto HurtBowser;
    LDQ_zp(cpu.X, 0x01);

ChkBuzzyBeetle:
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, BuzzyBeetle);
    if (BEQ()) goto ExHCF;
    CMPQ_imm(cpu.A, Bowser);
    if (BNE()) goto ChkOtherEnemies;

HurtBowser:
    DEC_abs(BowserHitPoints);
    if (BNE()) goto ExHCF;
    CALL(InitVStf);
    STQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
    STQ_abs(cpu.A, EnemyFrenzyBuffer);
    LDQ_imm(cpu.A, 0xfe);
    STQ_zp_xy(cpu.A, Enemy_Y_Speed, cpu.X);
    LDQ_abs(cpu.Y, WorldNumber);
    LDQ_PRGTable(cpu.A, BowserIdentities, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    LDQ_imm(cpu.A, 0x20);
    CMPQ_imm(cpu.Y, 0x03);
    if (BCS()) goto SetDBSte;
    ORA_imm(0x03);
SetDBSte:
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    LDQ_imm(cpu.A, Sfx_BowserFall);
    STQ_zp(cpu.A, Square2SoundQueue);
    LDQ_zp(cpu.X, 0x01);
    LDQ_imm(cpu.A, 0x09);
    if (BNE()) goto EnemySmackScore;

ChkOtherEnemies:
    CMPQ_imm(cpu.A, BulletBill_FrenzyVar);
    if (BEQ()) goto ExHCF;
    CMPQ_imm(cpu.A, Podoboo);
    if (BEQ()) goto ExHCF;
    CMPQ_imm(cpu.A, 0x15);
    if (BCS()) goto ExHCF;

ShellOrBlockDefeat:
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, PiranhaPlant);
    if (BNE()) goto StnE;
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    ADC_imm(0x18);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
StnE:
    CALL(ChkToStunEnemies);
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b00011111);
    ORA_imm(0b00100000);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    LDQ_imm(cpu.A, 0x02);
    LDQ_zp_xy(cpu.Y, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.Y, HammerBro);
    if (BNE()) goto GoombaPoints;
    LDQ_imm(cpu.A, 0x06);

GoombaPoints:
    CMPQ_imm(cpu.Y, Goomba);
    if (BNE()) goto EnemySmackScore;
    LDQ_imm(cpu.A, 0x01);

EnemySmackScore:
    CALL(SetupFloateyNumber);
    LDQ_imm(cpu.A, Sfx_EnemySmack);
    STQ_zp(cpu.A, Square1SoundQueue);
ExHCF:
    RET();

PlayerHammerCollision:
    LDQ_zp(cpu.A, FrameCounter);
    LSR_acc();
    if (BCC()) goto ExPHC;
    LDQ_abs(cpu.A, TimerControl);
    ORA_abs(Misc_OffscreenBits);
    if (BNE()) goto ExPHC;
    CopyXA();
    ASL_acc();
    ASL_acc();
    CLC();
    ADC_imm(0x24);
    CopyAY();
    CALL(PlayerCollisionCore);
    LDQ_zp(cpu.X, ObjectOffset);
    if (BCC()) goto ClHCol;
    LDQ_abs_xy(cpu.A, Misc_Collision_Flag, cpu.X);
    if (BNE()) goto ExPHC;
    LDQ_imm(cpu.A, 0x01);
    STA_abs_xy(Misc_Collision_Flag, cpu.X);
    LDQ_zp_xy(cpu.A, Misc_X_Speed, cpu.X);
    EOR_imm(0xff);
    CLC();
    ADC_imm(0x01);
    STQ_zp_xy(cpu.A, Misc_X_Speed, cpu.X);
    LDQ_abs(cpu.A, StarInvincibleTimer);
    if (BNE()) goto ExPHC;
    
    goto InjurePlayer;
ClHCol:
    LDQ_imm(cpu.A, 0x00);
    STA_abs_xy(Misc_Collision_Flag, cpu.X);
ExPHC:
    RET();

HandlePowerUpCollision:
    CALL(EraseEnemyObject);
    LDQ_imm(cpu.A, 0x06);
    CALL(SetupFloateyNumber);
    LDQ_imm(cpu.A, Sfx_PowerUpGrab);
    STQ_zp(cpu.A, Square2SoundQueue);
    LDQ_zp(cpu.A, PowerUpType);
    CMPQ_imm(cpu.A, 0x02);
    if (BCC()) goto Shroom_Flower_PUp;
    CMPQ_imm(cpu.A, 0x03);
    if (BEQ()) goto SetFor1Up;
    LDQ_imm(cpu.A, 0x23);
    STQ_abs(cpu.A, StarInvincibleTimer);
    LDQ_imm(cpu.A, StarPowerMusic);
    STQ_zp(cpu.A, AreaMusicQueue);
    RET();

Shroom_Flower_PUp:
    LDQ_abs(cpu.A, PlayerStatus);
    if (BEQ()) goto UpToSuper;
    CMPQ_imm(cpu.A, 0x01);
    if (BNE()) goto NoPUp;
    LDQ_zp(cpu.X, ObjectOffset);
    LDQ_imm(cpu.A, 0x02);
    STQ_abs(cpu.A, PlayerStatus);
    CALL(GetPlayerColors);
    LDQ_zp(cpu.X, ObjectOffset);
    LDQ_imm(cpu.A, 0x0c);
    
    goto UpToFiery;

SetFor1Up:
    LDQ_imm(cpu.A, 0x0b);
    STA_abs_xy(FloateyNum_Control, cpu.X);
    RET();

UpToSuper:
    LDQ_imm(cpu.A, 0x01);
    STQ_abs(cpu.A, PlayerStatus);
    LDQ_imm(cpu.A, 0x09);

UpToFiery:
    LDQ_imm(cpu.Y, 0x00);
    CALL(SetPRout);
NoPUp:
    RET();

PlayerEnemyCollision:
    LDQ_zp(cpu.A, FrameCounter);
    LSR_acc();
    if (BCS()) goto NoPUp;
    CALL(CheckPlayerVertical);
    if (BCS()) goto NoPECol;
    LDQ_abs_xy(cpu.A, EnemyOffscrBitsMasked, cpu.X);
    if (BNE()) goto NoPECol;
    LDQ_zp(cpu.A, GameEngineSubroutine);
    CMPQ_imm(cpu.A, 0x08);
    if (BNE()) goto NoPECol;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b00100000);
    if (BNE()) goto NoPECol;
    CALL(GetEnemyBoundBoxOfs);
    CALL(PlayerCollisionCore);
    LDQ_zp(cpu.X, ObjectOffset);
    if (BCS()) goto CheckForPUpCollision;
    LDQ_abs_xy(cpu.A, Enemy_CollisionBits, cpu.X);
    AND_imm(0b11111110);
    STA_abs_xy(Enemy_CollisionBits, cpu.X);
NoPECol:
    RET();

CheckForPUpCollision:
    LDQ_zp_xy(cpu.Y, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.Y, PowerUpObject);
    if (BNE()) goto EColl;
    
    goto HandlePowerUpCollision;
EColl:
    LDQ_abs(cpu.A, StarInvincibleTimer);
    if (BEQ()) goto HandlePECollisions;
    
    goto ShellOrBlockDefeat;

HandlePECollisions:
    LDQ_abs_xy(cpu.A, Enemy_CollisionBits, cpu.X);
    AND_imm(0b00000001);
    ORA_abs_xy(EnemyOffscrBitsMasked, cpu.X);
    if (BNE()) goto ExPEC;
    LDQ_imm(cpu.A, 0x01);
    ORA_abs_xy(Enemy_CollisionBits, cpu.X);
    STA_abs_xy(Enemy_CollisionBits, cpu.X);
    CMPQ_imm(cpu.Y, Spiny);
    if (BEQ()) goto ChkForPlayerInjury;
    CMPQ_imm(cpu.Y, PiranhaPlant);
    if (BEQ()) goto InjurePlayer;
    CMPQ_imm(cpu.Y, Podoboo);
    if (BEQ()) goto InjurePlayer;
    CMPQ_imm(cpu.Y, BulletBill_CannonVar);
    if (BEQ()) goto ChkForPlayerInjury;
    CMPQ_imm(cpu.Y, 0x15);
    if (BCS()) goto InjurePlayer;
    LDQ_abs(cpu.A, AreaType);
    if (BEQ()) goto InjurePlayer;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    ASL_acc();
    if (BCS()) goto ChkForPlayerInjury;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b00000111);
    CMPQ_imm(cpu.A, 0x02);
    if (BCC()) goto ChkForPlayerInjury;
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, Goomba);
    if (BEQ()) goto ExPEC;
    LDQ_imm(cpu.A, Sfx_EnemySmack);
    STQ_zp(cpu.A, Square1SoundQueue);
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    ORA_imm(0b10000000);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    CALL(EnemyFacePlayer);
    LDQ_PRGTable(cpu.A, KickedShellXSpdData, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
    LDQ_imm(cpu.A, 0x03);
    CLC();
    ADC_abs(StompChainCounter);
    LDQ_abs_xy(cpu.Y, EnemyIntervalTimer, cpu.X);
    CMPQ_imm(cpu.Y, 0x03);
    if (BCS()) goto KSPts;
    LDQ_PRGTable(cpu.A, KickedShellPtsData, cpu.Y);
KSPts:
    CALL(SetupFloateyNumber);
ExPEC:
    RET();

ChkForPlayerInjury:
    LDQ_zp(cpu.A, Player_Y_Speed);
    if (BMI()) goto ChkInj;
    if (BNE()) goto EnemyStomped;

ChkInj:
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, Bloober);
    if (BCC()) goto ChkETmrs;
    LDQ_zp(cpu.A, Player_Y_Position);
    CLC();
    ADC_imm(0x0c);
    CMPA_zp_xy(Enemy_Y_Position, cpu.X);
    if (BCC()) goto EnemyStomped;

ChkETmrs:
    LDQ_abs(cpu.A, StompTimer);
    if (BNE()) goto EnemyStomped;
    LDQ_abs(cpu.A, InjuryTimer);
    if (BNE()) goto ExInjColRoutines;
    LDQ_abs(cpu.A, Player_Rel_XPos);
    CMPQ_abs(cpu.A, Enemy_Rel_XPos);
    if (BCC()) goto TInjE;
    
    goto ChkEnemyFaceRight;
TInjE:
    LDQ_zp_xy(cpu.A, Enemy_MovingDir, cpu.X);
    CMPQ_imm(cpu.A, 0x01);
    if (BNE()) goto InjurePlayer;
    
    goto LInj;

InjurePlayer:
    LDQ_abs(cpu.A, InjuryTimer);
    if (BNE()) goto ExInjColRoutines;

ForceInjury:
    LDQ_abs(cpu.X, PlayerStatus);
    if (BEQ()) goto KillPlayer;
    STQ_abs(cpu.A, PlayerStatus);
    LDQ_imm(cpu.A, 0x08);
    STQ_abs(cpu.A, InjuryTimer);
    ASL_acc();
    STQ_zp(cpu.A, Square1SoundQueue);
    CALL(GetPlayerColors);
    LDQ_imm(cpu.A, 0x0a);
SetKRout:
    LDQ_imm(cpu.Y, 0x01);
SetPRout:
    STQ_zp(cpu.A, GameEngineSubroutine);
    STQ_zp(cpu.Y, Player_State);
    LDQ_imm(cpu.Y, 0xff);
    STQ_abs(cpu.Y, TimerControl);
    INY();
    STQ_abs(cpu.Y, ScrollAmount);

ExInjColRoutines:
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

KillPlayer:
    STQ_zp(cpu.X, Player_X_Speed);
    INX();
    STQ_zp(cpu.X, EventMusicQueue);
    LDQ_imm(cpu.A, 0xfc);
    STQ_zp(cpu.A, Player_Y_Speed);
    LDQ_imm(cpu.A, 0x0b);
    if (BNE()) goto SetKRout;

EnemyStomped:
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, Spiny);
    if (BEQ()) goto InjurePlayer;
    LDQ_imm(cpu.A, Sfx_EnemyStomp);
    STQ_zp(cpu.A, Square1SoundQueue);
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    LDQ_imm(cpu.Y, 0x00);
    CMPQ_imm(cpu.A, FlyingCheepCheep);
    if (BEQ()) goto EnemyStompedPts;
    CMPQ_imm(cpu.A, BulletBill_FrenzyVar);
    if (BEQ()) goto EnemyStompedPts;
    CMPQ_imm(cpu.A, BulletBill_CannonVar);
    if (BEQ()) goto EnemyStompedPts;
    CMPQ_imm(cpu.A, Podoboo);
    if (BEQ()) goto EnemyStompedPts;
    INY();
    CMPQ_imm(cpu.A, HammerBro);
    if (BEQ()) goto EnemyStompedPts;
    INY();
    CMPQ_imm(cpu.A, Lakitu);
    if (BEQ()) goto EnemyStompedPts;
    INY();
    CMPQ_imm(cpu.A, Bloober);
    if (BNE()) goto ChkForDemoteKoopa;

EnemyStompedPts:
    LDQ_PRGTable(cpu.A, StompedEnemyPtsData, cpu.Y);
    CALL(SetupFloateyNumber);
    LDQ_zp_xy(cpu.A, Enemy_MovingDir, cpu.X);
    PHA();
    CALL(SetStun);
    PLA();
    STQ_zp_xy(cpu.A, Enemy_MovingDir, cpu.X);
    LDQ_imm(cpu.A, 0b00100000);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    CALL(InitVStf);
    STQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
    LDQ_imm(cpu.A, 0xfd);
    STQ_zp(cpu.A, Player_Y_Speed);
    RET();

ChkForDemoteKoopa:
    CMPQ_imm(cpu.A, 0x09);
    if (BCC()) goto HandleStompedShellE;
    AND_imm(0b00000001);
    STQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    LDQ_imm(cpu.Y, 0x00);
    STQ_zp_xy(cpu.Y, Enemy_State, cpu.X);
    LDQ_imm(cpu.A, 0x03);
    CALL(SetupFloateyNumber);
    CALL(InitVStf);
    CALL(EnemyFacePlayer);
    LDQ_PRGTable(cpu.A, DemotedKoopaXSpdData, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
    
    goto SBnce;

HandleStompedShellE:
    LDQ_imm(cpu.A, 0x04);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    INC_abs(StompChainCounter);
    LDQ_abs(cpu.A, StompChainCounter);
    CLC();
    ADC_abs(StompTimer);
    CALL(SetupFloateyNumber);
    INC_abs(StompTimer);
    LDQ_abs(cpu.Y, PrimaryHardMode);
    LDQ_PRGTable(cpu.A, RevivalRateData, cpu.Y);
    STA_abs_xy(EnemyIntervalTimer, cpu.X);
SBnce:
    LDQ_imm(cpu.A, 0xfc);
    STQ_zp(cpu.A, Player_Y_Speed);
    RET();

ChkEnemyFaceRight:
    LDQ_zp_xy(cpu.A, Enemy_MovingDir, cpu.X);
    CMPQ_imm(cpu.A, 0x01);
    if (BNE()) goto LInj;
    
    goto InjurePlayer;
LInj:
    CALL(EnemyTurnAround);
    
    goto InjurePlayer;

EnemyFacePlayer:
    LDQ_imm(cpu.Y, 0x01);
    CALL(PlayerEnemyDiff);
    if (BPL()) goto SFcRt;
    INY();
SFcRt:
    STQ_zp_xy(cpu.Y, Enemy_MovingDir, cpu.X);
    DEY();
    RET();

SetupFloateyNumber:
    STA_abs_xy(FloateyNum_Control, cpu.X);
    LDQ_imm(cpu.A, 0x30);
    STA_abs_xy(FloateyNum_Timer, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    STA_abs_xy(FloateyNum_Y_Pos, cpu.X);
    LDQ_abs(cpu.A, Enemy_Rel_XPos);
    STA_abs_xy(FloateyNum_X_Pos, cpu.X);
ExSFN:
    RET();

EnemiesCollision:
    LDQ_zp(cpu.A, FrameCounter);
    LSR_acc();
    if (BCC()) goto ExSFN;
    LDQ_abs(cpu.A, AreaType);
    if (BEQ()) goto ExSFN;
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, 0x15);
    if (BCS()) goto ExitECRoutine;
    CMPQ_imm(cpu.A, Lakitu);
    if (BEQ()) goto ExitECRoutine;
    CMPQ_imm(cpu.A, PiranhaPlant);
    if (BEQ()) goto ExitECRoutine;
    LDQ_abs_xy(cpu.A, EnemyOffscrBitsMasked, cpu.X);
    if (BNE()) goto ExitECRoutine;
    CALL(GetEnemyBoundBoxOfs);
    DEX();
    if (BMI()) goto ExitECRoutine;

ECLoop:
    STQ_zp(cpu.X, 0x01);
    CopyYA();
    PHA();
    LDQ_zp_xy(cpu.A, Enemy_Flag, cpu.X);
    if (BEQ()) goto ReadyNextEnemy;
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, 0x15);
    if (BCS()) goto ReadyNextEnemy;
    CMPQ_imm(cpu.A, Lakitu);
    if (BEQ()) goto ReadyNextEnemy;
    CMPQ_imm(cpu.A, PiranhaPlant);
    if (BEQ()) goto ReadyNextEnemy;
    LDQ_abs_xy(cpu.A, EnemyOffscrBitsMasked, cpu.X);
    if (BNE()) goto ReadyNextEnemy;
    CopyXA();
    ASL_acc();
    ASL_acc();
    CLC();
    ADC_imm(0x04);
    CopyAX();
    CALL(SprObjectCollisionCore);
    LDQ_zp(cpu.X, ObjectOffset);
    LDQ_zp(cpu.Y, 0x01);
    if (BCC()) goto NoEnemyCollision;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    ORA_abs_xy(Enemy_State, cpu.Y);
    AND_imm(0b10000000);
    if (BNE()) goto YesEC;
    LDQ_abs_xy(cpu.A, Enemy_CollisionBits, cpu.Y);
    AND_PRGTable(SetBitsMask, cpu.X);
    if (BNE()) goto ReadyNextEnemy;
    LDQ_abs_xy(cpu.A, Enemy_CollisionBits, cpu.Y);
    ORA_PRGTable(SetBitsMask, cpu.X);
    STA_abs_xy(Enemy_CollisionBits, cpu.Y);
YesEC:
    CALL(ProcEnemyCollisions);
    
    goto ReadyNextEnemy;

NoEnemyCollision:
    LDQ_abs_xy(cpu.A, Enemy_CollisionBits, cpu.Y);
    AND_PRGTable(ClearBitsMask, cpu.X);
    STA_abs_xy(Enemy_CollisionBits, cpu.Y);

ReadyNextEnemy:
    PLA();
    CopyAY();
    LDQ_zp(cpu.X, 0x01);
    DEX();
    if (BPL()) goto ECLoop;

ExitECRoutine:
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

ProcEnemyCollisions:
    LDQ_abs_xy(cpu.A, Enemy_State, cpu.Y);
    ORA_zp_xy(Enemy_State, cpu.X);
    AND_imm(0b00100000);
    if (BNE()) goto ExitProcessEColl;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    CMPQ_imm(cpu.A, 0x06);
    if (BCC()) goto ProcSecondEnemyColl;
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, HammerBro);
    if (BEQ()) goto ExitProcessEColl;
    LDQ_abs_xy(cpu.A, Enemy_State, cpu.Y);
    ASL_acc();
    if (BCC()) goto ShellCollisions;
    LDQ_imm(cpu.A, 0x06);
    CALL(SetupFloateyNumber);
    CALL(ShellOrBlockDefeat);
    LDQ_zp(cpu.Y, 0x01);

ShellCollisions:
    CopyYA();
    CopyAX();
    CALL(ShellOrBlockDefeat);
    LDQ_zp(cpu.X, ObjectOffset);
    LDQ_abs_xy(cpu.A, ShellChainCounter, cpu.X);
    CLC();
    ADC_imm(0x04);
    LDQ_zp(cpu.X, 0x01);
    CALL(SetupFloateyNumber);
    LDQ_zp(cpu.X, ObjectOffset);
    INC_abs_x(ShellChainCounter);

ExitProcessEColl:
    RET();

ProcSecondEnemyColl:
    LDQ_abs_xy(cpu.A, Enemy_State, cpu.Y);
    CMPQ_imm(cpu.A, 0x06);
    if (BCC()) goto MoveEOfs;
    LDQ_abs_xy(cpu.A, Enemy_ID, cpu.Y);
    CMPQ_imm(cpu.A, HammerBro);
    if (BEQ()) goto ExitProcessEColl;
    CALL(ShellOrBlockDefeat);
    LDQ_zp(cpu.Y, 0x01);
    LDQ_abs_xy(cpu.A, ShellChainCounter, cpu.Y);
    CLC();
    ADC_imm(0x04);
    LDQ_zp(cpu.X, ObjectOffset);
    CALL(SetupFloateyNumber);
    LDQ_zp(cpu.X, 0x01);
    INC_abs_x(ShellChainCounter);
    RET();

MoveEOfs:
    CopyYA();
    CopyAX();
    CALL(EnemyTurnAround);
    LDQ_zp(cpu.X, ObjectOffset);

EnemyTurnAround:
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, PiranhaPlant);
    if (BEQ()) goto ExTA;
    CMPQ_imm(cpu.A, Lakitu);
    if (BEQ()) goto ExTA;
    CMPQ_imm(cpu.A, HammerBro);
    if (BEQ()) goto ExTA;
    CMPQ_imm(cpu.A, Spiny);
    if (BEQ()) goto RXSpd;
    CMPQ_imm(cpu.A, GreenParatroopaJump);
    if (BEQ()) goto RXSpd;
    CMPQ_imm(cpu.A, 0x07);
    if (BCS()) goto ExTA;

RXSpd:
    LDQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
    EOR_imm(0xff);
    CopyAY();
    INY();
    STQ_zp_xy(cpu.Y, Enemy_X_Speed, cpu.X);
    LDQ_zp_xy(cpu.A, Enemy_MovingDir, cpu.X);
    EOR_imm(0b00000011);
    STQ_zp_xy(cpu.A, Enemy_MovingDir, cpu.X);
ExTA:
    RET();

LargePlatformCollision:
    LDQ_imm(cpu.A, 0xff);
    STA_abs_xy(PlatformCollisionFlag, cpu.X);
    LDQ_abs(cpu.A, TimerControl);
    if (BNE()) goto ExLPC;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    if (BMI()) goto ExLPC;
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, 0x24);
    if (BNE()) goto ChkForPlayerC_LargeP;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    CopyAX();
    CALL(ChkForPlayerC_LargeP);

ChkForPlayerC_LargeP:
    CALL(CheckPlayerVertical);
    if (BCS()) goto ExLPC;
    CopyXA();
    CALL(GetEnemyBoundBoxOfsArg);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    STQ_zp(cpu.A, 0x00);
    CopyXA();
    PHA();
    CALL(PlayerCollisionCore);
    PLA();
    CopyAX();
    if (BCC()) goto ExLPC;
    CALL(ProcLPlatCollisions);
ExLPC:
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

SmallPlatformCollision:
    LDQ_abs(cpu.A, TimerControl);
    if (BNE()) goto ExSPC;
    STA_abs_xy(PlatformCollisionFlag, cpu.X);
    CALL(CheckPlayerVertical);
    if (BCS()) goto ExSPC;
    LDQ_imm(cpu.A, 0x02);
    STQ_zp(cpu.A, 0x00);

ChkSmallPlatLoop:
    LDQ_zp(cpu.X, ObjectOffset);
    CALL(GetEnemyBoundBoxOfs);
    AND_imm(0b00000010);
    if (BNE()) goto ExSPC;
    LDQ_abs_xy(cpu.A, BoundingBox_UL_YPos, cpu.Y);
    CMPQ_imm(cpu.A, 0x20);
    if (BCC()) goto MoveBoundBox;
    CALL(PlayerCollisionCore);
    if (BCS()) goto ProcSPlatCollisions;

MoveBoundBox:
    LDQ_abs_xy(cpu.A, BoundingBox_UL_YPos, cpu.Y);
    CLC();
    ADC_imm(0x80);
    STA_abs_xy(BoundingBox_UL_YPos, cpu.Y);
    LDQ_abs_xy(cpu.A, BoundingBox_DR_YPos, cpu.Y);
    CLC();
    ADC_imm(0x80);
    STA_abs_xy(BoundingBox_DR_YPos, cpu.Y);
    DEC_zp(0x00);
    if (BNE()) goto ChkSmallPlatLoop;

ExSPC:
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

ProcSPlatCollisions:
    LDQ_zp(cpu.X, ObjectOffset);

ProcLPlatCollisions:
    LDQ_abs_xy(cpu.A, BoundingBox_DR_YPos, cpu.Y);
    SEC();
    SBC_abs(BoundingBox_UL_YPos);
    CMPQ_imm(cpu.A, 0x04);
    if (BCS()) goto ChkForTopCollision;
    LDQ_zp(cpu.A, Player_Y_Speed);
    if (BPL()) goto ChkForTopCollision;
    LDQ_imm(cpu.A, 0x01);
    STQ_zp(cpu.A, Player_Y_Speed);

ChkForTopCollision:
    LDQ_abs(cpu.A, BoundingBox_DR_YPos);
    SEC();
    SBC_abs_y(BoundingBox_UL_YPos);
    CMPQ_imm(cpu.A, 0x06);
    if (BCS()) goto PlatformSideCollisions;
    LDQ_zp(cpu.A, Player_Y_Speed);
    if (BMI()) goto PlatformSideCollisions;
    LDQ_zp(cpu.A, 0x00);
    LDQ_zp_xy(cpu.Y, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.Y, 0x2b);
    if (BEQ()) goto SetCollisionFlag;
    CMPQ_imm(cpu.Y, 0x2c);
    if (BEQ()) goto SetCollisionFlag;
    CopyXA();

SetCollisionFlag:
    LDQ_zp(cpu.X, ObjectOffset);
    STA_abs_xy(PlatformCollisionFlag, cpu.X);
    LDQ_imm(cpu.A, 0x00);
    STQ_zp(cpu.A, Player_State);
    RET();

PlatformSideCollisions:
    LDQ_imm(cpu.A, 0x01);
    STQ_zp(cpu.A, 0x00);
    LDQ_abs(cpu.A, BoundingBox_DR_XPos);
    SEC();
    SBC_abs_y(BoundingBox_UL_XPos);
    CMPQ_imm(cpu.A, 0x08);
    if (BCC()) goto SideC;
    INC_zp(0x00);
    LDQ_abs_xy(cpu.A, BoundingBox_DR_XPos, cpu.Y);
    CLC();
    SBC_abs(BoundingBox_UL_XPos);
    CMPQ_imm(cpu.A, 0x09);
    if (BCS()) goto NoSideC;

SideC:
    CALL(ImpedePlayerMove);
NoSideC:
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

PositionPlayerOnS_Plat:
    CopyAY();
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CLC();
    ADC_PRGTable(PlayerPosSPlatData, cpu.Y - 1);
    bitFlag = true;

PositionPlayerOnVPlat:
    if (!bitFlag)
        LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    else
        bitFlag = false;
    
    LDQ_zp(cpu.Y, GameEngineSubroutine);
    CMPQ_imm(cpu.Y, 0x0b);
    if (BEQ()) goto ExPlPos;
    LDQ_zp_xy(cpu.Y, Enemy_Y_HighPos, cpu.X);
    CMPQ_imm(cpu.Y, 0x01);
    if (BNE()) goto ExPlPos;
    SEC();
    SBC_imm(0x20);
    STQ_zp(cpu.A, Player_Y_Position);
    CopyYA();
    SBC_imm(0x00);
    STQ_zp(cpu.A, Player_Y_HighPos);
    LDQ_imm(cpu.A, 0x00);
    STQ_zp(cpu.A, Player_Y_Speed);
    STQ_abs(cpu.A, Player_Y_MoveForce);
ExPlPos:
    RET();

CheckPlayerVertical:
    LDQ_abs(cpu.A, Player_OffscreenBits);
    CMPQ_imm(cpu.A, 0xf0);
    if (BCS()) goto ExCPV;
    LDQ_zp(cpu.Y, Player_Y_HighPos);
    DEY();
    if (BNE()) goto ExCPV;
    LDQ_zp(cpu.A, Player_Y_Position);
    CMPQ_imm(cpu.A, 0xd0);
ExCPV:
    RET();

GetEnemyBoundBoxOfs:
    LDQ_zp(cpu.A, ObjectOffset);

GetEnemyBoundBoxOfsArg:
    ASL_acc();
    ASL_acc();
    CLC();
    ADC_imm(0x04);
    CopyAY();
    LDQ_abs(cpu.A, Enemy_OffscreenBits);
    AND_imm(0b00001111);
    CMPQ_imm(cpu.A, 0b00001111);
    RET();

PlayerBGCollision:
    LDQ_abs(cpu.A, DisableCollisionDet);
    if (BNE()) goto ExPBGCol;
    LDQ_zp(cpu.A, GameEngineSubroutine);
    CMPQ_imm(cpu.A, 0x0b);
    if (BEQ()) goto ExPBGCol;
    CMPQ_imm(cpu.A, 0x04);
    if (BCC()) goto ExPBGCol;
    LDQ_imm(cpu.A, 0x01);
    LDQ_abs(cpu.Y, SwimmingFlag);
    if (BNE()) goto SetPSte;
    LDQ_zp(cpu.A, Player_State);
    if (BEQ()) goto SetFallS;
    CMPQ_imm(cpu.A, 0x03);
    if (BNE()) goto ChkOnScr;

SetFallS:
    LDQ_imm(cpu.A, 0x02);
SetPSte:
    STQ_zp(cpu.A, Player_State);
ChkOnScr:
    LDQ_zp(cpu.A, Player_Y_HighPos);
    CMPQ_imm(cpu.A, 0x01);
    if (BNE()) goto ExPBGCol;
    LDQ_imm(cpu.A, 0xff);
    STQ_abs(cpu.A, Player_CollisionBits);
    LDQ_zp(cpu.A, Player_Y_Position);
    CMPQ_imm(cpu.A, 0xcf);
    if (BCC()) goto ChkCollSize;

ExPBGCol:
    RET();

ChkCollSize:
    LDQ_imm(cpu.Y, 0x02);
    LDQ_abs(cpu.A, CrouchingFlag);
    if (BNE()) goto GBBAdr;
    LDQ_abs(cpu.A, PlayerSize);
    if (BNE()) goto GBBAdr;
    DEY();
    LDQ_abs(cpu.A, SwimmingFlag);
    if (BNE()) goto GBBAdr;
    DEY();
GBBAdr:
    LDQ_PRGTable(cpu.A, BlockBufferAdderData, cpu.Y);
    STQ_zp(cpu.A, 0xeb);
    CopyAY();
    LDQ_abs(cpu.X, PlayerSize);
    LDQ_abs(cpu.A, CrouchingFlag);
    if (BEQ()) goto HeadChk;
    INX();
HeadChk:
    LDQ_zp(cpu.A, Player_Y_Position);
    CMPA_PRGTable(PlayerBGUpperExtent, cpu.X);
    if (BCC()) goto DoFootCheck;
    CALL(BlockBufferColli_Head);
    if (BEQ()) goto DoFootCheck;
    CALL(CheckForCoinMTiles);
    if (BCS()) goto AwardTouchedCoin;
    LDQ_zp(cpu.Y, Player_Y_Speed);
    if (BPL()) goto DoFootCheck;
    LDQ_zp(cpu.Y, 0x04);
    CMPQ_imm(cpu.Y, 0x04);
    if (BCC()) goto DoFootCheck;
    CALL(CheckForSolidMTiles);
    if (BCS()) goto SolidOrClimb;
    LDQ_abs(cpu.Y, AreaType);
    if (BEQ()) goto NYSpd;
    LDQ_abs(cpu.Y, BlockBounceTimer);
    if (BNE()) goto NYSpd;
    CALL(PlayerHeadCollision);
    
    goto DoFootCheck;

SolidOrClimb:
    CMPQ_imm(cpu.A, 0x26);
    if (BEQ()) goto NYSpd;
    LDQ_imm(cpu.A, Sfx_Bump);
    STQ_zp(cpu.A, Square1SoundQueue);
NYSpd:
    LDQ_imm(cpu.A, 0x01);
    STQ_zp(cpu.A, Player_Y_Speed);

DoFootCheck:
    LDQ_zp(cpu.Y, 0xeb);
    LDQ_zp(cpu.A, Player_Y_Position);
    CMPQ_imm(cpu.A, 0xcf);
    if (BCS()) goto DoPlayerSideCheck;
    CALL(BlockBufferColli_Feet);
    CALL(CheckForCoinMTiles);
    if (BCS()) goto AwardTouchedCoin;
    PHA();
    CALL(BlockBufferColli_Feet);
    STQ_zp(cpu.A, 0x00);
    PLA();
    STQ_zp(cpu.A, 0x01);
    if (BNE()) goto ChkFootMTile;
    LDQ_zp(cpu.A, 0x00);
    if (BEQ()) goto DoPlayerSideCheck;
    CALL(CheckForCoinMTiles);
    if (BCC()) goto ChkFootMTile;

AwardTouchedCoin:
    goto HandleCoinMetatile;

ChkFootMTile:
    CALL(CheckForClimbMTiles);
    if (BCS()) goto DoPlayerSideCheck;
    LDQ_zp(cpu.Y, Player_Y_Speed);
    if (BMI()) goto DoPlayerSideCheck;
    CMPQ_imm(cpu.A, 0xc5);
    if (BNE()) goto ContChk;
    
    goto HandleAxeMetatile;
ContChk:
    CALL(ChkInvisibleMTiles);
    if (BEQ()) goto DoPlayerSideCheck;
    LDQ_abs(cpu.Y, JumpspringAnimCtrl);
    if (BNE()) goto InitSteP;
    LDQ_zp(cpu.Y, 0x04);
    CMPQ_imm(cpu.Y, 0x05);
    if (BCC()) goto LandPlyr;
    LDQ_zp(cpu.A, Player_MovingDir);
    STQ_zp(cpu.A, 0x00);
    
    goto ImpedePlayerMove;
LandPlyr:
    CALL(ChkForLandJumpSpring);
    LDQ_imm(cpu.A, 0xf0);
    AND_zp(Player_Y_Position);
    STQ_zp(cpu.A, Player_Y_Position);
    CALL(HandlePipeEntry);
    LDQ_imm(cpu.A, 0x00);
    STQ_zp(cpu.A, Player_Y_Speed);
    STQ_abs(cpu.A, Player_Y_MoveForce);
    STQ_abs(cpu.A, StompChainCounter);
InitSteP:
    LDQ_imm(cpu.A, 0x00);
    STQ_zp(cpu.A, Player_State);

DoPlayerSideCheck:
    LDQ_zp(cpu.Y, 0xeb);
    INY();
    INY();
    LDQ_imm(cpu.A, 0x02);
    STQ_zp(cpu.A, 0x00);

SideCheckLoop:
    INY();
    STQ_zp(cpu.Y, 0xeb);
    LDQ_zp(cpu.A, Player_Y_Position);
    CMPQ_imm(cpu.A, 0x20);
    if (BCC()) goto BHalf;
    CMPQ_imm(cpu.A, 0xe4);
    if (BCS()) goto ExSCH;
    CALL(BlockBufferColli_Side);
    if (BEQ()) goto BHalf;
    CMPQ_imm(cpu.A, 0x1c);
    if (BEQ()) goto BHalf;
    CMPQ_imm(cpu.A, 0x6b);
    if (BEQ()) goto BHalf;
    CALL(CheckForClimbMTiles);
    if (BCC()) goto CheckSideMTiles;

BHalf:
    LDQ_zp(cpu.Y, 0xeb);
    INY();
    LDQ_zp(cpu.A, Player_Y_Position);
    CMPQ_imm(cpu.A, 0x08);
    if (BCC()) goto ExSCH;
    CMPQ_imm(cpu.A, 0xd0);
    if (BCS()) goto ExSCH;
    CALL(BlockBufferColli_Side);
    if (BNE()) goto CheckSideMTiles;
    DEC_zp(0x00);
    if (BNE()) goto SideCheckLoop;

ExSCH:
    RET();

CheckSideMTiles:
    CALL(ChkInvisibleMTiles);
    if (BEQ()) goto ExCSM;
    CALL(CheckForClimbMTiles);
    if (BCC()) goto ContSChk;
    
    goto HandleClimbing;
ContSChk:
    CALL(CheckForCoinMTiles);
    if (BCS()) goto HandleCoinMetatile;
    CALL(ChkJumpspringMetatiles);
    if (BCC()) goto ChkPBtm;
    LDQ_abs(cpu.A, JumpspringAnimCtrl);
    if (BNE()) goto ExCSM;
    
    goto StopPlayerMove;
ChkPBtm:
    LDQ_zp(cpu.Y, Player_State);
    CMPQ_imm(cpu.Y, 0x00);
    if (BNE()) goto StopPlayerMove;
    LDQ_zp(cpu.Y, PlayerFacingDir);
    DEY();
    if (BNE()) goto StopPlayerMove;
    CMPQ_imm(cpu.A, 0x6c);
    if (BEQ()) goto PipeDwnS;
    CMPQ_imm(cpu.A, 0x1f);
    if (BNE()) goto StopPlayerMove;

PipeDwnS:
    LDQ_abs(cpu.A, Player_SprAttrib);
    if (BNE()) goto PlyrPipe;
    LDQ_imm(cpu.Y, Sfx_PipeDown_Injury);
    STQ_zp(cpu.Y, Square1SoundQueue);
PlyrPipe:
    ORA_imm(0b00100000);
    STQ_abs(cpu.A, Player_SprAttrib);
    LDQ_zp(cpu.A, Player_X_Position);
    AND_imm(0b00001111);
    if (BEQ()) goto ChkGERtn;
    LDQ_imm(cpu.Y, 0x00);
    LDQ_abs(cpu.A, ScreenLeft_PageLoc);
    if (BEQ()) goto SetCATmr;
    INY();
SetCATmr:
    LDQ_PRGTable(cpu.A, AreaChangeTimerData, cpu.Y);
    STQ_abs(cpu.A, ChangeAreaTimer);
ChkGERtn:
    LDQ_zp(cpu.A, GameEngineSubroutine);
    CMPQ_imm(cpu.A, 0x07);
    if (BEQ()) goto ExCSM;
    CMPQ_imm(cpu.A, 0x08);
    if (BNE()) goto ExCSM;
    LDQ_imm(cpu.A, 0x02);
    STQ_zp(cpu.A, GameEngineSubroutine);
    RET();

StopPlayerMove:
    CALL(ImpedePlayerMove);
ExCSM:
    RET();

HandleCoinMetatile:
    CALL(ErACM);
    INC_abs(CoinTallyFor1Ups);
    
    goto GiveOneCoin;

HandleAxeMetatile:
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, OperMode_Task);
    LDQ_imm(cpu.A, 0x02);
    STQ_abs(cpu.A, OperMode);
    LDQ_imm(cpu.A, 0x18);
    STQ_zp(cpu.A, Player_X_Speed);
ErACM:
    LDQ_zp(cpu.Y, 0x02);
    LDQ_imm(cpu.A, 0x00);
    STA_ind_y(0x06);
    
    goto RemoveCoin_Axe;

HandleClimbing:
    LDQ_zp(cpu.Y, 0x04);
    CMPQ_imm(cpu.Y, 0x06);
    if (BCC()) goto ExHC;
    CMPQ_imm(cpu.Y, 0x0a);
    if (BCC()) goto ChkForFlagpole;

ExHC:
    RET();

ChkForFlagpole:
    CMPQ_imm(cpu.A, 0x24);
    if (BEQ()) goto FlagpoleCollision;
    CMPQ_imm(cpu.A, 0x25);
    if (BNE()) goto VineCollision;

FlagpoleCollision:
    LDQ_zp(cpu.A, GameEngineSubroutine);
    CMPQ_imm(cpu.A, 0x05);
    if (BEQ()) goto PutPlayerOnVine;
    LDQ_imm(cpu.A, 0x01);
    STQ_zp(cpu.A, PlayerFacingDir);
    INC_abs(ScrollLock);
    LDQ_zp(cpu.A, GameEngineSubroutine);
    CMPQ_imm(cpu.A, 0x04);
    if (BEQ()) goto RunFR;
    LDQ_imm(cpu.A, BulletBill_CannonVar);
    CALL(KillEnemies);
    LDQ_imm(cpu.A, Silence);
    STQ_zp(cpu.A, EventMusicQueue);
    LSR_acc();
    STQ_abs(cpu.A, FlagpoleSoundQueue);
    LDQ_imm(cpu.X, 0x04);
    LDQ_zp(cpu.A, Player_Y_Position);
    STQ_abs(cpu.A, FlagpoleCollisionYPos);

ChkFlagpoleYPosLoop:
    CMPA_PRGTable(FlagpoleYPosData, cpu.X);
    if (BCS()) goto MtchF;
    DEX();
    if (BNE()) goto ChkFlagpoleYPosLoop;

MtchF:
    STQ_abs(cpu.X, FlagpoleScore);
RunFR:
    LDQ_imm(cpu.A, 0x04);
    STQ_zp(cpu.A, GameEngineSubroutine);
    
    goto PutPlayerOnVine;

VineCollision:
    CMPQ_imm(cpu.A, 0x26);
    if (BNE()) goto PutPlayerOnVine;
    LDQ_zp(cpu.A, Player_Y_Position);
    CMPQ_imm(cpu.A, 0x20);
    if (BCS()) goto PutPlayerOnVine;
    LDQ_imm(cpu.A, 0x01);
    STQ_zp(cpu.A, GameEngineSubroutine);

PutPlayerOnVine:
    LDQ_imm(cpu.A, 0x03);
    STQ_zp(cpu.A, Player_State);
    LDQ_imm(cpu.A, 0x00);
    STQ_zp(cpu.A, Player_X_Speed);
    STQ_abs(cpu.A, Player_X_MoveForce);
    LDQ_zp(cpu.A, Player_X_Position);
    SEC();
    SBC_abs(ScreenLeft_X_Pos);
    CMPQ_imm(cpu.A, 0x10);
    if (BCS()) goto SetVXPl;
    LDQ_imm(cpu.A, 0x02);
    STQ_zp(cpu.A, PlayerFacingDir);
SetVXPl:
    LDQ_zp(cpu.Y, PlayerFacingDir);
    LDQ_zp(cpu.A, 0x06);
    ASL_acc();
    ASL_acc();
    ASL_acc();
    ASL_acc();
    CLC();
    ADC_PRGTable(ClimbXPosAdder, cpu.Y - 1);
    STQ_zp(cpu.A, Player_X_Position);
    LDQ_zp(cpu.A, 0x06);
    if (BNE()) goto ExPVne;
    LDQ_abs(cpu.A, ScreenRight_PageLoc);
    CLC();
    ADC_PRGTable(ClimbPLocAdder, cpu.Y - 1);
    STQ_zp(cpu.A, Player_PageLoc);
ExPVne:
    RET();

ChkInvisibleMTiles:
    CMPQ_imm(cpu.A, 0x5f);
    if (BEQ()) goto ExCInvT;
    CMPQ_imm(cpu.A, 0x60);
ExCInvT:
    RET();

ChkForLandJumpSpring:
    CALL(ChkJumpspringMetatiles);
    if (BCC()) goto ExCJSp;
    LDQ_imm(cpu.A, 0x70);
    STQ_abs(cpu.A, VerticalForce);
    LDQ_imm(cpu.A, 0xf9);
    STQ_abs(cpu.A, JumpspringForce);
    LDQ_imm(cpu.A, 0x03);
    STQ_abs(cpu.A, JumpspringTimer);
    LSR_acc();
    STQ_abs(cpu.A, JumpspringAnimCtrl);
ExCJSp:
    RET();

ChkJumpspringMetatiles:
    CMPQ_imm(cpu.A, 0x67);
    if (BEQ()) goto JSFnd;
    CMPQ_imm(cpu.A, 0x68);
    CLC();
    if (BNE()) goto NoJSFnd;

JSFnd:
    SEC();
NoJSFnd:
    RET();

HandlePipeEntry:
    LDQ_zp(cpu.A, Up_Down_Buttons);
    AND_imm(0b00000100);
    if (BEQ()) goto ExPipeE;
    LDQ_zp(cpu.A, 0x00);
    CMPQ_imm(cpu.A, 0x11);
    if (BNE()) goto ExPipeE;
    LDQ_zp(cpu.A, 0x01);
    CMPQ_imm(cpu.A, 0x10);
    if (BNE()) goto ExPipeE;
    LDQ_imm(cpu.A, 0x30);
    STQ_abs(cpu.A, ChangeAreaTimer);
    LDQ_imm(cpu.A, 0x03);
    STQ_zp(cpu.A, GameEngineSubroutine);
    LDQ_imm(cpu.A, Sfx_PipeDown_Injury);
    STQ_zp(cpu.A, Square1SoundQueue);
    LDQ_imm(cpu.A, 0b00100000);
    STQ_abs(cpu.A, Player_SprAttrib);
    LDQ_abs(cpu.A, WarpZoneControl);
    if (BEQ()) goto ExPipeE;
    AND_imm(0b00000011);
    ASL_acc();
    ASL_acc();
    CopyAX();
    LDQ_zp(cpu.A, Player_X_Position);
    CMPQ_imm(cpu.A, 0x60);
    if (BCC()) goto GetWNum;
    INX();
    CMPQ_imm(cpu.A, 0xa0);
    if (BCC()) goto GetWNum;
    INX();
GetWNum:
    LDQ_PRGTable(cpu.Y, WarpZoneNumbers, cpu.X);
    DEY();
    STQ_abs(cpu.Y, WorldNumber);
    LDQ_PRGTable(cpu.X, WorldAddrOffsets, cpu.Y);
    LDQ_PRGTable(cpu.A, AreaAddrOffsets, cpu.X);
    STQ_abs(cpu.A, AreaPointer);
    LDQ_imm(cpu.A, Silence);
    STQ_zp(cpu.A, EventMusicQueue);
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, EntrancePage);
    STQ_abs(cpu.A, AreaNumber);
    STQ_abs(cpu.A, LevelNumber);
    STQ_abs(cpu.A, AltEntranceControl);
    INC_abs(Hidden1UpFlag);
    INC_abs(FetchNewGameTimerFlag);
ExPipeE:
    RET();

ImpedePlayerMove:
    LDQ_imm(cpu.A, 0x00);
    LDQ_zp(cpu.Y, Player_X_Speed);
    LDQ_zp(cpu.X, 0x00);
    DEX();
    if (BNE()) goto RImpd;
    INX();
    CMPQ_imm(cpu.Y, 0x00);
    if (BMI()) goto ExIPM;
    LDQ_imm(cpu.A, 0xff);
    
    goto NXSpd;
RImpd:
    LDQ_imm(cpu.X, 0x02);
    CMPQ_imm(cpu.Y, 0x01);
    if (BPL()) goto ExIPM;
    LDQ_imm(cpu.A, 0x01);
NXSpd:
    LDQ_imm(cpu.Y, 0x10);
    STQ_abs(cpu.Y, SideCollisionTimer);
    LDQ_imm(cpu.Y, 0x00);
    STQ_zp(cpu.Y, Player_X_Speed);
    CMPQ_imm(cpu.A, 0x00);
    if (BPL()) goto PlatF;
    DEY();
PlatF:
    STQ_zp(cpu.Y, 0x00);
    CLC();
    ADC_zp(Player_X_Position);
    STQ_zp(cpu.A, Player_X_Position);
    LDQ_zp(cpu.A, Player_PageLoc);
    ADC_zp(0x00);
    STQ_zp(cpu.A, Player_PageLoc);
ExIPM:
    CopyXA();
    EOR_imm(0xff);
    AND_abs(Player_CollisionBits);
    STQ_abs(cpu.A, Player_CollisionBits);
    RET();

CheckForSolidMTiles:
    CALL(GetMTileAttrib);
    CMPA_PRGTable(SolidMTileUpperExt, cpu.X);
    RET();

CheckForClimbMTiles:
    CALL(GetMTileAttrib);
    CMPA_PRGTable(ClimbMTileUpperExt, cpu.X);
    RET();

CheckForCoinMTiles:
    CMPQ_imm(cpu.A, 0xc2);
    if (BEQ()) goto CoinSd;
    CMPQ_imm(cpu.A, 0xc3);
    if (BEQ()) goto CoinSd;
    CLC();
    RET();
CoinSd:
    LDQ_imm(cpu.A, Sfx_CoinGrab);
    STQ_zp(cpu.A, Square2SoundQueue);
    RET();

GetMTileAttrib:
    CopyAY();
    AND_imm(0b11000000);
    ASL_acc();
    ROL_acc();
    ROL_acc();
    CopyAX();
    CopyYA();
ExEBG:
    RET();

EnemyToBGCollisionDet:
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b00100000);
    if (BNE()) goto ExEBG;
    CALL(SubtEnemyYPos);
    if (BCC()) goto ExEBG;
    LDQ_zp_xy(cpu.Y, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.Y, Spiny);
    if (BNE()) goto DoIDCheckBGColl;
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CMPQ_imm(cpu.A, 0x25);
    if (BCC()) goto ExEBG;

DoIDCheckBGColl:
    CMPQ_imm(cpu.Y, GreenParatroopaJump);
    if (BNE()) goto HBChk;
    
    goto EnemyJump;
HBChk:
    CMPQ_imm(cpu.Y, HammerBro);
    if (BNE()) goto CInvu;
    
    goto HammerBroBGColl;
CInvu:
    CMPQ_imm(cpu.Y, Spiny);
    if (BEQ()) goto YesIn;
    CMPQ_imm(cpu.Y, PowerUpObject);
    if (BEQ()) goto YesIn;
    CMPQ_imm(cpu.Y, 0x07);
    if (BCS()) goto ExEBGChk;

YesIn:
    CALL(ChkUnderEnemy);
    if (BNE()) goto HandleEToBGCollision;

NoEToBGCollision:
    goto ChkForRedKoopa;

HandleEToBGCollision:
    CALL(ChkForNonSolids);
    if (BEQ()) goto NoEToBGCollision;
    CMPQ_imm(cpu.A, 0x23);
    if (BNE()) goto LandEnemyProperly;
    LDQ_zp(cpu.Y, 0x02);
    LDQ_imm(cpu.A, 0x00);
    STA_ind_y(0x06);
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, 0x15);
    if (BCS()) goto ChkToStunEnemies;
    CMPQ_imm(cpu.A, Goomba);
    if (BNE()) goto GiveOEPoints;
    CALL(KillEnemyAboveBlock);

GiveOEPoints:
    LDQ_imm(cpu.A, 0x01);
    CALL(SetupFloateyNumber);

ChkToStunEnemies:
    CMPQ_imm(cpu.A, 0x09);
    if (BCC()) goto SetStun;
    CMPQ_imm(cpu.A, 0x11);
    if (BCS()) goto SetStun;
    CMPQ_imm(cpu.A, 0x0a);
    if (BCC()) goto Demote;
    CMPQ_imm(cpu.A, PiranhaPlant);
    if (BCC()) goto SetStun;

Demote:
    AND_imm(0b00000001);
    STQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
SetStun:
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b11110000);
    ORA_imm(0b00000010);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    DEC_zp_x(Enemy_Y_Position);
    DEC_zp_x(Enemy_Y_Position);
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, Bloober);
    if (BEQ()) goto SetWYSpd;
    LDQ_imm(cpu.A, 0xfd);
    LDQ_abs(cpu.Y, AreaType);
    if (BNE()) goto SetNotW;

SetWYSpd:
    LDQ_imm(cpu.A, 0xff);
SetNotW:
    STQ_zp_xy(cpu.A, Enemy_Y_Speed, cpu.X);
    LDQ_imm(cpu.Y, 0x01);
    CALL(PlayerEnemyDiff);
    if (BPL()) goto ChkBBill;
    INY();
ChkBBill:
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, BulletBill_CannonVar);
    if (BEQ()) goto NoCDirF;
    CMPQ_imm(cpu.A, BulletBill_FrenzyVar);
    if (BEQ()) goto NoCDirF;
    STQ_zp_xy(cpu.Y, Enemy_MovingDir, cpu.X);
NoCDirF:
    DEY();
    LDQ_PRGTable(cpu.A, EnemyBGCXSpdData, cpu.Y);
    STQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
ExEBGChk:
    RET();

LandEnemyProperly:
    LDQ_zp(cpu.A, 0x04);
    SEC();
    SBC_imm(0x08);
    CMPQ_imm(cpu.A, 0x05);
    if (BCS()) goto ChkForRedKoopa;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b01000000);
    if (BNE()) goto LandEnemyInitState;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    ASL_acc();
    if (BCC()) goto ChkLandedEnemyState;

SChkA:
    
    goto DoEnemySideCheck;

ChkLandedEnemyState:
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    if (BEQ()) goto SChkA;
    CMPQ_imm(cpu.A, 0x05);
    if (BEQ()) goto ProcEnemyDirection;
    CMPQ_imm(cpu.A, 0x03);
    if (BCS()) goto ExSteChk;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    CMPQ_imm(cpu.A, 0x02);
    if (BNE()) goto ProcEnemyDirection;
    LDQ_imm(cpu.A, 0x10);
    LDQ_zp_xy(cpu.Y, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.Y, Spiny);
    if (BNE()) goto SetForStn;
    LDQ_imm(cpu.A, 0x00);
SetForStn:
    STA_abs_xy(EnemyIntervalTimer, cpu.X);
    LDQ_imm(cpu.A, 0x03);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    CALL(EnemyLanding);
ExSteChk:
    RET();

ProcEnemyDirection:
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, Goomba);
    if (BEQ()) goto LandEnemyInitState;
    CMPQ_imm(cpu.A, Spiny);
    if (BNE()) goto InvtD;
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, Enemy_MovingDir, cpu.X);
    LDQ_imm(cpu.A, 0x08);
    STQ_zp_xy(cpu.A, Enemy_X_Speed, cpu.X);
    LDQ_zp(cpu.A, FrameCounter);
    AND_imm(0b00000111);
    if (BEQ()) goto LandEnemyInitState;

InvtD:
    LDQ_imm(cpu.Y, 0x01);
    CALL(PlayerEnemyDiff);
    if (BPL()) goto CNwCDir;
    INY();
CNwCDir:
    CopyYA();
    CMPA_zp_xy(Enemy_MovingDir, cpu.X);
    if (BNE()) goto LandEnemyInitState;
    CALL(ChkForBump_HammerBroJ);

LandEnemyInitState:
    CALL(EnemyLanding);
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b10000000);
    if (BNE()) goto NMovShellFallBit;
    LDQ_imm(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    RET();

NMovShellFallBit:
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b10111111);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    RET();

ChkForRedKoopa:
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, RedKoopa);
    if (BNE()) goto Chk2MSBSt;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    if (BEQ()) goto ChkForBump_HammerBroJ;

Chk2MSBSt:
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    CopyAY();
    ASL_acc();
    if (BCC()) goto GetSteFromD;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    ORA_imm(0b01000000);
    
    goto SetD6Ste;
GetSteFromD:
    LDQ_PRGTable(cpu.A, EnemyBGCStateData, cpu.Y);
SetD6Ste:
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);

DoEnemySideCheck:
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CMPQ_imm(cpu.A, 0x20);
    if (BCC()) goto ExESdeC;
    LDQ_imm(cpu.Y, 0x16);
    LDQ_imm(cpu.A, 0x02);
    STQ_zp(cpu.A, 0xeb);
SdeCLoop:
    LDQ_zp(cpu.A, 0xeb);
    CMPA_zp_xy(Enemy_MovingDir, cpu.X);
    if (BNE()) goto NextSdeC;
    LDQ_imm(cpu.A, 0x01);
    CALL(BlockBufferChk_Enemy);
    if (BEQ()) goto NextSdeC;
    CALL(ChkForNonSolids);
    if (BNE()) goto ChkForBump_HammerBroJ;

NextSdeC:
    DEC_zp(0xeb);
    INY();
    CMPQ_imm(cpu.Y, 0x18);
    if (BCC()) goto SdeCLoop;

ExESdeC:
    RET();

ChkForBump_HammerBroJ:
    CMPQ_imm(cpu.X, 0x05);
    if (BEQ()) goto NoBump;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    ASL_acc();
    if (BCC()) goto NoBump;
    LDQ_imm(cpu.A, Sfx_Bump);
    STQ_zp(cpu.A, Square1SoundQueue);
NoBump:
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, 0x05);
    if (BNE()) goto InvEnemyDir;
    LDQ_imm(cpu.A, 0x00);
    STQ_zp(cpu.A, 0x00);
    LDQ_imm(cpu.Y, 0xfa);
    
    goto SetHJ;

InvEnemyDir:
    goto RXSpd;

PlayerEnemyDiff:
    LDQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    SEC();
    SBC_zp(Player_X_Position);
    STQ_zp(cpu.A, 0x00);
    LDQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    SBC_zp(Player_PageLoc);
    RET();

EnemyLanding:
    CALL(InitVStf);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    AND_imm(0b11110000);
    ORA_imm(0b00001000);
    STQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    RET();

SubtEnemyYPos:
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CLC();
    ADC_imm(0x3e);
    CMPQ_imm(cpu.A, 0x44);
    RET();

EnemyJump:
    CALL(SubtEnemyYPos);
    if (BCC()) goto DoSide;
    LDQ_zp_xy(cpu.A, Enemy_Y_Speed, cpu.X);
    CLC();
    ADC_imm(0x02);
    CMPQ_imm(cpu.A, 0x03);
    if (BCC()) goto DoSide;
    CALL(ChkUnderEnemy);
    if (BEQ()) goto DoSide;
    CALL(ChkForNonSolids);
    if (BEQ()) goto DoSide;
    CALL(EnemyLanding);
    LDQ_imm(cpu.A, 0xfd);
    STQ_zp_xy(cpu.A, Enemy_Y_Speed, cpu.X);
DoSide:
    
    goto DoEnemySideCheck;

HammerBroBGColl:
    CALL(ChkUnderEnemy);
    if (BEQ()) goto NoUnderHammerBro;
    CMPQ_imm(cpu.A, 0x23);
    if (BNE()) goto UnderHammerBro;

KillEnemyAboveBlock:
    CALL(ShellOrBlockDefeat);
    LDQ_imm(cpu.A, 0xfc);
    STQ_zp_xy(cpu.A, Enemy_Y_Speed, cpu.X);
    RET();

UnderHammerBro:
    LDQ_abs_xy(cpu.A, EnemyFrameTimer, cpu.X);
    if (BNE()) goto NoUnderHammerBro;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    AND_imm(0b10001000);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    CALL(EnemyLanding);
    
    goto DoEnemySideCheck;

NoUnderHammerBro:
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    ORA_imm(0x01);
    STQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    RET();

ChkUnderEnemy:
    LDQ_imm(cpu.A, 0x00);
    LDQ_imm(cpu.Y, 0x15);
    
    goto BlockBufferChk_Enemy;

ChkForNonSolids:
    CMPQ_imm(cpu.A, 0x26);
    if (BEQ()) goto NSFnd;
    CMPQ_imm(cpu.A, 0xc2);
    if (BEQ()) goto NSFnd;
    CMPQ_imm(cpu.A, 0xc3);
    if (BEQ()) goto NSFnd;
    CMPQ_imm(cpu.A, 0x5f);
    if (BEQ()) goto NSFnd;
    CMPQ_imm(cpu.A, 0x60);
NSFnd:
    RET();

FireballBGCollision:
    LDQ_zp_xy(cpu.A, Fireball_Y_Position, cpu.X);
    CMPQ_imm(cpu.A, 0x18);
    if (BCC()) goto ClearBounceFlag;
    CALL(BlockBufferChk_FBall);
    if (BEQ()) goto ClearBounceFlag;
    CALL(ChkForNonSolids);
    if (BEQ()) goto ClearBounceFlag;
    LDQ_zp_xy(cpu.A, Fireball_Y_Speed, cpu.X);
    if (BMI()) goto InitFireballExplode;
    LDQ_zp_xy(cpu.A, FireballBouncingFlag, cpu.X);
    if (BNE()) goto InitFireballExplode;
    LDQ_imm(cpu.A, 0xfd);
    STQ_zp_xy(cpu.A, Fireball_Y_Speed, cpu.X);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp_xy(cpu.A, FireballBouncingFlag, cpu.X);
    LDQ_zp_xy(cpu.A, Fireball_Y_Position, cpu.X);
    AND_imm(0xf8);
    STQ_zp_xy(cpu.A, Fireball_Y_Position, cpu.X);
    RET();

ClearBounceFlag:
    LDQ_imm(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, FireballBouncingFlag, cpu.X);
    RET();

InitFireballExplode:
    LDQ_imm(cpu.A, 0x80);
    STQ_zp_xy(cpu.A, Fireball_State, cpu.X);
    LDQ_imm(cpu.A, Sfx_Bump);
    STQ_zp(cpu.A, Square1SoundQueue);
    RET();

GetFireballBoundBox:
    CopyXA();
    CLC();
    ADC_imm(0x07);
    CopyAX();
    LDQ_imm(cpu.Y, 0x02);
    if (BNE()) goto FBallB;

GetMiscBoundBox:
    CopyXA();
    CLC();
    ADC_imm(0x09);
    CopyAX();
    LDQ_imm(cpu.Y, 0x06);
FBallB:
    CALL(BoundingBoxCore);
    
    goto CheckRightScreenBBox;

GetEnemyBoundBox:
    LDQ_imm(cpu.Y, 0x48);
    STQ_zp(cpu.Y, 0x00);
    LDQ_imm(cpu.Y, 0x44);
    
    goto GetMaskedOffScrBits;

SmallPlatformBoundBox:
    LDQ_imm(cpu.Y, 0x08);
    STQ_zp(cpu.Y, 0x00);
    LDQ_imm(cpu.Y, 0x04);

GetMaskedOffScrBits:
    LDQ_zp_xy(cpu.A, Enemy_X_Position, cpu.X);
    SEC();
    SBC_abs(ScreenLeft_X_Pos);
    STQ_zp(cpu.A, 0x01);
    LDQ_zp_xy(cpu.A, Enemy_PageLoc, cpu.X);
    SBC_abs(ScreenLeft_PageLoc);
    if (BMI()) goto CMBits;
    ORA_zp(0x01);
    if (BEQ()) goto CMBits;
    LDQ_zp(cpu.Y, 0x00);
CMBits:
    CopyYA();
    AND_abs(Enemy_OffscreenBits);
    STA_abs_xy(EnemyOffscrBitsMasked, cpu.X);
    if (BNE()) goto MoveBoundBoxOffscreen;
    
    goto SetupEOffsetFBBox;

LargePlatformBoundBox:
    INX();
    CALL(GetXOffscreenBits);
    DEX();
    CMPQ_imm(cpu.A, 0xfe);
    if (BCS()) goto MoveBoundBoxOffscreen;

SetupEOffsetFBBox:
    CopyXA();
    CLC();
    ADC_imm(0x01);
    CopyAX();
    LDQ_imm(cpu.Y, 0x01);
    CALL(BoundingBoxCore);
    
    goto CheckRightScreenBBox;

MoveBoundBoxOffscreen:
    CopyXA();
    ASL_acc();
    ASL_acc();
    CopyAY();
    LDQ_imm(cpu.A, 0xff);
    STA_abs_xy(EnemyBoundingBoxCoord, cpu.Y);
    STA_abs_xy(EnemyBoundingBoxCoord + 1, cpu.Y);
    STA_abs_xy(EnemyBoundingBoxCoord + 2, cpu.Y);
    STA_abs_xy(EnemyBoundingBoxCoord + 3, cpu.Y);
    RET();

BoundingBoxCore:
    STQ_zp(cpu.X, 0x00);
    LDQ_abs_xy(cpu.A, SprObject_Rel_YPos, cpu.Y);
    STQ_zp(cpu.A, 0x02);
    LDQ_abs_xy(cpu.A, SprObject_Rel_XPos, cpu.Y);
    STQ_zp(cpu.A, 0x01);
    CopyXA();
    ASL_acc();
    ASL_acc();
    PHA();
    CopyAY();
    LDQ_abs_xy(cpu.A, SprObj_BoundBoxCtrl, cpu.X);
    ASL_acc();
    ASL_acc();
    CopyAX();
    LDQ_zp(cpu.A, 0x01);
    CLC();
    ADC_PRGTable(BoundBoxCtrlData, cpu.X);
    STA_abs_xy(BoundingBox_UL_Corner, cpu.Y);
    LDQ_zp(cpu.A, 0x01);
    CLC();
    ADC_PRGTable(BoundBoxCtrlData, cpu.X + 2);
    STA_abs_xy(BoundingBox_LR_Corner, cpu.Y);
    INX();
    INY();
    LDQ_zp(cpu.A, 0x02);
    CLC();
    ADC_PRGTable(BoundBoxCtrlData, cpu.X);
    STA_abs_xy(BoundingBox_UL_Corner, cpu.Y);
    LDQ_zp(cpu.A, 0x02);
    CLC();
    ADC_PRGTable(BoundBoxCtrlData, cpu.X + 2);
    STA_abs_xy(BoundingBox_LR_Corner, cpu.Y);
    PLA();
    CopyAY();
    LDQ_zp(cpu.X, 0x00);
    RET();

CheckRightScreenBBox:
    LDQ_abs(cpu.A, ScreenLeft_X_Pos);
    CLC();
    ADC_imm(0x80);
    STQ_zp(cpu.A, 0x02);
    LDQ_abs(cpu.A, ScreenLeft_PageLoc);
    ADC_imm(0x00);
    STQ_zp(cpu.A, 0x01);
    LDQ_zp_xy(cpu.A, SprObject_X_Position, cpu.X);
    CMPQ_zp(cpu.A, 0x02);
    LDQ_zp_xy(cpu.A, SprObject_PageLoc, cpu.X);
    SBC_zp(0x01);
    if (BCC()) goto CheckLeftScreenBBox;
    LDQ_abs_xy(cpu.A, BoundingBox_DR_XPos, cpu.Y);
    if (BMI()) goto NoOfs;
    LDQ_imm(cpu.A, 0xff);
    LDQ_abs_xy(cpu.X, BoundingBox_UL_XPos, cpu.Y);
    if (BMI()) goto SORte;
    STA_abs_xy(BoundingBox_UL_XPos, cpu.Y);
SORte:
    STA_abs_xy(BoundingBox_DR_XPos, cpu.Y);
NoOfs:
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

CheckLeftScreenBBox:
    LDQ_abs_xy(cpu.A, BoundingBox_UL_XPos, cpu.Y);
    if (BPL()) goto NoOfs2;
    CMPQ_imm(cpu.A, 0xa0);
    if (BCC()) goto NoOfs2;
    LDQ_imm(cpu.A, 0x00);
    LDQ_abs_xy(cpu.X, BoundingBox_DR_XPos, cpu.Y);
    if (BPL()) goto SOLft;
    STA_abs_xy(BoundingBox_DR_XPos, cpu.Y);
SOLft:
    STA_abs_xy(BoundingBox_UL_XPos, cpu.Y);
NoOfs2:
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

PlayerCollisionCore:
    LDQ_imm(cpu.X, 0x00);

SprObjectCollisionCore:
    STQ_zp(cpu.Y, 0x06);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp(cpu.A, 0x07);

CollisionCoreLoop:
    LDQ_abs_xy(cpu.A, BoundingBox_UL_Corner, cpu.Y);
    CMPA_abs_xy(BoundingBox_UL_Corner, cpu.X);
    if (BCS()) goto FirstBoxGreater;
    CMPA_abs_xy(BoundingBox_LR_Corner, cpu.X);
    if (BCC()) goto SecondBoxVerticalChk;
    if (BEQ()) goto CollisionFound;
    LDQ_abs_xy(cpu.A, BoundingBox_LR_Corner, cpu.Y);
    CMPA_abs_xy(BoundingBox_UL_Corner, cpu.Y);
    if (BCC()) goto CollisionFound;
    CMPA_abs_xy(BoundingBox_UL_Corner, cpu.X);
    if (BCS()) goto CollisionFound;
    LDQ_zp(cpu.Y, 0x06);
    RET();

SecondBoxVerticalChk:
    LDQ_abs_xy(cpu.A, BoundingBox_LR_Corner, cpu.X);
    CMPA_abs_xy(BoundingBox_UL_Corner, cpu.X);
    if (BCC()) goto CollisionFound;
    LDQ_abs_xy(cpu.A, BoundingBox_LR_Corner, cpu.Y);
    CMPA_abs_xy(BoundingBox_UL_Corner, cpu.X);
    if (BCS()) goto CollisionFound;
    LDQ_zp(cpu.Y, 0x06);
    RET();

FirstBoxGreater:
    CMPA_abs_xy(BoundingBox_UL_Corner, cpu.X);
    if (BEQ()) goto CollisionFound;
    CMPA_abs_xy(BoundingBox_LR_Corner, cpu.X);
    if (BCC()) goto CollisionFound;
    if (BEQ()) goto CollisionFound;
    CMPA_abs_xy(BoundingBox_LR_Corner, cpu.Y);
    if (BCC()) goto NoCollisionFound;
    if (BEQ()) goto NoCollisionFound;
    LDQ_abs_xy(cpu.A, BoundingBox_LR_Corner, cpu.Y);
    CMPA_abs_xy(BoundingBox_UL_Corner, cpu.X);
    if (BCS()) goto CollisionFound;

NoCollisionFound:
    CLC();
    LDQ_zp(cpu.Y, 0x06);
    RET();

CollisionFound:
    INX();
    INY();
    DEC_zp(0x07);
    if (BPL()) goto CollisionCoreLoop;
    SEC();
    LDQ_zp(cpu.Y, 0x06);
    RET();

BlockBufferChk_Enemy:
    PHA();
    CopyXA();
    CLC();
    ADC_imm(0x01);
    CopyAX();
    PLA();
    
    goto BBChk_E;

#ifndef REMOVE_RESIDUAL
// ResidualMiscObjectCode: // unreferenced
    CopyXA();
    CLC();
    ADC_imm(0x0d);
    CopyAX();
    LDQ_imm(cpu.Y, 0x1b);
    goto ResJmpM;
#endif

BlockBufferChk_FBall:
    LDQ_imm(cpu.Y, 0x1a);
    CopyXA();
    CLC();
    ADC_imm(0x07);
    CopyAX();
#ifndef REMOVE_RESIDUAL
ResJmpM:
#endif
    LDQ_imm(cpu.A, 0x00);

BBChk_E:
    CALL(BlockBufferCollision);
    LDQ_zp(cpu.X, ObjectOffset);
    CMPQ_imm(cpu.A, 0x00);
    RET();

BlockBufferColli_Feet:
    INY();

BlockBufferColli_Head:
    LDQ_imm(cpu.A, 0x00);
    bitFlag = true;

BlockBufferColli_Side:
    if (!bitFlag)
        LDQ_imm(cpu.A, 0x01);
    else
        bitFlag = false;

    LDQ_imm(cpu.X, 0x00);

BlockBufferCollision:
    PHA();
    STQ_zp(cpu.Y, 0x04);
    LDQ_PRGTable(cpu.A, BlockBuffer_X_Adder, cpu.Y);
    CLC();
    ADC_zp_x(SprObject_X_Position);
    STQ_zp(cpu.A, 0x05);
    LDQ_zp_xy(cpu.A, SprObject_PageLoc, cpu.X);
    ADC_imm(0x00);
    AND_imm(0x01);
    LSR_acc();
    ORA_zp(0x05);
    ROR_acc();
    LSR_acc();
    LSR_acc();
    LSR_acc();
    CALL(GetBlockBufferAddr);
    LDQ_zp(cpu.Y, 0x04);
    LDQ_zp_xy(cpu.A, SprObject_Y_Position, cpu.X);
    CLC();
    ADC_PRGTable(BlockBuffer_Y_Adder, cpu.Y);
    AND_imm(0b11110000);
    SEC();
    SBC_imm(0x20);
    STQ_zp(cpu.A, 0x02);
    CopyAY();
    LDA_Dyn_ind_y(0x06);
    STQ_zp(cpu.A, 0x03);
    LDQ_zp(cpu.Y, 0x04);
    PLA();
    if (BNE()) goto RetXC;
    LDQ_zp_xy(cpu.A, SprObject_Y_Position, cpu.X);
    
    goto RetYC;
RetXC:
    LDQ_zp_xy(cpu.A, SprObject_X_Position, cpu.X);
RetYC:
    AND_imm(0b00001111);
    STQ_zp(cpu.A, 0x04);
    LDQ_zp(cpu.A, 0x03);
    RET();

DrawVine:
    STQ_zp(cpu.Y, 0x00);
    LDQ_abs(cpu.A, Enemy_Rel_YPos);
    CLC();
    ADC_PRGTable(VineYPosAdder, cpu.Y);
    LDQ_abs_xy(cpu.X, VineObjOffset, cpu.Y);
    LDQ_abs_xy(cpu.Y, Enemy_SprDataOffset, cpu.X);
    STQ_zp(cpu.Y, 0x02);
    CALL(SixSpriteStacker);
    LDQ_abs(cpu.A, Enemy_Rel_XPos);
    STA_abs_xy(Sprite_X_Position, cpu.Y);
    STA_abs_xy(Sprite_X_Position + 8, cpu.Y);
    STA_abs_xy(Sprite_X_Position + 16, cpu.Y);
    CLC();
    ADC_imm(0x06);
    STA_abs_xy(Sprite_X_Position + 4, cpu.Y);
    STA_abs_xy(Sprite_X_Position + 12, cpu.Y);
    STA_abs_xy(Sprite_X_Position + 20, cpu.Y);
    LDQ_imm(cpu.A, 0b00100001);
    STA_abs_xy(Sprite_Attributes, cpu.Y);
    STA_abs_xy(Sprite_Attributes + 8, cpu.Y);
    STA_abs_xy(Sprite_Attributes + 16, cpu.Y);
    ORA_imm(0b01000000);
    STA_abs_xy(Sprite_Attributes + 4, cpu.Y);
    STA_abs_xy(Sprite_Attributes + 12, cpu.Y);
    STA_abs_xy(Sprite_Attributes + 20, cpu.Y);
    LDQ_imm(cpu.X, 0x05);
VineTL:
    LDQ_imm(cpu.A, 0xe1);
    STA_abs_xy(Sprite_Tilenumber, cpu.Y);
    INY();
    INY();
    INY();
    INY();
    DEX();
    if (BPL()) goto VineTL;
    LDQ_zp(cpu.Y, 0x02);
    LDQ_zp(cpu.A, 0x00);
    if (BNE()) goto SkpVTop;
    LDQ_imm(cpu.A, 0xe0);
    STA_abs_xy(Sprite_Tilenumber, cpu.Y);
SkpVTop:
    LDQ_imm(cpu.X, 0x00);
ChkFTop:
    LDQ_abs(cpu.A, VineStart_Y_Position);
    SEC();
    SBC_abs_y(Sprite_Y_Position);
    CMPQ_imm(cpu.A, 0x64);
    if (BCC()) goto NextVSp;
    LDQ_imm(cpu.A, 0xf8);
    STA_abs_xy(Sprite_Y_Position, cpu.Y);
NextVSp:
    INY();
    INY();
    INY();
    INY();
    INX();
    CMPQ_imm(cpu.X, 0x06);
    if (BNE()) goto ChkFTop;
    LDQ_zp(cpu.Y, 0x00);
    RET();

SixSpriteStacker:
    LDQ_imm(cpu.X, 0x06);
StkLp:
    STA_abs_xy(Sprite_Data, cpu.Y);
    CLC();
    ADC_imm(0x08);
    INY();
    INY();
    INY();
    INY();
    DEX();
    if (BNE()) goto StkLp;
    LDQ_zp(cpu.Y, 0x02);
    RET();

DrawHammer:
    LDQ_abs_xy(cpu.Y, Misc_SprDataOffset, cpu.X);
    LDQ_abs(cpu.A, TimerControl);
    if (BNE()) goto ForceHPose;
    LDQ_zp_xy(cpu.A, Misc_State, cpu.X);
    AND_imm(0b01111111);
    CMPQ_imm(cpu.A, 0x01);
    if (BEQ()) goto GetHPose;

ForceHPose:
    LDQ_imm(cpu.X, 0x00);
    if (BEQ()) goto RenderH;

GetHPose:
    LDQ_zp(cpu.A, FrameCounter);
    LSR_acc();
    LSR_acc();
    AND_imm(0b00000011);
    CopyAX();
RenderH:
    LDQ_abs(cpu.A, Misc_Rel_YPos);
    CLC();
    ADC_PRGTable(FirstSprYPos, cpu.X);
    STA_abs_xy(Sprite_Y_Position, cpu.Y);
    CLC();
    ADC_PRGTable(SecondSprYPos, cpu.X);
    STA_abs_xy(Sprite_Y_Position + 4, cpu.Y);
    LDQ_abs(cpu.A, Misc_Rel_XPos);
    CLC();
    ADC_PRGTable(FirstSprXPos, cpu.X);
    STA_abs_xy(Sprite_X_Position, cpu.Y);
    CLC();
    ADC_PRGTable(SecondSprXPos, cpu.X);
    STA_abs_xy(Sprite_X_Position + 4, cpu.Y);
    LDQ_PRGTable(cpu.A, FirstSprTilenum, cpu.X);
    STA_abs_xy(Sprite_Tilenumber, cpu.Y);
    LDQ_PRGTable(cpu.A, SecondSprTilenum, cpu.X);
    STA_abs_xy(Sprite_Tilenumber + 4, cpu.Y);
    LDQ_PRGTable(cpu.A, HammerSprAttrib, cpu.X);
    STA_abs_xy(Sprite_Attributes, cpu.Y);
    STA_abs_xy(Sprite_Attributes + 4, cpu.Y);
    LDQ_zp(cpu.X, ObjectOffset);
    LDQ_abs(cpu.A, Misc_OffscreenBits);
    AND_imm(0b11111100);
    if (BEQ()) goto NoHOffscr;
    LDQ_imm(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, Misc_State, cpu.X);
    LDQ_imm(cpu.A, 0xf8);
    CALL(DumpTwoSpr);
NoHOffscr:
    RET();

FlagpoleGfxHandler:
    LDQ_abs_xy(cpu.Y, Enemy_SprDataOffset, cpu.X);
    LDQ_abs(cpu.A, Enemy_Rel_XPos);
    STA_abs_xy(Sprite_X_Position, cpu.Y);
    CLC();
    ADC_imm(0x08);
    STA_abs_xy(Sprite_X_Position + 4, cpu.Y);
    STA_abs_xy(Sprite_X_Position + 8, cpu.Y);
    CLC();
    ADC_imm(0x0c);
    STQ_zp(cpu.A, 0x05);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CALL(DumpTwoSpr);
    ADC_imm(0x08);
    STA_abs_xy(Sprite_Y_Position + 8, cpu.Y);
    LDQ_abs(cpu.A, FlagpoleFNum_Y_Pos);
    STQ_zp(cpu.A, 0x02);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp(cpu.A, 0x03);
    STQ_zp(cpu.A, 0x04);
    STA_abs_xy(Sprite_Attributes, cpu.Y);
    STA_abs_xy(Sprite_Attributes + 4, cpu.Y);
    STA_abs_xy(Sprite_Attributes + 8, cpu.Y);
    LDQ_imm(cpu.A, 0x7e);
    STA_abs_xy(Sprite_Tilenumber, cpu.Y);
    STA_abs_xy(Sprite_Tilenumber + 8, cpu.Y);
    LDQ_imm(cpu.A, 0x7f);
    STA_abs_xy(Sprite_Tilenumber + 4, cpu.Y);
    LDQ_abs(cpu.A, FlagpoleCollisionYPos);
    if (BEQ()) goto ChkFlagOffscreen;
    CopyYA();
    CLC();
    ADC_imm(0x0c);
    CopyAY();
    LDQ_abs(cpu.A, FlagpoleScore);
    ASL_acc();
    CopyAX();
    LDQ_PRGTable(cpu.A, FlagpoleScoreNumTiles, cpu.X);
    STQ_zp(cpu.A, 0x00);
    LDQ_PRGTable(cpu.A, FlagpoleScoreNumTiles, cpu.X + 1);
    CALL(DrawOneSpriteRow);

ChkFlagOffscreen:
    LDQ_zp(cpu.X, ObjectOffset);
    LDQ_abs_xy(cpu.Y, Enemy_SprDataOffset, cpu.X);
    LDQ_abs(cpu.A, Enemy_OffscreenBits);
    AND_imm(0b00001110);
    if (BEQ()) goto ExitDumpSpr;

MoveSixSpritesOffscreen:
    LDQ_imm(cpu.A, 0xf8);

DumpSixSpr:
    STA_abs_xy(Sprite_Data + 20, cpu.Y);
    STA_abs_xy(Sprite_Data + 16, cpu.Y);

DumpFourSpr:
    STA_abs_xy(Sprite_Data + 12, cpu.Y);

DumpThreeSpr:
    STA_abs_xy(Sprite_Data + 8, cpu.Y);

DumpTwoSpr:
    STA_abs_xy(Sprite_Data + 4, cpu.Y);
    STA_abs_xy(Sprite_Data, cpu.Y);

ExitDumpSpr:
    RET();

DrawLargePlatform:
    LDQ_abs_xy(cpu.Y, Enemy_SprDataOffset, cpu.X);
    STQ_zp(cpu.Y, 0x02);
    INY();
    INY();
    INY();
    LDQ_abs(cpu.A, Enemy_Rel_XPos);
    CALL(SixSpriteStacker);
    LDQ_zp(cpu.X, ObjectOffset);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CALL(DumpFourSpr);
    LDQ_abs(cpu.Y, AreaType);
    CMPQ_imm(cpu.Y, 0x03);
    if (BEQ()) goto ShrinkPlatform;
    LDQ_abs(cpu.Y, SecondaryHardMode);
    if (BEQ()) goto SetLast2Platform;

ShrinkPlatform:
    LDQ_imm(cpu.A, 0xf8);

SetLast2Platform:
    LDQ_abs_xy(cpu.Y, Enemy_SprDataOffset, cpu.X);
    STA_abs_xy(Sprite_Y_Position + 16, cpu.Y);
    STA_abs_xy(Sprite_Y_Position + 20, cpu.Y);
    LDQ_imm(cpu.A, 0x5b);
    LDQ_abs(cpu.X, CloudTypeOverride);
    if (BEQ()) goto SetPlatformTilenum;
    LDQ_imm(cpu.A, 0x75);

SetPlatformTilenum:
    LDQ_zp(cpu.X, ObjectOffset);
    INY();
    CALL(DumpSixSpr);
    LDQ_imm(cpu.A, 0x02);
    INY();
    CALL(DumpSixSpr);
    INX();
    CALL(GetXOffscreenBits);
    DEX();
    LDQ_abs_xy(cpu.Y, Enemy_SprDataOffset, cpu.X);
    ASL_acc();
    PHA();
    if (BCC()) goto SChk2;
    LDQ_imm(cpu.A, 0xf8);
    STA_abs_xy(Sprite_Y_Position, cpu.Y);
SChk2:
    PLA();
    ASL_acc();
    PHA();
    if (BCC()) goto SChk3;
    LDQ_imm(cpu.A, 0xf8);
    STA_abs_xy(Sprite_Y_Position + 4, cpu.Y);
SChk3:
    PLA();
    ASL_acc();
    PHA();
    if (BCC()) goto SChk4;
    LDQ_imm(cpu.A, 0xf8);
    STA_abs_xy(Sprite_Y_Position + 8, cpu.Y);
SChk4:
    PLA();
    ASL_acc();
    PHA();
    if (BCC()) goto SChk5;
    LDQ_imm(cpu.A, 0xf8);
    STA_abs_xy(Sprite_Y_Position + 12, cpu.Y);
SChk5:
    PLA();
    ASL_acc();
    PHA();
    if (BCC()) goto SChk6;
    LDQ_imm(cpu.A, 0xf8);
    STA_abs_xy(Sprite_Y_Position + 16, cpu.Y);
SChk6:
    PLA();
    ASL_acc();
    if (BCC()) goto SLChk;
    LDQ_imm(cpu.A, 0xf8);
    STA_abs_xy(Sprite_Y_Position + 20, cpu.Y);
SLChk:
    LDQ_abs(cpu.A, Enemy_OffscreenBits);
    ASL_acc();
    if (BCC()) goto ExDLPl;
    CALL(MoveSixSpritesOffscreen);
ExDLPl:
    RET();

DrawFloateyNumber_Coin:
    LDQ_zp(cpu.A, FrameCounter);
    LSR_acc();
    if (BCS()) goto NotRsNum;
    DEC_zp_x(Misc_Y_Position);
NotRsNum:
    LDQ_zp_xy(cpu.A, Misc_Y_Position, cpu.X);
    CALL(DumpTwoSpr);
    LDQ_abs(cpu.A, Misc_Rel_XPos);
    STA_abs_xy(Sprite_X_Position, cpu.Y);
    CLC();
    ADC_imm(0x08);
    STA_abs_xy(Sprite_X_Position + 4, cpu.Y);
    LDQ_imm(cpu.A, 0x02);
    STA_abs_xy(Sprite_Attributes, cpu.Y);
    STA_abs_xy(Sprite_Attributes + 4, cpu.Y);
    LDQ_imm(cpu.A, 0xf7);
    STA_abs_xy(Sprite_Tilenumber, cpu.Y);
    LDQ_imm(cpu.A, 0xfb);
    STA_abs_xy(Sprite_Tilenumber + 4, cpu.Y);
    
    goto ExJCGfx;

JCoinGfxHandler:
    LDQ_abs_xy(cpu.Y, Misc_SprDataOffset, cpu.X);
    LDQ_zp_xy(cpu.A, Misc_State, cpu.X);
    CMPQ_imm(cpu.A, 0x02);
    if (BCS()) goto DrawFloateyNumber_Coin;
    LDQ_zp_xy(cpu.A, Misc_Y_Position, cpu.X);
    STA_abs_xy(Sprite_Y_Position, cpu.Y);
    CLC();
    ADC_imm(0x08);
    STA_abs_xy(Sprite_Y_Position + 4, cpu.Y);
    LDQ_abs(cpu.A, Misc_Rel_XPos);
    STA_abs_xy(Sprite_X_Position, cpu.Y);
    STA_abs_xy(Sprite_X_Position + 4, cpu.Y);
    LDQ_zp(cpu.A, FrameCounter);
    LSR_acc();
    AND_imm(0b00000011);
    CopyAX();
    LDQ_PRGTable(cpu.A, JumpingCoinTiles, cpu.X);
    INY();
    CALL(DumpTwoSpr);
    DEY();
    LDQ_imm(cpu.A, 0x02);
    STA_abs_xy(Sprite_Attributes, cpu.Y);
    LDQ_imm(cpu.A, 0x82);
    STA_abs_xy(Sprite_Attributes + 4, cpu.Y);
    LDQ_zp(cpu.X, ObjectOffset);
ExJCGfx:
    RET();

DrawPowerUp:
    LDQ_abs(cpu.Y, Enemy_SprDataOffset + 5);
    LDQ_abs(cpu.A, Enemy_Rel_YPos);
    CLC();
    ADC_imm(0x08);
    STQ_zp(cpu.A, 0x02);
    LDQ_abs(cpu.A, Enemy_Rel_XPos);
    STQ_zp(cpu.A, 0x05);
    LDQ_zp(cpu.X, PowerUpType);
    LDQ_PRGTable(cpu.A, PowerUpAttributes, cpu.X);
    ORA_abs(Enemy_SprAttrib + 5);
    STQ_zp(cpu.A, 0x04);
    CopyXA();
    PHA();
    ASL_acc();
    ASL_acc();
    CopyAX();
    LDQ_imm(cpu.A, 0x01);
    STQ_zp(cpu.A, 0x07);
    STQ_zp(cpu.A, 0x03);

PUpDrawLoop:
    LDQ_PRGTable(cpu.A, PowerUpGfxTable, cpu.X);
    STQ_zp(cpu.A, 0x00);
    LDQ_PRGTable(cpu.A, PowerUpGfxTable, cpu.X + 1);
    CALL(DrawOneSpriteRow);
    DEC_zp(0x07);
    if (BPL()) goto PUpDrawLoop;
    LDQ_abs(cpu.Y, Enemy_SprDataOffset + 5);
    PLA();
    if (BEQ()) goto PUpOfs;
    CMPQ_imm(cpu.A, 0x03);
    if (BEQ()) goto PUpOfs;
    STQ_zp(cpu.A, 0x00);
    LDQ_zp(cpu.A, FrameCounter);
    LSR_acc();
    AND_imm(0b00000011);
    ORA_abs(Enemy_SprAttrib + 5);
    STA_abs_xy(Sprite_Attributes, cpu.Y);
    STA_abs_xy(Sprite_Attributes + 4, cpu.Y);
    LDQ_zp(cpu.X, 0x00);
    DEX();
    if (BEQ()) goto FlipPUpRightSide;
    STA_abs_xy(Sprite_Attributes + 8, cpu.Y);
    STA_abs_xy(Sprite_Attributes + 12, cpu.Y);

FlipPUpRightSide:
    LDQ_abs_xy(cpu.A, Sprite_Attributes + 4, cpu.Y);
    ORA_imm(0b01000000);
    STA_abs_xy(Sprite_Attributes + 4, cpu.Y);
    LDQ_abs_xy(cpu.A, Sprite_Attributes + 12, cpu.Y);
    ORA_imm(0b01000000);
    STA_abs_xy(Sprite_Attributes + 12, cpu.Y);
PUpOfs:
    
    goto SprObjectOffscrChk;

EnemyGfxHandler:
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    STQ_zp(cpu.A, 0x02);
    LDQ_abs(cpu.A, Enemy_Rel_XPos);
    STQ_zp(cpu.A, 0x05);
    LDQ_abs_xy(cpu.Y, Enemy_SprDataOffset, cpu.X);
    STQ_zp(cpu.Y, 0xeb);
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, VerticalFlipFlag);
    LDQ_zp_xy(cpu.A, Enemy_MovingDir, cpu.X);
    STQ_zp(cpu.A, 0x03);
    LDQ_abs_xy(cpu.A, Enemy_SprAttrib, cpu.X);
    STQ_zp(cpu.A, 0x04);
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, PiranhaPlant);
    if (BNE()) goto CheckForRetainerObj;
    LDQ_zp_xy(cpu.Y, PiranhaPlant_Y_Speed, cpu.X);
    if (BMI()) goto CheckForRetainerObj;
    LDQ_abs_xy(cpu.Y, EnemyFrameTimer, cpu.X);
    if (BEQ()) goto CheckForRetainerObj;
    RET();

CheckForRetainerObj:
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    STQ_zp(cpu.A, 0xed);
    AND_imm(0b00011111);
    CopyAY();
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, RetainerObject);
    if (BNE()) goto CheckForBulletBillCV;
    LDQ_imm(cpu.Y, 0x00);
    LDQ_imm(cpu.A, 0x01);
    STQ_zp(cpu.A, 0x03);
    LDQ_imm(cpu.A, 0x15);

CheckForBulletBillCV:
    CMPQ_imm(cpu.A, BulletBill_CannonVar);
    if (BNE()) goto CheckForJumpspring;
    DEC_zp(0x02);
    LDQ_imm(cpu.A, 0x03);
    LDQ_abs_xy(cpu.Y, EnemyFrameTimer, cpu.X);
    if (BEQ()) goto SBBAt;
    ORA_imm(0b00100000);
SBBAt:
    STQ_zp(cpu.A, 0x04);
    LDQ_imm(cpu.Y, 0x00);
    STQ_zp(cpu.Y, 0xed);
    LDQ_imm(cpu.A, 0x08);

CheckForJumpspring:
    CMPQ_imm(cpu.A, JumpspringObject);
    if (BNE()) goto CheckForPodoboo;
    LDQ_imm(cpu.Y, 0x03);
    LDQ_abs(cpu.X, JumpspringAnimCtrl);
    LDQ_PRGTable(cpu.A, JumpspringFrameOffsets, cpu.X);

CheckForPodoboo:
    STQ_zp(cpu.A, 0xef);
    STQ_zp(cpu.Y, 0xec);
    LDQ_zp(cpu.X, ObjectOffset);
    CMPQ_imm(cpu.A, 0x0c);
    if (BNE()) goto CheckBowserGfxFlag;
    LDQ_zp_xy(cpu.A, Enemy_Y_Speed, cpu.X);
    if (BMI()) goto CheckBowserGfxFlag;
    INC_abs(VerticalFlipFlag);

CheckBowserGfxFlag:
    LDQ_abs(cpu.A, BowserGfxFlag);
    if (BEQ()) goto CheckForGoomba;
    LDQ_imm(cpu.Y, 0x16);
    CMPQ_imm(cpu.A, 0x01);
    if (BEQ()) goto SBwsrGfxOfs;
    INY();
SBwsrGfxOfs:
    STQ_zp(cpu.Y, 0xef);

CheckForGoomba:
    LDQ_zp(cpu.Y, 0xef);
    CMPQ_imm(cpu.Y, Goomba);
    if (BNE()) goto CheckBowserFront;
    LDQ_zp_xy(cpu.A, Enemy_State, cpu.X);
    CMPQ_imm(cpu.A, 0x02);
    if (BCC()) goto GmbaAnim;
    LDQ_imm(cpu.X, 0x04);
    STQ_zp(cpu.X, 0xec);
GmbaAnim:
    AND_imm(0b00100000);
    ORA_abs(TimerControl);
    if (BNE()) goto CheckBowserFront;
    LDQ_zp(cpu.A, FrameCounter);
    AND_imm(0b00001000);
    if (BNE()) goto CheckBowserFront;
    LDQ_zp(cpu.A, 0x03);
    EOR_imm(0b00000011);
    STQ_zp(cpu.A, 0x03);

CheckBowserFront:
    LDQ_PRGTable(cpu.A, EnemyAttributeData, cpu.Y);
    ORA_zp(0x04);
    STQ_zp(cpu.A, 0x04);
    LDQ_PRGTable(cpu.A, EnemyGfxTableOffsets, cpu.Y);
    CopyAX();
    LDQ_zp(cpu.Y, 0xec);
    LDQ_abs(cpu.A, BowserGfxFlag);
    if (BEQ()) goto CheckForSpiny;
    CMPQ_imm(cpu.A, 0x01);
    if (BNE()) goto CheckBowserRear;
    LDQ_abs(cpu.A, BowserBodyControls);
    if (BPL()) goto ChkFrontSte;
    LDQ_imm(cpu.X, 0xde);
ChkFrontSte:
    LDQ_zp(cpu.A, 0xed);
    AND_imm(0b00100000);
    if (BEQ()) goto DrawBowser;

FlipBowserOver:
    STQ_abs(cpu.X, VerticalFlipFlag);

DrawBowser:
    goto DrawEnemyObject;

CheckBowserRear:
    LDQ_abs(cpu.A, BowserBodyControls);
    AND_imm(0x01);
    if (BEQ()) goto ChkRearSte;
    LDQ_imm(cpu.X, 0xe4);
ChkRearSte:
    LDQ_zp(cpu.A, 0xed);
    AND_imm(0b00100000);
    if (BEQ()) goto DrawBowser;
    LDQ_zp(cpu.A, 0x02);
    SEC();
    SBC_imm(0x10);
    STQ_zp(cpu.A, 0x02);
    
    goto FlipBowserOver;

CheckForSpiny:
    CMPQ_imm(cpu.X, 0x24);
    if (BNE()) goto CheckForLakitu;
    CMPQ_imm(cpu.Y, 0x05);
    if (BNE()) goto NotEgg;
    LDQ_imm(cpu.X, 0x30);
    LDQ_imm(cpu.A, 0x02);
    STQ_zp(cpu.A, 0x03);
    LDQ_imm(cpu.A, 0x05);
    STQ_zp(cpu.A, 0xec);
NotEgg:
    
    goto CheckForHammerBro;

CheckForLakitu:
    CMPQ_imm(cpu.X, 0x90);
    if (BNE()) goto CheckUpsideDownShell;
    LDQ_zp(cpu.A, 0xed);
    AND_imm(0b00100000);
    if (BNE()) goto NoLAFr;
    LDQ_abs(cpu.A, FrenzyEnemyTimer);
    CMPQ_imm(cpu.A, 0x10);
    if (BCS()) goto NoLAFr;
    LDQ_imm(cpu.X, 0x96);
NoLAFr:
    
    goto CheckDefeatedState;

CheckUpsideDownShell:
    LDQ_zp(cpu.A, 0xef);
    CMPQ_imm(cpu.A, 0x04);
    if (BCS()) goto CheckRightSideUpShell;
    CMPQ_imm(cpu.Y, 0x02);
    if (BCC()) goto CheckRightSideUpShell;
    LDQ_imm(cpu.X, 0x5a);
    LDQ_zp(cpu.Y, 0xef);
    CMPQ_imm(cpu.Y, BuzzyBeetle);
    if (BNE()) goto CheckRightSideUpShell;
    LDQ_imm(cpu.X, 0x7e);
    INC_zp(0x02);

CheckRightSideUpShell:
    LDQ_zp(cpu.A, 0xec);
    CMPQ_imm(cpu.A, 0x04);
    if (BNE()) goto CheckForHammerBro;
    LDQ_imm(cpu.X, 0x72);
    INC_zp(0x02);
    LDQ_zp(cpu.Y, 0xef);
    CMPQ_imm(cpu.Y, BuzzyBeetle);
    if (BEQ()) goto CheckForDefdGoomba;
    LDQ_imm(cpu.X, 0x66);
    INC_zp(0x02);

CheckForDefdGoomba:
    CMPQ_imm(cpu.Y, Goomba);
    if (BNE()) goto CheckForHammerBro;
    LDQ_imm(cpu.X, 0x54);
    LDQ_zp(cpu.A, 0xed);
    AND_imm(0b00100000);
    if (BNE()) goto CheckForHammerBro;
    LDQ_imm(cpu.X, 0x8a);
    DEC_zp(0x02);

CheckForHammerBro:
    LDQ_zp(cpu.Y, ObjectOffset);
    LDQ_zp(cpu.A, 0xef);
    CMPQ_imm(cpu.A, HammerBro);
    if (BNE()) goto CheckForBloober;
    LDQ_zp(cpu.A, 0xed);
    if (BEQ()) goto CheckToAnimateEnemy;
    AND_imm(0b00001000);
    if (BEQ()) goto CheckDefeatedState;
    LDQ_imm(cpu.X, 0xb4);
    if (BNE()) goto CheckToAnimateEnemy;

CheckForBloober:
    CMPQ_imm(cpu.X, 0x48);
    if (BEQ()) goto CheckToAnimateEnemy;
    LDQ_abs_xy(cpu.A, EnemyIntervalTimer, cpu.Y);
    CMPQ_imm(cpu.A, 0x05);
    if (BCS()) goto CheckDefeatedState;
    CMPQ_imm(cpu.X, 0x3c);
    if (BNE()) goto CheckToAnimateEnemy;
    CMPQ_imm(cpu.A, 0x01);
    if (BEQ()) goto CheckDefeatedState;
    INC_zp(0x02);
    INC_zp(0x02);
    INC_zp(0x02);
    
    goto CheckAnimationStop;

CheckToAnimateEnemy:
    LDQ_zp(cpu.A, 0xef);
    CMPQ_imm(cpu.A, Goomba);
    if (BEQ()) goto CheckDefeatedState;
    CMPQ_imm(cpu.A, 0x08);
    if (BEQ()) goto CheckDefeatedState;
    CMPQ_imm(cpu.A, Podoboo);
    if (BEQ()) goto CheckDefeatedState;
    CMPQ_imm(cpu.A, 0x18);
    if (BCS()) goto CheckDefeatedState;
    LDQ_imm(cpu.Y, 0x00);
    CMPQ_imm(cpu.A, 0x15);
    if (BNE()) goto CheckForSecondFrame;
    INY();
    LDQ_abs(cpu.A, WorldNumber);
    CMPQ_imm(cpu.A, World8);
    if (BCS()) goto CheckDefeatedState;
    LDQ_imm(cpu.X, 0xa2);
    LDQ_imm(cpu.A, 0x03);
    STQ_zp(cpu.A, 0xec);
    if (BNE()) goto CheckDefeatedState;

CheckForSecondFrame:
    LDQ_zp(cpu.A, FrameCounter);
    AND_PRGTable(EnemyAnimTimingBMask, cpu.Y);
    if (BNE()) goto CheckDefeatedState;

CheckAnimationStop:
    LDQ_zp(cpu.A, 0xed);
    AND_imm(0b10100000);
    ORA_abs(TimerControl);
    if (BNE()) goto CheckDefeatedState;
    CopyXA();
    CLC();
    ADC_imm(0x06);
    CopyAX();

CheckDefeatedState:
    LDQ_zp(cpu.A, 0xed);
    AND_imm(0b00100000);
    if (BEQ()) goto DrawEnemyObject;
    LDQ_zp(cpu.A, 0xef);
    CMPQ_imm(cpu.A, 0x04);
    if (BCC()) goto DrawEnemyObject;
    LDQ_imm(cpu.Y, 0x01);
    STQ_abs(cpu.Y, VerticalFlipFlag);
    DEY();
    STQ_zp(cpu.Y, 0xec);

DrawEnemyObject:
    LDQ_zp(cpu.Y, 0xeb);
    CALL(DrawEnemyObjRow);
    CALL(DrawEnemyObjRow);
    CALL(DrawEnemyObjRow);
    LDQ_zp(cpu.X, ObjectOffset);
    LDQ_abs_xy(cpu.Y, Enemy_SprDataOffset, cpu.X);
    LDQ_zp(cpu.A, 0xef);
    CMPQ_imm(cpu.A, 0x08);
    if (BNE()) goto CheckForVerticalFlip;

SkipToOffScrChk:
    goto SprObjectOffscrChk;

CheckForVerticalFlip:
    LDQ_abs(cpu.A, VerticalFlipFlag);
    if (BEQ()) goto CheckForESymmetry;
    LDQ_abs_xy(cpu.A, Sprite_Attributes, cpu.Y);
    ORA_imm(0b10000000);
    INY();
    INY();
    CALL(DumpSixSpr);
    DEY();
    DEY();
    CopyYA();
    CopyAX();
    LDQ_zp(cpu.A, 0xef);
    CMPQ_imm(cpu.A, HammerBro);
    if (BEQ()) goto FlipEnemyVertically;
    CMPQ_imm(cpu.A, Lakitu);
    if (BEQ()) goto FlipEnemyVertically;
    CMPQ_imm(cpu.A, 0x15);
    if (BCS()) goto FlipEnemyVertically;
    CopyXA();
    CLC();
    ADC_imm(0x08);
    CopyAX();

FlipEnemyVertically:
    LDQ_abs_xy(cpu.A, Sprite_Tilenumber, cpu.X);
    PHA();
    LDQ_abs_xy(cpu.A, Sprite_Tilenumber + 4, cpu.X);
    PHA();
    LDQ_abs_xy(cpu.A, Sprite_Tilenumber + 16, cpu.Y);
    STA_abs_xy(Sprite_Tilenumber, cpu.X);
    LDQ_abs_xy(cpu.A, Sprite_Tilenumber + 20, cpu.Y);
    STA_abs_xy(Sprite_Tilenumber + 4, cpu.X);
    PLA();
    STA_abs_xy(Sprite_Tilenumber + 20, cpu.Y);
    PLA();
    STA_abs_xy(Sprite_Tilenumber + 16, cpu.Y);

CheckForESymmetry:
    LDQ_abs(cpu.A, BowserGfxFlag);
    if (BNE()) goto SkipToOffScrChk;
    LDQ_zp(cpu.A, 0xef);
    LDQ_zp(cpu.X, 0xec);
    CMPQ_imm(cpu.A, 0x05);
    if (BNE()) goto ContES;
    
    goto SprObjectOffscrChk;
ContES:
    CMPQ_imm(cpu.A, Bloober);
    if (BEQ()) goto MirrorEnemyGfx;
    CMPQ_imm(cpu.A, PiranhaPlant);
    if (BEQ()) goto MirrorEnemyGfx;
    CMPQ_imm(cpu.A, Podoboo);
    if (BEQ()) goto MirrorEnemyGfx;
    CMPQ_imm(cpu.A, Spiny);
    if (BNE()) goto ESRtnr;
    CMPQ_imm(cpu.X, 0x05);
    if (BNE()) goto CheckToMirrorLakitu;

ESRtnr:
    CMPQ_imm(cpu.A, 0x15);
    if (BNE()) goto SpnySC;
    LDQ_imm(cpu.A, 0x42);
    STA_abs_xy(Sprite_Attributes + 20, cpu.Y);
SpnySC:
    CMPQ_imm(cpu.X, 0x02);
    if (BCC()) goto CheckToMirrorLakitu;

MirrorEnemyGfx:
    LDQ_abs(cpu.A, BowserGfxFlag);
    if (BNE()) goto CheckToMirrorLakitu;
    LDQ_abs_xy(cpu.A, Sprite_Attributes, cpu.Y);
    AND_imm(0b10100011);
    STA_abs_xy(Sprite_Attributes, cpu.Y);
    STA_abs_xy(Sprite_Attributes + 8, cpu.Y);
    STA_abs_xy(Sprite_Attributes + 16, cpu.Y);
    ORA_imm(0b01000000);
    CMPQ_imm(cpu.X, 0x05);
    if (BNE()) goto EggExc;
    ORA_imm(0b10000000);
EggExc:
    STA_abs_xy(Sprite_Attributes + 4, cpu.Y);
    STA_abs_xy(Sprite_Attributes + 12, cpu.Y);
    STA_abs_xy(Sprite_Attributes + 20, cpu.Y);
    CMPQ_imm(cpu.X, 0x04);
    if (BNE()) goto CheckToMirrorLakitu;
    LDQ_abs_xy(cpu.A, Sprite_Attributes + 8, cpu.Y);
    ORA_imm(0b10000000);
    STA_abs_xy(Sprite_Attributes + 8, cpu.Y);
    STA_abs_xy(Sprite_Attributes + 16, cpu.Y);
    ORA_imm(0b01000000);
    STA_abs_xy(Sprite_Attributes + 12, cpu.Y);
    STA_abs_xy(Sprite_Attributes + 20, cpu.Y);

CheckToMirrorLakitu:
    LDQ_zp(cpu.A, 0xef);
    CMPQ_imm(cpu.A, Lakitu);
    if (BNE()) goto CheckToMirrorJSpring;
    LDQ_abs(cpu.A, VerticalFlipFlag);
    if (BNE()) goto NVFLak;
    LDQ_abs_xy(cpu.A, Sprite_Attributes + 16, cpu.Y);
    AND_imm(0b10000001);
    STA_abs_xy(Sprite_Attributes + 16, cpu.Y);
    LDQ_abs_xy(cpu.A, Sprite_Attributes + 20, cpu.Y);
    ORA_imm(0b01000001);
    STA_abs_xy(Sprite_Attributes + 20, cpu.Y);
    LDQ_abs(cpu.X, FrenzyEnemyTimer);
    CMPQ_imm(cpu.X, 0x10);
    if (BCS()) goto SprObjectOffscrChk;
    STA_abs_xy(Sprite_Attributes + 12, cpu.Y);
    AND_imm(0b10000001);
    STA_abs_xy(Sprite_Attributes + 8, cpu.Y);
    if (BCC()) goto SprObjectOffscrChk;

NVFLak:
    LDQ_abs_xy(cpu.A, Sprite_Attributes, cpu.Y);
    AND_imm(0b10000001);
    STA_abs_xy(Sprite_Attributes, cpu.Y);
    LDQ_abs_xy(cpu.A, Sprite_Attributes + 4, cpu.Y);
    ORA_imm(0b01000001);
    STA_abs_xy(Sprite_Attributes + 4, cpu.Y);

CheckToMirrorJSpring:
    LDQ_zp(cpu.A, 0xef);
    CMPQ_imm(cpu.A, 0x18);
    if (BCC()) goto SprObjectOffscrChk;
    LDQ_imm(cpu.A, 0x82);
    STA_abs_xy(Sprite_Attributes + 8, cpu.Y);
    STA_abs_xy(Sprite_Attributes + 16, cpu.Y);
    ORA_imm(0b01000000);
    STA_abs_xy(Sprite_Attributes + 12, cpu.Y);
    STA_abs_xy(Sprite_Attributes + 20, cpu.Y);

SprObjectOffscrChk:
    LDQ_zp(cpu.X, ObjectOffset);
    LDQ_abs(cpu.A, Enemy_OffscreenBits);
    LSR_acc();
    LSR_acc();
    LSR_acc();
    PHA();
    if (BCC()) goto LcChk;
    LDQ_imm(cpu.A, 0x04);
    CALL(MoveESprColOffscreen);
LcChk:
    PLA();
    LSR_acc();
    PHA();
    if (BCC()) goto Row3C;
    LDQ_imm(cpu.A, 0x00);
    CALL(MoveESprColOffscreen);
Row3C:
    PLA();
    LSR_acc();
    LSR_acc();
    PHA();
    if (BCC()) goto Row23C;
    LDQ_imm(cpu.A, 0x10);
    CALL(MoveESprRowOffscreen);
Row23C:
    PLA();
    LSR_acc();
    PHA();
    if (BCC()) goto AllRowC;
    LDQ_imm(cpu.A, 0x08);
    CALL(MoveESprRowOffscreen);
AllRowC:
    PLA();
    LSR_acc();
    if (BCC()) goto ExEGHandler;
    CALL(MoveESprRowOffscreen);
    LDQ_zp_xy(cpu.A, Enemy_ID, cpu.X);
    CMPQ_imm(cpu.A, Podoboo);
    if (BEQ()) goto ExEGHandler;
    LDQ_zp_xy(cpu.A, Enemy_Y_HighPos, cpu.X);
    CMPQ_imm(cpu.A, 0x02);
    if (BNE()) goto ExEGHandler;
    CALL(EraseEnemyObject);

ExEGHandler:
    RET();

DrawEnemyObjRow:
    LDQ_PRGTable(cpu.A, EnemyGraphicsTable, cpu.X);
    STQ_zp(cpu.A, 0x00);
    LDQ_PRGTable(cpu.A, EnemyGraphicsTable, cpu.X + 1);

DrawOneSpriteRow:
    STQ_zp(cpu.A, 0x01);
    
    goto DrawSpriteObject;

MoveESprRowOffscreen:
    CLC();
    ADC_abs_x(Enemy_SprDataOffset);
    CopyAY();
    LDQ_imm(cpu.A, 0xf8);
    
    goto DumpTwoSpr;

MoveESprColOffscreen:
    CLC();
    ADC_abs_x(Enemy_SprDataOffset);
    CopyAY();
    CALL(MoveColOffscreen);
    STA_abs_xy(Sprite_Data + 16, cpu.Y);
    RET();

DrawBlock:
    LDQ_abs(cpu.A, Block_Rel_YPos);
    STQ_zp(cpu.A, 0x02);
    LDQ_abs(cpu.A, Block_Rel_XPos);
    STQ_zp(cpu.A, 0x05);
    LDQ_imm(cpu.A, 0x03);
    STQ_zp(cpu.A, 0x04);
    LSR_acc();
    STQ_zp(cpu.A, 0x03);
    LDQ_abs_xy(cpu.Y, Block_SprDataOffset, cpu.X);
    LDQ_imm(cpu.X, 0x00);
DBlkLoop:
    LDQ_PRGTable(cpu.A, DefaultBlockObjTiles, cpu.X);
    STQ_zp(cpu.A, 0x00);
    LDQ_PRGTable(cpu.A, DefaultBlockObjTiles, cpu.X + 1);
    CALL(DrawOneSpriteRow);
    CMPQ_imm(cpu.X, 0x04);
    if (BNE()) goto DBlkLoop;
    LDQ_zp(cpu.X, ObjectOffset);
    LDQ_abs_xy(cpu.Y, Block_SprDataOffset, cpu.X);
    LDQ_abs(cpu.A, AreaType);
    CMPQ_imm(cpu.A, 0x01);
    if (BEQ()) goto ChkRep;
    LDQ_imm(cpu.A, 0x86);
    STA_abs_xy(Sprite_Tilenumber, cpu.Y);
    STA_abs_xy(Sprite_Tilenumber + 4, cpu.Y);
ChkRep:
    LDQ_abs_xy(cpu.A, Block_Metatile, cpu.X);
    CMPQ_imm(cpu.A, 0xc4);
    if (BNE()) goto BlkOffscr;
    LDQ_imm(cpu.A, 0x87);
    INY();
    CALL(DumpFourSpr);
    DEY();
    LDQ_imm(cpu.A, 0x03);
    LDQ_abs(cpu.X, AreaType);
    DEX();
    if (BEQ()) goto SetBFlip;
    LSR_acc();
SetBFlip:
    LDQ_zp(cpu.X, ObjectOffset);
    STA_abs_xy(Sprite_Attributes, cpu.Y);
    ORA_imm(0b01000000);
    STA_abs_xy(Sprite_Attributes + 4, cpu.Y);
    ORA_imm(0b10000000);
    STA_abs_xy(Sprite_Attributes + 12, cpu.Y);
    AND_imm(0b10000011);
    STA_abs_xy(Sprite_Attributes + 8, cpu.Y);
BlkOffscr:
    LDQ_abs(cpu.A, Block_OffscreenBits);
    PHA();
    AND_imm(0b00000100);

#ifndef REMOVE_RESIDUAL
    if (BEQ()) goto PullOfsB;
    LDQ_imm(cpu.A, 0xf8);
    STA_abs_xy(Sprite_Y_Position + 4, cpu.Y);
    STA_abs_xy(Sprite_Y_Position + 12, cpu.Y);
    
PullOfsB:
#endif
    PLA();

ChkLeftCo:
    AND_imm(0b00001000);
    if (BEQ()) goto ExDBlk;

MoveColOffscreen:
    LDQ_imm(cpu.A, 0xf8);
    STA_abs_xy(Sprite_Y_Position, cpu.Y);
    STA_abs_xy(Sprite_Y_Position + 8, cpu.Y);
ExDBlk:
    RET();

DrawBrickChunks:
    LDQ_imm(cpu.A, 0x02);
    STQ_zp(cpu.A, 0x00);
    LDQ_imm(cpu.A, 0x75);
    LDQ_zp(cpu.Y, GameEngineSubroutine);
    CMPQ_imm(cpu.Y, 0x05);
    if (BEQ()) goto DChunks;
    LDQ_imm(cpu.A, 0x03);
    STQ_zp(cpu.A, 0x00);
    LDQ_imm(cpu.A, 0x84);
DChunks:
    LDQ_abs_xy(cpu.Y, Block_SprDataOffset, cpu.X);
    INY();
    CALL(DumpFourSpr);
    LDQ_zp(cpu.A, FrameCounter);
    ASL_acc();
    ASL_acc();
    ASL_acc();
    ASL_acc();
    AND_imm(0xc0);
    ORA_zp(0x00);
    INY();
    CALL(DumpFourSpr);
    DEY();
    DEY();
    LDQ_abs(cpu.A, Block_Rel_YPos);
    CALL(DumpTwoSpr);
    LDQ_abs(cpu.A, Block_Rel_XPos);
    STA_abs_xy(Sprite_X_Position, cpu.Y);
    LDQ_abs_xy(cpu.A, Block_Orig_XPos, cpu.X);
    SEC();
    SBC_abs(ScreenLeft_X_Pos);
    STQ_zp(cpu.A, 0x00);
    SEC();
    SBC_abs(Block_Rel_XPos);
    ADC_zp(0x00);
    ADC_imm(0x06);
    STA_abs_xy(Sprite_X_Position + 4, cpu.Y);
    LDQ_abs(cpu.A, Block_Rel_YPos + 1);
    STA_abs_xy(Sprite_Y_Position + 8, cpu.Y);
    STA_abs_xy(Sprite_Y_Position + 12, cpu.Y);
    LDQ_abs(cpu.A, Block_Rel_XPos + 1);
    STA_abs_xy(Sprite_X_Position + 8, cpu.Y);
    LDQ_zp(cpu.A, 0x00);
    SEC();
    SBC_abs(Block_Rel_XPos + 1);
    ADC_zp(0x00);
    ADC_imm(0x06);
    STA_abs_xy(Sprite_X_Position + 12, cpu.Y);
    LDQ_abs(cpu.A, Block_OffscreenBits);
    CALL(ChkLeftCo);
    LDQ_abs(cpu.A, Block_OffscreenBits);
    ASL_acc();
    if (BCC()) goto ChnkOfs;
    LDQ_imm(cpu.A, 0xf8);
    CALL(DumpTwoSpr);
ChnkOfs:
    LDQ_zp(cpu.A, 0x00);
    if (BPL()) goto ExBCDr;
    LDQ_abs_xy(cpu.A, Sprite_X_Position, cpu.Y);
    CMPA_abs_xy(Sprite_X_Position + 4, cpu.Y);
    if (BCC()) goto ExBCDr;
    LDQ_imm(cpu.A, 0xf8);
    STA_abs_xy(Sprite_Y_Position + 4, cpu.Y);
    STA_abs_xy(Sprite_Y_Position + 12, cpu.Y);
ExBCDr:
    RET();

DrawFireball:
    LDQ_abs_xy(cpu.Y, FBall_SprDataOffset, cpu.X);
    LDQ_abs(cpu.A, Fireball_Rel_YPos);
    STA_abs_xy(Sprite_Y_Position, cpu.Y);
    LDQ_abs(cpu.A, Fireball_Rel_XPos);
    STA_abs_xy(Sprite_X_Position, cpu.Y);

DrawFirebar:
    LDQ_zp(cpu.A, FrameCounter);
    LSR_acc();
    LSR_acc();
    PHA();
    AND_imm(0x01);
    EOR_imm(0x64);
    STA_abs_xy(Sprite_Tilenumber, cpu.Y);
    PLA();
    LSR_acc();
    LSR_acc();
    LDQ_imm(cpu.A, 0x02);
    if (BCC()) goto FireA;
    ORA_imm(0b11000000);
FireA:
    STA_abs_xy(Sprite_Attributes, cpu.Y);
    RET();

DrawExplosion_Fireball:
    LDQ_abs_xy(cpu.Y, Alt_SprDataOffset, cpu.X);
    LDQ_zp_xy(cpu.A, Fireball_State, cpu.X);
    INC_zp_x(Fireball_State);
    LSR_acc();
    AND_imm(0b00000111);
    CMPQ_imm(cpu.A, 0x03);
    if (BCS()) goto KillFireBall;

DrawExplosion_Fireworks:
    CopyAX();
    LDQ_PRGTable(cpu.A, ExplosionTiles, cpu.X);
    INY();
    CALL(DumpFourSpr);
    DEY();
    LDQ_zp(cpu.X, ObjectOffset);
    LDQ_abs(cpu.A, Fireball_Rel_YPos);
    SEC();
    SBC_imm(0x04);
    STA_abs_xy(Sprite_Y_Position, cpu.Y);
    STA_abs_xy(Sprite_Y_Position + 8, cpu.Y);
    CLC();
    ADC_imm(0x08);
    STA_abs_xy(Sprite_Y_Position + 4, cpu.Y);
    STA_abs_xy(Sprite_Y_Position + 12, cpu.Y);
    LDQ_abs(cpu.A, Fireball_Rel_XPos);
    SEC();
    SBC_imm(0x04);
    STA_abs_xy(Sprite_X_Position, cpu.Y);
    STA_abs_xy(Sprite_X_Position + 4, cpu.Y);
    CLC();
    ADC_imm(0x08);
    STA_abs_xy(Sprite_X_Position + 8, cpu.Y);
    STA_abs_xy(Sprite_X_Position + 12, cpu.Y);
    LDQ_imm(cpu.A, 0x02);
    STA_abs_xy(Sprite_Attributes, cpu.Y);
    LDQ_imm(cpu.A, 0x82);
    STA_abs_xy(Sprite_Attributes + 4, cpu.Y);
    LDQ_imm(cpu.A, 0x42);
    STA_abs_xy(Sprite_Attributes + 8, cpu.Y);
    LDQ_imm(cpu.A, 0xc2);
    STA_abs_xy(Sprite_Attributes + 12, cpu.Y);
    RET();

KillFireBall:
    LDQ_imm(cpu.A, 0x00);
    STQ_zp_xy(cpu.A, Fireball_State, cpu.X);
    RET();

DrawSmallPlatform:
    LDQ_abs_xy(cpu.Y, Enemy_SprDataOffset, cpu.X);
    LDQ_imm(cpu.A, 0x5b);
    INY();
    CALL(DumpSixSpr);
    INY();
    LDQ_imm(cpu.A, 0x02);
    CALL(DumpSixSpr);
    DEY();
    DEY();
    LDQ_abs(cpu.A, Enemy_Rel_XPos);
    STA_abs_xy(Sprite_X_Position, cpu.Y);
    STA_abs_xy(Sprite_X_Position + 12, cpu.Y);
    CLC();
    ADC_imm(0x08);
    STA_abs_xy(Sprite_X_Position + 4, cpu.Y);
    STA_abs_xy(Sprite_X_Position + 16, cpu.Y);
    CLC();
    ADC_imm(0x08);
    STA_abs_xy(Sprite_X_Position + 8, cpu.Y);
    STA_abs_xy(Sprite_X_Position + 20, cpu.Y);
    LDQ_zp_xy(cpu.A, Enemy_Y_Position, cpu.X);
    CopyAX();
    PHA();
    CMPQ_imm(cpu.X, 0x20);
    if (BCS()) goto TopSP;
    LDQ_imm(cpu.A, 0xf8);
TopSP:
    CALL(DumpThreeSpr);
    PLA();
    CLC();
    ADC_imm(0x80);
    CopyAX();
    CMPQ_imm(cpu.X, 0x20);
    if (BCS()) goto BotSP;
    LDQ_imm(cpu.A, 0xf8);
BotSP:
    STA_abs_xy(Sprite_Y_Position + 12, cpu.Y);
    STA_abs_xy(Sprite_Y_Position + 16, cpu.Y);
    STA_abs_xy(Sprite_Y_Position + 20, cpu.Y);
    LDQ_abs(cpu.A, Enemy_OffscreenBits);
    PHA();
    AND_imm(0b00001000);
    if (BEQ()) goto SOfs;
    LDQ_imm(cpu.A, 0xf8);
    STA_abs_xy(Sprite_Y_Position, cpu.Y);
    STA_abs_xy(Sprite_Y_Position + 12, cpu.Y);
SOfs:
    PLA();
    PHA();
    AND_imm(0b00000100);
    if (BEQ()) goto SOfs2;
    LDQ_imm(cpu.A, 0xf8);
    STA_abs_xy(Sprite_Y_Position + 4, cpu.Y);
    STA_abs_xy(Sprite_Y_Position + 16, cpu.Y);
SOfs2:
    PLA();
    AND_imm(0b00000010);
    if (BEQ()) goto ExSPl;
    LDQ_imm(cpu.A, 0xf8);
    STA_abs_xy(Sprite_Y_Position + 8, cpu.Y);
    STA_abs_xy(Sprite_Y_Position + 20, cpu.Y);
ExSPl:
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

DrawBubble:
    LDQ_zp(cpu.Y, Player_Y_HighPos);
    DEY();
    if (BNE()) goto ExDBub;
    LDQ_abs(cpu.A, Bubble_OffscreenBits);
    AND_imm(0b00001000);
    if (BNE()) goto ExDBub;
    LDQ_abs_xy(cpu.Y, Bubble_SprDataOffset, cpu.X);
    LDQ_abs(cpu.A, Bubble_Rel_XPos);
    STA_abs_xy(Sprite_X_Position, cpu.Y);
    LDQ_abs(cpu.A, Bubble_Rel_YPos);
    STA_abs_xy(Sprite_Y_Position, cpu.Y);
    LDQ_imm(cpu.A, 0x74);
    STA_abs_xy(Sprite_Tilenumber, cpu.Y);
    LDQ_imm(cpu.A, 0x02);
    STA_abs_xy(Sprite_Attributes, cpu.Y);
ExDBub:
    RET();

PlayerGfxHandler:
    LDQ_abs(cpu.A, InjuryTimer);
    if (BEQ()) goto CntPl;
    LDQ_zp(cpu.A, FrameCounter);
    LSR_acc();
    if (BCS()) goto ExPGH;

CntPl:
    LDQ_zp(cpu.A, GameEngineSubroutine);
    CMPQ_imm(cpu.A, 0x0b);
    if (BEQ()) goto PlayerKilled;
    LDQ_abs(cpu.A, PlayerChangeSizeFlag);
    if (BNE()) goto DoChangeSize;
    LDQ_abs(cpu.Y, SwimmingFlag);
    if (BEQ()) goto FindPlayerAction;
    LDQ_zp(cpu.A, Player_State);
    CMPQ_imm(cpu.A, 0x00);
    if (BEQ()) goto FindPlayerAction;
    CALL(FindPlayerAction);
    LDQ_zp(cpu.A, FrameCounter);
    AND_imm(0b00000100);
    if (BNE()) goto ExPGH;
    CopyAX();
    LDQ_abs(cpu.Y, Player_SprDataOffset);
    LDQ_zp(cpu.A, PlayerFacingDir);
    LSR_acc();
    if (BCS()) goto SwimKT;
    INY();
    INY();
    INY();
    INY();
SwimKT:
    LDQ_abs(cpu.A, PlayerSize);
    if (BEQ()) goto BigKTS;
    LDQ_abs_xy(cpu.A, Sprite_Tilenumber + 24, cpu.Y);
    CMPQ_imm(cpu.A, PlayerGraphicsTable[0x9e]);
    if (BEQ()) goto ExPGH;
    INX();
BigKTS:
    LDQ_PRGTable(cpu.A, SwimKickTileNum, cpu.X);
    STA_abs_xy(Sprite_Tilenumber + 24, cpu.Y);
ExPGH:
    RET();

FindPlayerAction:
    CALL(ProcessPlayerAction);
    
    goto PlayerGfxProcessing;

DoChangeSize:
    CALL(HandleChangeSize);
    
    goto PlayerGfxProcessing;

PlayerKilled:
    LDQ_imm(cpu.Y, 0x0e);
    LDQ_PRGTable(cpu.A, PlayerGfxTblOffsets, cpu.Y);

PlayerGfxProcessing:
    STQ_abs(cpu.A, PlayerGfxOffset);
    LDQ_imm(cpu.A, 0x04);
    CALL(RenderPlayerSub);
    CALL(ChkForPlayerAttrib);
    LDQ_abs(cpu.A, FireballThrowingTimer);
    if (BEQ()) goto PlayerOffscreenChk;
    LDQ_imm(cpu.Y, 0x00);
    LDQ_abs(cpu.A, PlayerAnimTimer);
    CMPQ_abs(cpu.A, FireballThrowingTimer);
    STQ_abs(cpu.Y, FireballThrowingTimer);
    if (BCS()) goto PlayerOffscreenChk;
    STQ_abs(cpu.A, FireballThrowingTimer);
    LDQ_imm(cpu.Y, 0x07);
    LDQ_PRGTable(cpu.A, PlayerGfxTblOffsets, cpu.Y);
    STQ_abs(cpu.A, PlayerGfxOffset);
    LDQ_imm(cpu.Y, 0x04);
    LDQ_zp(cpu.A, Player_X_Speed);
    ORA_zp(Left_Right_Buttons);
    if (BEQ()) goto SUpdR;
    DEY();
SUpdR:
    CopyYA();
    CALL(RenderPlayerSub);

PlayerOffscreenChk:
    LDQ_abs(cpu.A, Player_OffscreenBits);
    LSR_acc();
    LSR_acc();
    LSR_acc();
    LSR_acc();
    STQ_zp(cpu.A, 0x00);
    LDQ_imm(cpu.X, 0x03);
    LDQ_abs(cpu.A, Player_SprDataOffset);
    CLC();
    ADC_imm(0x18);
    CopyAY();
PROfsLoop:
    LDQ_imm(cpu.A, 0xf8);
    LSR_zp(0x00);
    if (BCC()) goto NPROffscr;
    CALL(DumpTwoSpr);
NPROffscr:
    CopyYA();
    SEC();
    SBC_imm(0x08);
    CopyAY();
    DEX();
    if (BPL()) goto PROfsLoop;
    RET();

DrawPlayer_Intermediate:
    LDQ_imm(cpu.X, 0x05);
PIntLoop:
    LDQ_PRGTable(cpu.A, IntermediatePlayerData, cpu.X);
    STQ_zp_xy(cpu.A, 0x02, cpu.X);
    DEX();
    if (BPL()) goto PIntLoop;
    LDQ_imm(cpu.X, 0xb8);
    LDQ_imm(cpu.Y, 0x04);
    CALL(DrawPlayerLoop);
    LDQ_abs(cpu.A, Sprite_Attributes + 36);
    ORA_imm(0b01000000);
    STQ_abs(cpu.A, Sprite_Attributes + 32);
    RET();

RenderPlayerSub:
    STQ_zp(cpu.A, 0x07);
    LDQ_abs(cpu.A, Player_Rel_XPos);
    STQ_abs(cpu.A, Player_Pos_ForScroll);
    STQ_zp(cpu.A, 0x05);
    LDQ_abs(cpu.A, Player_Rel_YPos);
    STQ_zp(cpu.A, 0x02);
    LDQ_zp(cpu.A, PlayerFacingDir);
    STQ_zp(cpu.A, 0x03);
    LDQ_abs(cpu.A, Player_SprAttrib);
    STQ_zp(cpu.A, 0x04);
    LDQ_abs(cpu.X, PlayerGfxOffset);
    LDQ_abs(cpu.Y, Player_SprDataOffset);

DrawPlayerLoop:
    LDQ_PRGTable(cpu.A, PlayerGraphicsTable, cpu.X);
    STQ_zp(cpu.A, 0x00);
    LDQ_PRGTable(cpu.A, PlayerGraphicsTable, cpu.X + 1);
    CALL(DrawOneSpriteRow);
    DEC_zp(0x07);
    if (BNE()) goto DrawPlayerLoop;
    RET();

ProcessPlayerAction:
    LDQ_zp(cpu.A, Player_State);
    CMPQ_imm(cpu.A, 0x03);
    if (BEQ()) goto ActionClimbing;
    CMPQ_imm(cpu.A, 0x02);
    if (BEQ()) goto ActionFalling;
    CMPQ_imm(cpu.A, 0x01);
    if (BNE()) goto ProcOnGroundActs;
    LDQ_abs(cpu.A, SwimmingFlag);
    if (BNE()) goto ActionSwimming;
    LDQ_imm(cpu.Y, 0x06);
    LDQ_abs(cpu.A, CrouchingFlag);
    if (BNE()) goto NonAnimatedActs;
    LDQ_imm(cpu.Y, 0x00);
    
    goto NonAnimatedActs;

ProcOnGroundActs:
    LDQ_imm(cpu.Y, 0x06);
    LDQ_abs(cpu.A, CrouchingFlag);
    if (BNE()) goto NonAnimatedActs;
    LDQ_imm(cpu.Y, 0x02);
    LDQ_zp(cpu.A, Player_X_Speed);
    ORA_zp(Left_Right_Buttons);
    if (BEQ()) goto NonAnimatedActs;
    LDQ_abs(cpu.A, Player_XSpeedAbsolute);
    CMPQ_imm(cpu.A, 0x09);
    if (BCC()) goto ActionWalkRun;
    LDQ_zp(cpu.A, Player_MovingDir);
    AND_zp(PlayerFacingDir);
    if (BNE()) goto ActionWalkRun;
    INY();

NonAnimatedActs:
    CALL(GetGfxOffsetAdder);
    LDQ_imm(cpu.A, 0x00);
    STQ_abs(cpu.A, PlayerAnimCtrl);
    LDQ_PRGTable(cpu.A, PlayerGfxTblOffsets, cpu.Y);
    RET();

ActionFalling:
    LDQ_imm(cpu.Y, 0x04);
    CALL(GetGfxOffsetAdder);
    
    goto GetCurrentAnimOffset;

ActionWalkRun:
    LDQ_imm(cpu.Y, 0x04);
    CALL(GetGfxOffsetAdder);
    
    goto FourFrameExtent;

ActionClimbing:
    LDQ_imm(cpu.Y, 0x05);
    LDQ_zp(cpu.A, Player_Y_Speed);
    if (BEQ()) goto NonAnimatedActs;
    CALL(GetGfxOffsetAdder);
    
    goto ThreeFrameExtent;

ActionSwimming:
    LDQ_imm(cpu.Y, 0x01);
    CALL(GetGfxOffsetAdder);
    LDQ_abs(cpu.A, JumpSwimTimer);
    ORA_abs(PlayerAnimCtrl);
    if (BNE()) goto FourFrameExtent;
    LDQ_zp(cpu.A, A_B_Buttons);
    ASL_acc();
    if (BCS()) goto FourFrameExtent;

GetCurrentAnimOffset:
    LDQ_abs(cpu.A, PlayerAnimCtrl);
    
    goto GetOffsetFromAnimCtrl;

FourFrameExtent:
    LDQ_imm(cpu.A, 0x03);
    
    goto AnimationControl;

ThreeFrameExtent:
    LDQ_imm(cpu.A, 0x02);

AnimationControl:
    STQ_zp(cpu.A, 0x00);
    CALL(GetCurrentAnimOffset);
    PHA();
    LDQ_abs(cpu.A, PlayerAnimTimer);
    if (BNE()) goto ExAnimC;
    LDQ_abs(cpu.A, PlayerAnimTimerSet);
    STQ_abs(cpu.A, PlayerAnimTimer);
    LDQ_abs(cpu.A, PlayerAnimCtrl);
    CLC();
    ADC_imm(0x01);
    CMPQ_zp(cpu.A, 0x00);
    if (BCC()) goto SetAnimC;
    LDQ_imm(cpu.A, 0x00);
SetAnimC:
    STQ_abs(cpu.A, PlayerAnimCtrl);
ExAnimC:
    PLA();
    RET();

GetGfxOffsetAdder:
    LDQ_abs(cpu.A, PlayerSize);
    if (BEQ()) goto SzOfs;
    CopyYA();
    CLC();
    ADC_imm(0x08);
    CopyAY();
SzOfs:
    RET();

HandleChangeSize:
    LDQ_abs(cpu.Y, PlayerAnimCtrl);
    LDQ_zp(cpu.A, FrameCounter);
    AND_imm(0b00000011);
    if (BNE()) goto GorSLog;
    INY();
    CMPQ_imm(cpu.Y, 0x0a);
    if (BCC()) goto CSzNext;
    LDQ_imm(cpu.Y, 0x00);
    STQ_abs(cpu.Y, PlayerChangeSizeFlag);
CSzNext:
    STQ_abs(cpu.Y, PlayerAnimCtrl);
GorSLog:
    LDQ_abs(cpu.A, PlayerSize);
    if (BNE()) goto ShrinkPlayer;
    LDQ_PRGTable(cpu.A, ChangeSizeOffsetAdder, cpu.Y);
    LDQ_imm(cpu.Y, 0x0f);

GetOffsetFromAnimCtrl:
    ASL_acc();
    ASL_acc();
    ASL_acc();
    ADC_PRGTable(PlayerGfxTblOffsets, cpu.Y);
    RET();

ShrinkPlayer:
    CopyYA();
    CLC();
    ADC_imm(0x0a);
    CopyAX();
    LDQ_imm(cpu.Y, 0x09);
    LDQ_PRGTable(cpu.A, ChangeSizeOffsetAdder, cpu.X);
    if (BNE()) goto ShrPlF;
    LDQ_imm(cpu.Y, 0x01);
ShrPlF:
    LDQ_PRGTable(cpu.A, PlayerGfxTblOffsets, cpu.Y);
    RET();

ChkForPlayerAttrib:
    LDQ_abs(cpu.Y, Player_SprDataOffset);
    LDQ_zp(cpu.A, GameEngineSubroutine);
    CMPQ_imm(cpu.A, 0x0b);
    if (BEQ()) goto KilledAtt;
    LDQ_abs(cpu.A, PlayerGfxOffset);
    CMPQ_imm(cpu.A, 0x50);
    if (BEQ()) goto C_S_IGAtt;
    CMPQ_imm(cpu.A, 0xb8);
    if (BEQ()) goto C_S_IGAtt;
    CMPQ_imm(cpu.A, 0xc0);
    if (BEQ()) goto C_S_IGAtt;
    CMPQ_imm(cpu.A, 0xc8);
    if (BNE()) goto ExPlyrAt;

KilledAtt:
    LDQ_abs_xy(cpu.A, Sprite_Attributes + 16, cpu.Y);
    AND_imm(0b00111111);
    STA_abs_xy(Sprite_Attributes + 16, cpu.Y);
    LDQ_abs_xy(cpu.A, Sprite_Attributes + 20, cpu.Y);
    AND_imm(0b00111111);
    ORA_imm(0b01000000);
    STA_abs_xy(Sprite_Attributes + 20, cpu.Y);
C_S_IGAtt:
    LDQ_abs_xy(cpu.A, Sprite_Attributes + 24, cpu.Y);
    AND_imm(0b00111111);
    STA_abs_xy(Sprite_Attributes + 24, cpu.Y);
    LDQ_abs_xy(cpu.A, Sprite_Attributes + 28, cpu.Y);
    AND_imm(0b00111111);
    ORA_imm(0b01000000);
    STA_abs_xy(Sprite_Attributes + 28, cpu.Y);
ExPlyrAt:
    RET();

RelativePlayerPosition:
    LDQ_imm(cpu.X, 0x00);
    LDQ_imm(cpu.Y, 0x00);
    
    goto RelWOfs;

RelativeBubblePosition:
    LDQ_imm(cpu.Y, 0x01);
    CALL(GetProperObjOffset);
    LDQ_imm(cpu.Y, 0x03);
    
    goto RelWOfs;

RelativeFireballPosition:
    LDQ_imm(cpu.Y, 0x00);
    CALL(GetProperObjOffset);
    LDQ_imm(cpu.Y, 0x02);
RelWOfs:
    CALL(GetObjRelativePosition);
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

RelativeMiscPosition:
    LDQ_imm(cpu.Y, 0x02);
    CALL(GetProperObjOffset);
    LDQ_imm(cpu.Y, 0x06);
    
    goto RelWOfs;

RelativeEnemyPosition:
    LDQ_imm(cpu.A, 0x01);
    LDQ_imm(cpu.Y, 0x01);
    
    goto VariableObjOfsRelPos;

RelativeBlockPosition:
    LDQ_imm(cpu.A, 0x09);
    LDQ_imm(cpu.Y, 0x04);
    CALL(VariableObjOfsRelPos);
    INX();
    INX();
    LDQ_imm(cpu.A, 0x09);
    INY();

VariableObjOfsRelPos:
    STQ_zp(cpu.X, 0x00);
    CLC();
    ADC_zp(0x00);
    CopyAX();
    CALL(GetObjRelativePosition);
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

GetObjRelativePosition:
    LDQ_zp_xy(cpu.A, SprObject_Y_Position, cpu.X);
    STA_abs_xy(SprObject_Rel_YPos, cpu.Y);
    LDQ_zp_xy(cpu.A, SprObject_X_Position, cpu.X);
    SEC();
    SBC_abs(ScreenLeft_X_Pos);
    STA_abs_xy(SprObject_Rel_XPos, cpu.Y);
    RET();

GetPlayerOffscreenBits:
    LDQ_imm(cpu.X, 0x00);
    LDQ_imm(cpu.Y, 0x00);
    
    goto GetOffScreenBitsSet;

GetFireballOffscreenBits:
    LDQ_imm(cpu.Y, 0x00);
    CALL(GetProperObjOffset);
    LDQ_imm(cpu.Y, 0x02);
    
    goto GetOffScreenBitsSet;

GetBubbleOffscreenBits:
    LDQ_imm(cpu.Y, 0x01);
    CALL(GetProperObjOffset);
    LDQ_imm(cpu.Y, 0x03);
    
    goto GetOffScreenBitsSet;

GetMiscOffscreenBits:
    LDQ_imm(cpu.Y, 0x02);
    CALL(GetProperObjOffset);
    LDQ_imm(cpu.Y, 0x06);
    
    goto GetOffScreenBitsSet;

GetProperObjOffset:
    CopyXA();
    CLC();
    ADC_PRGTable(ObjOffsetData, cpu.Y);
    CopyAX();
    RET();

GetEnemyOffscreenBits:
    LDQ_imm(cpu.A, 0x01);
    LDQ_imm(cpu.Y, 0x01);
    
    goto SetOffscrBitsOffset;

GetBlockOffscreenBits:
    LDQ_imm(cpu.A, 0x09);
    LDQ_imm(cpu.Y, 0x04);

SetOffscrBitsOffset:
    STQ_zp(cpu.X, 0x00);
    CLC();
    ADC_zp(0x00);
    CopyAX();

GetOffScreenBitsSet:
    CopyYA();
    PHA();
    CALL(RunOffscrBitsSubs);
    ASL_acc();
    ASL_acc();
    ASL_acc();
    ASL_acc();
    ORA_zp(0x00);
    STQ_zp(cpu.A, 0x00);
    PLA();
    CopyAY();
    LDQ_zp(cpu.A, 0x00);
    STA_abs_xy(SprObject_OffscrBits, cpu.Y);
    LDQ_zp(cpu.X, ObjectOffset);
    RET();

RunOffscrBitsSubs:
    CALL(GetXOffscreenBits);
    LSR_acc();
    LSR_acc();
    LSR_acc();
    LSR_acc();
    STQ_zp(cpu.A, 0x00);
    
    goto GetYOffscreenBits;

GetXOffscreenBits:
    STQ_zp(cpu.X, 0x04);
    LDQ_imm(cpu.Y, 0x01);
XOfsLoop:
    LDQ_abs_xy(cpu.A, ScreenEdge_X_Pos, cpu.Y);
    SEC();
    SBC_zp_x(SprObject_X_Position);
    STQ_zp(cpu.A, 0x07);
    LDQ_abs_xy(cpu.A, ScreenEdge_PageLoc, cpu.Y);
    SBC_zp_x(SprObject_PageLoc);
    LDQ_PRGTable(cpu.X, DefaultXOnscreenOfs, cpu.Y);
    CMPQ_imm(cpu.A, 0x00);
    if (BMI()) goto XLdBData;
    LDQ_PRGTable(cpu.X, DefaultXOnscreenOfs, cpu.Y + 1);
    CMPQ_imm(cpu.A, 0x01);
    if (BPL()) goto XLdBData;
    LDQ_imm(cpu.A, 0x38);
    STQ_zp(cpu.A, 0x06);
    LDQ_imm(cpu.A, 0x08);
    CALL(DividePDiff);
XLdBData:
    LDQ_PRGTable(cpu.A, XOffscreenBitsData, cpu.X);
    LDQ_zp(cpu.X, 0x04);
    CMPQ_imm(cpu.A, 0x00);
    if (BNE()) goto ExXOfsBS;
    DEY();
    if (BPL()) goto XOfsLoop;

ExXOfsBS:
    RET();

GetYOffscreenBits:
    STQ_zp(cpu.X, 0x04);
    LDQ_imm(cpu.Y, 0x01);
YOfsLoop:
    LDQ_PRGTable(cpu.A, HighPosUnitData, cpu.Y);
    SEC();
    SBC_zp_x(SprObject_Y_Position);
    STQ_zp(cpu.A, 0x07);
    LDQ_imm(cpu.A, 0x01);
    SBC_zp_x(SprObject_Y_HighPos);
    LDQ_PRGTable(cpu.X, DefaultYOnscreenOfs, cpu.Y);
    CMPQ_imm(cpu.A, 0x00);
    if (BMI()) goto YLdBData;
    LDQ_PRGTable(cpu.X, DefaultYOnscreenOfs, cpu.Y + 1);
    CMPQ_imm(cpu.A, 0x01);
    if (BPL()) goto YLdBData;
    LDQ_imm(cpu.A, 0x20);
    STQ_zp(cpu.A, 0x06);
    LDQ_imm(cpu.A, 0x04);
    CALL(DividePDiff);
YLdBData:
    LDQ_PRGTable(cpu.A, YOffscreenBitsData, cpu.X);
    LDQ_zp(cpu.X, 0x04);
    CMPQ_imm(cpu.A, 0x00);
    if (BNE()) goto ExYOfsBS;
    DEY();
    if (BPL()) goto YOfsLoop;

ExYOfsBS:
    RET();

DividePDiff:
    STQ_zp(cpu.A, 0x05);
    LDQ_zp(cpu.A, 0x07);
    CMPQ_zp(cpu.A, 0x06);
    if (BCS()) goto ExDivPD;
    LSR_acc();
    LSR_acc();
    LSR_acc();
    AND_imm(0x07);
    CMPQ_imm(cpu.Y, 0x01);
    if (BCS()) goto SetOscrO;
    ADC_zp(0x05);
SetOscrO:
    CopyAX();
ExDivPD:
    RET();

DrawSpriteObject:
    LDQ_zp(cpu.A, 0x03);
    LSR_acc();
    LSR_acc();
    LDQ_zp(cpu.A, 0x00);
    if (BCC()) goto NoHFlip;
    STA_abs_xy(Sprite_Tilenumber + 4, cpu.Y);
    LDQ_zp(cpu.A, 0x01);
    STA_abs_xy(Sprite_Tilenumber, cpu.Y);
    LDQ_imm(cpu.A, 0x40);
    if (BNE()) goto SetHFAt;

NoHFlip:
    STA_abs_xy(Sprite_Tilenumber, cpu.Y);
    LDQ_zp(cpu.A, 0x01);
    STA_abs_xy(Sprite_Tilenumber + 4, cpu.Y);
    LDQ_imm(cpu.A, 0x00);
SetHFAt:
    ORA_zp(0x04);
    STA_abs_xy(Sprite_Attributes, cpu.Y);
    STA_abs_xy(Sprite_Attributes + 4, cpu.Y);
    LDQ_zp(cpu.A, 0x02);
    STA_abs_xy(Sprite_Y_Position, cpu.Y);
    STA_abs_xy(Sprite_Y_Position + 4, cpu.Y);
    LDQ_zp(cpu.A, 0x05);
    STA_abs_xy(Sprite_X_Position, cpu.Y);
    CLC();
    ADC_imm(0x08);
    STA_abs_xy(Sprite_X_Position + 4, cpu.Y);
    LDQ_zp(cpu.A, 0x02);
    CLC();
    ADC_imm(0x08);
    STQ_zp(cpu.A, 0x02);
    CopyYA();
    CLC();
    ADC_imm(0x08);
    CopyAY();
    INX();
    INX();
    RET();

}