#pragma once
#include <cstdint>

// Credits to the authors of the disassembly: https://gist.github.com/1wErt3r/4048722

inline constexpr uint16_t ObjectOffset = 0x08;
inline constexpr uint16_t FrameCounter = 0x09;

inline constexpr uint16_t SavedJoypadBits = 0x06fc;
inline constexpr uint16_t SavedJoypad1Bits = 0x06fc;
inline constexpr uint16_t SavedJoypad2Bits = 0x06fd;
inline constexpr uint16_t JoypadBitMask = 0x074a;
inline constexpr uint16_t JoypadOverride = 0x0758;

inline constexpr uint16_t A_B_Buttons = 0x0a;
inline constexpr uint16_t PreviousA_B_Buttons = 0x0d;
inline constexpr uint16_t Up_Down_Buttons = 0x0b;
inline constexpr uint16_t Left_Right_Buttons = 0x0c;

inline constexpr uint16_t GameEngineSubroutine = 0x0e;

inline constexpr uint16_t Mirror_PPU_CTRL_REG1 = 0x0778;
inline constexpr uint16_t Mirror_PPU_CTRL_REG2 = 0x0779;

inline constexpr uint16_t OperMode = 0x0770;
inline constexpr uint16_t OperMode_Task = 0x0772;
inline constexpr uint16_t ScreenRoutineTask = 0x073c;

inline constexpr uint16_t GamePauseStatus = 0x0776;
inline constexpr uint16_t GamePauseTimer = 0x0777;

inline constexpr uint16_t DemoAction = 0x0717;
inline constexpr uint16_t DemoActionTimer = 0x0718;

inline constexpr uint16_t TimerControl = 0x0747;
inline constexpr uint16_t IntervalTimerControl = 0x077f;

inline constexpr uint16_t Timers = 0x0780;
inline constexpr uint16_t SelectTimer = 0x0780;
inline constexpr uint16_t PlayerAnimTimer = 0x0781;
inline constexpr uint16_t JumpSwimTimer = 0x0782;
inline constexpr uint16_t RunningTimer = 0x0783;
inline constexpr uint16_t BlockBounceTimer = 0x0784;
inline constexpr uint16_t SideCollisionTimer = 0x0785;
inline constexpr uint16_t JumpspringTimer = 0x0786;
inline constexpr uint16_t GameTimerCtrlTimer = 0x0787;
inline constexpr uint16_t ClimbSideTimer = 0x0789;
inline constexpr uint16_t EnemyFrameTimer = 0x078a;
inline constexpr uint16_t FrenzyEnemyTimer = 0x078f;
inline constexpr uint16_t BowserFireBreathTimer = 0x0790;
inline constexpr uint16_t StompTimer = 0x0791;
inline constexpr uint16_t AirBubbleTimer = 0x0792;
inline constexpr uint16_t ScrollIntervalTimer = 0x0795;
inline constexpr uint16_t EnemyIntervalTimer = 0x0796;
inline constexpr uint16_t BrickCoinTimer = 0x079d;
inline constexpr uint16_t InjuryTimer = 0x079e;
inline constexpr uint16_t StarInvincibleTimer = 0x079f;
inline constexpr uint16_t ScreenTimer = 0x07a0;
inline constexpr uint16_t WorldEndTimer = 0x07a1;
inline constexpr uint16_t DemoTimer = 0x07a2;

inline constexpr uint16_t Sprite_Data = 0x0200;
inline constexpr uint16_t Sprite_Y_Position = 0x0200;
inline constexpr uint16_t Sprite_Tilenumber = 0x0201;
inline constexpr uint16_t Sprite_Attributes = 0x0202;
inline constexpr uint16_t Sprite_X_Position = 0x0203;

inline constexpr uint16_t ScreenEdge_PageLoc = 0x071a;
inline constexpr uint16_t ScreenEdge_X_Pos = 0x071c;
inline constexpr uint16_t ScreenLeft_PageLoc = 0x071a;
inline constexpr uint16_t ScreenRight_PageLoc = 0x071b;
inline constexpr uint16_t ScreenLeft_X_Pos = 0x071c;
inline constexpr uint16_t ScreenRight_X_Pos = 0x071d;

inline constexpr uint16_t PlayerFacingDir = 0x33;
inline constexpr uint16_t DestinationPageLoc = 0x34;
inline constexpr uint16_t VictoryWalkControl = 0x35;
inline constexpr uint16_t ScrollFractional = 0x0768;
inline constexpr uint16_t PrimaryMsgCounter = 0x0719;
inline constexpr uint16_t SecondaryMsgCounter = 0x0749;

inline constexpr uint16_t HorizontalScroll = 0x073f;
inline constexpr uint16_t VerticalScroll = 0x0740;
inline constexpr uint16_t ScrollLock = 0x0723;
inline constexpr uint16_t ScrollThirtyTwo = 0x073d;
inline constexpr uint16_t Player_X_Scroll = 0x06ff;
inline constexpr uint16_t Player_Pos_ForScroll = 0x0755;
inline constexpr uint16_t ScrollAmount = 0x0775;

inline constexpr uint16_t AreaData = 0xe7;
inline constexpr uint16_t AreaDataLow = 0xe7;
inline constexpr uint16_t AreaDataHigh = 0xe8;
inline constexpr uint16_t EnemyData = 0xe9;
inline constexpr uint16_t EnemyDataLow = 0xe9;
inline constexpr uint16_t EnemyDataHigh = 0xea;

inline constexpr uint16_t AreaParserTaskNum = 0x071f;
inline constexpr uint16_t ColumnSets = 0x071e;
inline constexpr uint16_t CurrentPageLoc = 0x0725;
inline constexpr uint16_t CurrentColumnPos = 0x0726;
inline constexpr uint16_t BackloadingFlag = 0x0728;
inline constexpr uint16_t BehindAreaParserFlag = 0x0729;
inline constexpr uint16_t AreaObjectPageLoc = 0x072a;
inline constexpr uint16_t AreaObjectPageSel = 0x072b;
inline constexpr uint16_t AreaDataOffset = 0x072c;
inline constexpr uint16_t AreaObjOffsetBuffer = 0x072d;
inline constexpr uint16_t AreaObjectLength = 0x0730;
inline constexpr uint16_t StaircaseControl = 0x0734;
inline constexpr uint16_t AreaObjectHeight = 0x0735;
inline constexpr uint16_t MushroomLedgeHalfLen = 0x0736;
inline constexpr uint16_t EnemyDataOffset = 0x0739;
inline constexpr uint16_t EnemyObjectPageLoc = 0x073a;
inline constexpr uint16_t EnemyObjectPageSel = 0x073b;
inline constexpr uint16_t MetatileBuffer = 0x06a1;
inline constexpr uint16_t BlockBufferColumnPos = 0x06a0;
inline constexpr uint16_t CurrentNTAddr_Low = 0x0721;
inline constexpr uint16_t CurrentNTAddr_High = 0x0720;
inline constexpr uint16_t AttributeBuffer = 0x03f9;

inline constexpr uint16_t LoopCommand = 0x0745;

inline constexpr uint16_t DisplayDigits = 0x07d7;
inline constexpr uint16_t TopScoreDisplay = 0x07d7;
inline constexpr uint16_t ScoreAndCoinDisplay = 0x07dd;
inline constexpr uint16_t PlayerScoreDisplay = 0x07dd;
inline constexpr uint16_t GameTimerDisplay = 0x07f8;
inline constexpr uint16_t DigitModifier = 0x0134;

inline constexpr uint16_t VerticalFlipFlag = 0x0109;
inline constexpr uint16_t FloateyNum_Control = 0x0110;
inline constexpr uint16_t ShellChainCounter = 0x0125;
inline constexpr uint16_t FloateyNum_Timer = 0x012c;
inline constexpr uint16_t FloateyNum_X_Pos = 0x0117;
inline constexpr uint16_t FloateyNum_Y_Pos = 0x011e;
inline constexpr uint16_t FlagpoleFNum_Y_Pos = 0x010d;
inline constexpr uint16_t FlagpoleFNum_YMFDummy = 0x010e;
inline constexpr uint16_t FlagpoleScore = 0x010f;
inline constexpr uint16_t FlagpoleCollisionYPos = 0x070f;
inline constexpr uint16_t StompChainCounter = 0x0484;

inline constexpr uint16_t VRAM_Buffer1_Offset = 0x0300;
inline constexpr uint16_t VRAM_Buffer1 = 0x0301;
inline constexpr uint16_t VRAM_Buffer2_Offset = 0x0340;
inline constexpr uint16_t VRAM_Buffer2 = 0x0341;
inline constexpr uint16_t VRAM_Buffer_AddrCtrl = 0x0773;
inline constexpr uint16_t Sprite0HitDetectFlag = 0x0722;
inline constexpr uint16_t DisableScreenFlag = 0x0774;
inline constexpr uint16_t DisableIntermediate = 0x0769;
inline constexpr uint16_t ColorRotateOffset = 0x06d4;

inline constexpr uint16_t TerrainControl = 0x0727;
inline constexpr uint16_t AreaStyle = 0x0733;
inline constexpr uint16_t ForegroundScenery = 0x0741;
inline constexpr uint16_t BackgroundScenery = 0x0742;
inline constexpr uint16_t CloudTypeOverride = 0x0743;
inline constexpr uint16_t BackgroundColorCtrl = 0x0744;
inline constexpr uint16_t AreaType = 0x074e;
inline constexpr uint16_t AreaAddrsLOffset = 0x074f;
inline constexpr uint16_t AreaPointer = 0x0750;

inline constexpr uint16_t PlayerEntranceCtrl = 0x0710;
inline constexpr uint16_t GameTimerSetting = 0x0715;
inline constexpr uint16_t AltEntranceControl = 0x0752;
inline constexpr uint16_t EntrancePage = 0x0751;
inline constexpr uint16_t NumberOfPlayers = 0x077a;
inline constexpr uint16_t WarpZoneControl = 0x06d6;
inline constexpr uint16_t ChangeAreaTimer = 0x06de;

inline constexpr uint16_t MultiLoopCorrectCntr = 0x06d9;
inline constexpr uint16_t MultiLoopPassCntr = 0x06da;

inline constexpr uint16_t FetchNewGameTimerFlag = 0x0757;
inline constexpr uint16_t GameTimerExpiredFlag = 0x0759;

inline constexpr uint16_t PrimaryHardMode = 0x076a;
inline constexpr uint16_t SecondaryHardMode = 0x06cc;
inline constexpr uint16_t WorldSelectNumber = 0x076b;
inline constexpr uint16_t WorldSelectEnableFlag = 0x07fc;
inline constexpr uint16_t ContinueWorld = 0x07fd;

inline constexpr uint16_t CurrentPlayer = 0x0753;
inline constexpr uint16_t PlayerSize = 0x0754;
inline constexpr uint16_t PlayerStatus = 0x0756;

inline constexpr uint16_t OnscreenPlayerInfo = 0x075a;
inline constexpr uint16_t NumberofLives = 0x075a;
inline constexpr uint16_t HalfwayPage = 0x075b;
inline constexpr uint16_t LevelNumber = 0x075c;
inline constexpr uint16_t Hidden1UpFlag = 0x075d;
inline constexpr uint16_t CoinTally = 0x075e;
inline constexpr uint16_t WorldNumber = 0x075f;
inline constexpr uint16_t AreaNumber = 0x0760;

inline constexpr uint16_t CoinTallyFor1Ups = 0x0748;

inline constexpr uint16_t OffscreenPlayerInfo = 0x0761;
inline constexpr uint16_t OffScr_NumberofLives = 0x0761;
inline constexpr uint16_t OffScr_Hidden1UpFlag = 0x0764;
inline constexpr uint16_t OffScr_WorldNumber = 0x0766;
inline constexpr uint16_t OffScr_AreaNumber = 0x0767;

inline constexpr uint16_t BalPlatformAlignment = 0x03a0;
inline constexpr uint16_t Platform_X_Scroll = 0x03a1;
inline constexpr uint16_t PlatformCollisionFlag = 0x03a2;
inline constexpr uint16_t YPlatformTopYPos = 0x0401;
inline constexpr uint16_t YPlatformCenterYPos = 0x58;

inline constexpr uint16_t BrickCoinTimerFlag = 0x06bc;
inline constexpr uint16_t StarFlagTaskControl = 0x0746;

inline constexpr uint16_t PseudoRandomBitReg = 0x07a7;
inline constexpr uint16_t WarmBootValidation = 0x07ff;

inline constexpr uint16_t SprShuffleAmtOffset = 0x06e0;
inline constexpr uint16_t SprShuffleAmt = 0x06e1;
inline constexpr uint16_t SprDataOffset = 0x06e4;
inline constexpr uint16_t Player_SprDataOffset = 0x06e4;
inline constexpr uint16_t Enemy_SprDataOffset = 0x06e5;
inline constexpr uint16_t Block_SprDataOffset = 0x06ec;
inline constexpr uint16_t Alt_SprDataOffset = 0x06ec;
inline constexpr uint16_t Bubble_SprDataOffset = 0x06ee;
inline constexpr uint16_t FBall_SprDataOffset = 0x06f1;
inline constexpr uint16_t Misc_SprDataOffset = 0x06f3;
inline constexpr uint16_t SprDataOffset_Ctrl = 0x03ee;

inline constexpr uint16_t Player_State = 0x1d;
inline constexpr uint16_t Enemy_State = 0x1e;
inline constexpr uint16_t Fireball_State = 0x24;
inline constexpr uint16_t Block_State = 0x26;
inline constexpr uint16_t Misc_State = 0x2a;

inline constexpr uint16_t Player_MovingDir = 0x45;
inline constexpr uint16_t Enemy_MovingDir = 0x46;

inline constexpr uint16_t SprObject_X_Speed = 0x57;
inline constexpr uint16_t Player_X_Speed = 0x57;
inline constexpr uint16_t Enemy_X_Speed = 0x58;
inline constexpr uint16_t Fireball_X_Speed = 0x5e;
inline constexpr uint16_t Block_X_Speed = 0x60;
inline constexpr uint16_t Misc_X_Speed = 0x64;

inline constexpr uint16_t Jumpspring_FixedYPos = 0x58;
inline constexpr uint16_t JumpspringAnimCtrl = 0x070e;
inline constexpr uint16_t JumpspringForce = 0x06db;

inline constexpr uint16_t SprObject_PageLoc = 0x6d;
inline constexpr uint16_t Player_PageLoc = 0x6d;
inline constexpr uint16_t Enemy_PageLoc = 0x6e;
inline constexpr uint16_t Fireball_PageLoc = 0x74;
inline constexpr uint16_t Block_PageLoc = 0x76;
inline constexpr uint16_t Misc_PageLoc = 0x7a;
inline constexpr uint16_t Bubble_PageLoc = 0x83;

inline constexpr uint16_t SprObject_X_Position = 0x86;
inline constexpr uint16_t Player_X_Position = 0x86;
inline constexpr uint16_t Enemy_X_Position = 0x87;
inline constexpr uint16_t Fireball_X_Position = 0x8d;
inline constexpr uint16_t Block_X_Position = 0x8f;
inline constexpr uint16_t Misc_X_Position = 0x93;
inline constexpr uint16_t Bubble_X_Position = 0x9c;

inline constexpr uint16_t SprObject_Y_Speed = 0x9f;
inline constexpr uint16_t Player_Y_Speed = 0x9f;
inline constexpr uint16_t Enemy_Y_Speed = 0xa0;
inline constexpr uint16_t Fireball_Y_Speed = 0xa6;
inline constexpr uint16_t Block_Y_Speed = 0xa8;
inline constexpr uint16_t Misc_Y_Speed = 0xac;

inline constexpr uint16_t SprObject_Y_HighPos = 0xb5;
inline constexpr uint16_t Player_Y_HighPos = 0xb5;
inline constexpr uint16_t Enemy_Y_HighPos = 0xb6;
inline constexpr uint16_t Fireball_Y_HighPos = 0xbc;
inline constexpr uint16_t Block_Y_HighPos = 0xbe;
inline constexpr uint16_t Misc_Y_HighPos = 0xc2;
inline constexpr uint16_t Bubble_Y_HighPos = 0xcb;

inline constexpr uint16_t SprObject_Y_Position = 0xce;
inline constexpr uint16_t Player_Y_Position = 0xce;
inline constexpr uint16_t Enemy_Y_Position = 0xcf;
inline constexpr uint16_t Fireball_Y_Position = 0xd5;
inline constexpr uint16_t Block_Y_Position = 0xd7;
inline constexpr uint16_t Misc_Y_Position = 0xdb;
inline constexpr uint16_t Bubble_Y_Position = 0xe4;

inline constexpr uint16_t SprObject_Rel_XPos = 0x03ad;
inline constexpr uint16_t Player_Rel_XPos = 0x03ad;
inline constexpr uint16_t Enemy_Rel_XPos = 0x03ae;
inline constexpr uint16_t Fireball_Rel_XPos = 0x03af;
inline constexpr uint16_t Bubble_Rel_XPos = 0x03b0;
inline constexpr uint16_t Block_Rel_XPos = 0x03b1;
inline constexpr uint16_t Misc_Rel_XPos = 0x03b3;

inline constexpr uint16_t SprObject_Rel_YPos = 0x03b8;
inline constexpr uint16_t Player_Rel_YPos = 0x03b8;
inline constexpr uint16_t Enemy_Rel_YPos = 0x03b9;
inline constexpr uint16_t Fireball_Rel_YPos = 0x03ba;
inline constexpr uint16_t Bubble_Rel_YPos = 0x03bb;
inline constexpr uint16_t Block_Rel_YPos = 0x03bc;
inline constexpr uint16_t Misc_Rel_YPos = 0x03be;

inline constexpr uint16_t Player_SprAttrib = 0x03c4;
inline constexpr uint16_t Enemy_SprAttrib = 0x03c5;

inline constexpr uint16_t SprObject_X_MoveForce = 0x0400;
inline constexpr uint16_t Enemy_X_MoveForce = 0x0401;

inline constexpr uint16_t SprObject_YMF_Dummy = 0x0416;
inline constexpr uint16_t Player_YMF_Dummy = 0x0416;
inline constexpr uint16_t Enemy_YMF_Dummy = 0x0417;
inline constexpr uint16_t Bubble_YMF_Dummy = 0x042c;

inline constexpr uint16_t SprObject_Y_MoveForce = 0x0433;
inline constexpr uint16_t Player_Y_MoveForce = 0x0433;
inline constexpr uint16_t Enemy_Y_MoveForce = 0x0434;
inline constexpr uint16_t Block_Y_MoveForce = 0x043c;

inline constexpr uint16_t DisableCollisionDet = 0x0716;
inline constexpr uint16_t Player_CollisionBits = 0x0490;
inline constexpr uint16_t Enemy_CollisionBits = 0x0491;

inline constexpr uint16_t SprObj_BoundBoxCtrl = 0x0499;
inline constexpr uint16_t Player_BoundBoxCtrl = 0x0499;
inline constexpr uint16_t Enemy_BoundBoxCtrl = 0x049a;
inline constexpr uint16_t Fireball_BoundBoxCtrl = 0x04a0;
inline constexpr uint16_t Misc_BoundBoxCtrl = 0x04a2;

inline constexpr uint16_t EnemyFrenzyBuffer = 0x06cb;
inline constexpr uint16_t EnemyFrenzyQueue = 0x06cd;
inline constexpr uint16_t Enemy_Flag = 0x0f;
inline constexpr uint16_t Enemy_ID = 0x16;

inline constexpr uint16_t PlayerGfxOffset = 0x06d5;
inline constexpr uint16_t Player_XSpeedAbsolute = 0x0700;
inline constexpr uint16_t FrictionAdderHigh = 0x0701;
inline constexpr uint16_t FrictionAdderLow = 0x0702;
inline constexpr uint16_t RunningSpeed = 0x0703;
inline constexpr uint16_t SwimmingFlag = 0x0704;
inline constexpr uint16_t Player_X_MoveForce = 0x0705;
inline constexpr uint16_t DiffToHaltJump = 0x0706;
inline constexpr uint16_t JumpOrigin_Y_HighPos = 0x0707;
inline constexpr uint16_t JumpOrigin_Y_Position = 0x0708;
inline constexpr uint16_t VerticalForce = 0x0709;
inline constexpr uint16_t VerticalForceDown = 0x070a;
inline constexpr uint16_t PlayerChangeSizeFlag = 0x070b;
inline constexpr uint16_t PlayerAnimTimerSet = 0x070c;
inline constexpr uint16_t PlayerAnimCtrl = 0x070d;
inline constexpr uint16_t DeathMusicLoaded = 0x0712;
inline constexpr uint16_t FlagpoleSoundQueue = 0x0713;
inline constexpr uint16_t CrouchingFlag = 0x0714;
inline constexpr uint16_t MaximumLeftSpeed = 0x0450;
inline constexpr uint16_t MaximumRightSpeed = 0x0456;

inline constexpr uint16_t SprObject_OffscrBits = 0x03d0;
inline constexpr uint16_t Player_OffscreenBits = 0x03d0;
inline constexpr uint16_t Enemy_OffscreenBits = 0x03d1;
inline constexpr uint16_t FBall_OffscreenBits = 0x03d2;
inline constexpr uint16_t Bubble_OffscreenBits = 0x03d3;
inline constexpr uint16_t Block_OffscreenBits = 0x03d4;
inline constexpr uint16_t Misc_OffscreenBits = 0x03d6;
inline constexpr uint16_t EnemyOffscrBitsMasked = 0x03d8;

inline constexpr uint16_t Cannon_Offset = 0x046a;
inline constexpr uint16_t Cannon_PageLoc = 0x046b;
inline constexpr uint16_t Cannon_X_Position = 0x0471;
inline constexpr uint16_t Cannon_Y_Position = 0x0477;
inline constexpr uint16_t Cannon_Timer = 0x047d;

inline constexpr uint16_t Whirlpool_Offset = 0x046a;
inline constexpr uint16_t Whirlpool_PageLoc = 0x046b;
inline constexpr uint16_t Whirlpool_LeftExtent = 0x0471;
inline constexpr uint16_t Whirlpool_Length = 0x0477;
inline constexpr uint16_t Whirlpool_Flag = 0x047d;

inline constexpr uint16_t VineFlagOffset = 0x0398;
inline constexpr uint16_t VineHeight = 0x0399;
inline constexpr uint16_t VineObjOffset = 0x039a;
inline constexpr uint16_t VineStart_Y_Position = 0x039d;

inline constexpr uint16_t Block_Orig_YPos = 0x03e4;
inline constexpr uint16_t Block_BBuf_Low = 0x03e6;
inline constexpr uint16_t Block_Metatile = 0x03e8;
inline constexpr uint16_t Block_PageLoc2 = 0x03ea;
inline constexpr uint16_t Block_RepFlag = 0x03ec;
inline constexpr uint16_t Block_ResidualCounter = 0x03f0;
inline constexpr uint16_t Block_Orig_XPos = 0x03f1;

inline constexpr uint16_t BoundingBox_UL_XPos = 0x04ac;
inline constexpr uint16_t BoundingBox_UL_YPos = 0x04ad;
inline constexpr uint16_t BoundingBox_DR_XPos = 0x04ae;
inline constexpr uint16_t BoundingBox_DR_YPos = 0x04af;
inline constexpr uint16_t BoundingBox_UL_Corner = 0x04ac;
inline constexpr uint16_t BoundingBox_LR_Corner = 0x04ae;
inline constexpr uint16_t EnemyBoundingBoxCoord = 0x04b0;

inline constexpr uint16_t PowerUpType = 0x39;

inline constexpr uint16_t FireballBouncingFlag = 0x3a;
inline constexpr uint16_t FireballCounter = 0x06ce;
inline constexpr uint16_t FireballThrowingTimer = 0x0711;

inline constexpr uint16_t HammerEnemyOffset = 0x06ae;
inline constexpr uint16_t JumpCoinMiscOffset = 0x06b7;

inline constexpr uint16_t Block_Buffer_1 = 0x0500;

inline constexpr uint16_t HammerThrowingTimer = 0x03a2;
inline constexpr uint16_t HammerBroJumpTimer = 0x3c;
inline constexpr uint16_t Misc_Collision_Flag = 0x06be;

inline constexpr uint16_t RedPTroopaOrigXPos = 0x0401;
inline constexpr uint16_t RedPTroopaCenterYPos = 0x58;

inline constexpr uint16_t XMovePrimaryCounter = 0xa0;
inline constexpr uint16_t XMoveSecondaryCounter = 0x58;

inline constexpr uint16_t CheepCheepMoveMFlag = 0x58;
inline constexpr uint16_t CheepCheepOrigYPos = 0x0434;
inline constexpr uint16_t BitMFilter = 0x06dd;

inline constexpr uint16_t LakituReappearTimer = 0x06d1;
inline constexpr uint16_t LakituMoveSpeed = 0x58;
inline constexpr uint16_t LakituMoveDirection = 0xa0;

inline constexpr uint16_t FirebarSpinState_Low = 0x58;
inline constexpr uint16_t FirebarSpinState_High = 0xa0;
inline constexpr uint16_t FirebarSpinSpeed = 0x0388;
inline constexpr uint16_t FirebarSpinDirection = 0x34;

inline constexpr uint16_t DuplicateObj_Offset = 0x06cf;
inline constexpr uint16_t NumberofGroupEnemies = 0x06d3;

inline constexpr uint16_t BlooperMoveCounter = 0xa0;
inline constexpr uint16_t BlooperMoveSpeed = 0x58;

inline constexpr uint16_t BowserBodyControls = 0x0363;
inline constexpr uint16_t BowserFeetCounter = 0x0364;
inline constexpr uint16_t BowserMovementSpeed = 0x0365;
inline constexpr uint16_t BowserOrigXPos = 0x0366;
inline constexpr uint16_t BowserFlameTimerCtrl = 0x0367;
inline constexpr uint16_t BowserFront_Offset = 0x0368;
inline constexpr uint16_t BridgeCollapseOffset = 0x0369;
inline constexpr uint16_t BowserGfxFlag = 0x036a;
inline constexpr uint16_t BowserHitPoints = 0x0483;
inline constexpr uint16_t MaxRangeFromOrigin = 0x06dc;

inline constexpr uint16_t BowserFlamePRandomOfs = 0x0417;

inline constexpr uint16_t PiranhaPlantUpYPos = 0x0417;
inline constexpr uint16_t PiranhaPlantDownYPos = 0x0434;
inline constexpr uint16_t PiranhaPlant_Y_Speed = 0x58;
inline constexpr uint16_t PiranhaPlant_MoveFlag = 0xa0;

inline constexpr uint16_t FireworksCounter = 0x06d7;
inline constexpr uint16_t ExplosionGfxCounter = 0x58;
inline constexpr uint16_t ExplosionTimerCounter = 0xa0;

inline constexpr uint16_t PauseSoundQueue = 0xfa;
inline constexpr uint16_t Square1SoundQueue = 0xff;
inline constexpr uint16_t Square2SoundQueue = 0xfe;
inline constexpr uint16_t NoiseSoundQueue = 0xfd;
inline constexpr uint16_t AreaMusicQueue = 0xfb;
inline constexpr uint16_t EventMusicQueue = 0xfc;

inline constexpr uint16_t EventMusicBuffer = 0x07b1;

inline constexpr uint16_t MusicData = 0xf5;

inline constexpr uint16_t Sfx_SmallJump = 0b10000000;
inline constexpr uint16_t Sfx_Fireball = 0b00100000;
inline constexpr uint16_t Sfx_PipeDown_Injury = 0b00010000;
inline constexpr uint16_t Sfx_EnemySmack = 0b00001000;
inline constexpr uint16_t Sfx_EnemyStomp = 0b00000100;
inline constexpr uint16_t Sfx_Bump = 0b00000010;
inline constexpr uint16_t Sfx_BigJump = 0b00000001;

inline constexpr uint16_t Sfx_BowserFall = 0b10000000;
inline constexpr uint16_t Sfx_ExtraLife = 0b01000000;
inline constexpr uint16_t Sfx_PowerUpGrab = 0b00100000;
inline constexpr uint16_t Sfx_TimerTick = 0b00010000;
inline constexpr uint16_t Sfx_Blast = 0b00001000;
inline constexpr uint16_t Sfx_GrowVine = 0b00000100;
inline constexpr uint16_t Sfx_GrowPowerUp = 0b00000010;
inline constexpr uint16_t Sfx_CoinGrab = 0b00000001;

inline constexpr uint16_t Sfx_BowserFlame = 0b00000010;
inline constexpr uint16_t Sfx_BrickShatter = 0b00000001;

inline constexpr uint16_t Silence = 0b10000000;

inline constexpr uint16_t StarPowerMusic = 0b01000000;

inline constexpr uint16_t TimeRunningOutMusic = 0b01000000;
inline constexpr uint16_t EndOfLevelMusic = 0b00100000;
inline constexpr uint16_t EndOfCastleMusic = 0b00001000;
inline constexpr uint16_t VictoryMusic = 0b00000100;
inline constexpr uint16_t GameOverMusic = 0b00000010;
inline constexpr uint16_t DeathMusic = 0b00000001;

inline constexpr uint8_t BuzzyBeetle = 0x02;
inline constexpr uint8_t RedKoopa = 0x03;
inline constexpr uint8_t HammerBro = 0x05;
inline constexpr uint8_t Goomba = 0x06;
inline constexpr uint8_t Bloober = 0x07;
inline constexpr uint8_t BulletBill_FrenzyVar = 0x08;
inline constexpr uint8_t GreyCheepCheep = 0x0a;
inline constexpr uint8_t RedCheepCheep = 0x0b;
inline constexpr uint8_t Podoboo = 0x0c;
inline constexpr uint8_t PiranhaPlant = 0x0d;
inline constexpr uint8_t GreenParatroopaJump = 0x0e;
inline constexpr uint8_t Lakitu = 0x11;
inline constexpr uint8_t Spiny = 0x12;
inline constexpr uint8_t FlyingCheepCheep = 0x14;
inline constexpr uint8_t BowserFlame = 0x15;
inline constexpr uint8_t Fireworks = 0x16;
inline constexpr uint8_t Bowser = 0x2d;
inline constexpr uint8_t PowerUpObject = 0x2e;
inline constexpr uint8_t VineObject = 0x2f;
inline constexpr uint8_t FlagpoleFlagObject = 0x30;
inline constexpr uint8_t StarFlagObject = 0x31;
inline constexpr uint8_t JumpspringObject = 0x32;
inline constexpr uint8_t BulletBill_CannonVar = 0x33;
inline constexpr uint8_t RetainerObject = 0x35;
inline constexpr uint8_t TallEnemy = 0x09;

inline constexpr uint8_t World2 = 1;
inline constexpr uint8_t World5 = 4;
inline constexpr uint8_t World6 = 5;
inline constexpr uint8_t World7 = 6;
inline constexpr uint8_t World8 = 7;
inline constexpr uint8_t Level3 = 2;

inline constexpr uint8_t WarmBootOffset = 0x07d6 & 0xFF;
inline constexpr uint8_t ColdBootOffset = 0x07fe & 0xFF;
inline constexpr uint16_t TitleScreenDataOffset = 0x1ec0; // combined low and high
inline constexpr uint16_t SoundMemory = 0x07b0;

inline constexpr uint8_t A_Button = 0b10000000;
inline constexpr uint8_t B_Button = 0b01000000;
inline constexpr uint8_t Select_Button = 0b00100000;
inline constexpr uint8_t Start_Button = 0b00010000;

inline constexpr uint8_t TitleScreenModeValue = 0;
inline constexpr uint8_t GameModeValue = 1;
inline constexpr uint8_t VictoryModeValue = 2;
inline constexpr uint8_t GameOverModeValue = 3;
