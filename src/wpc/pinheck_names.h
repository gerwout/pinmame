/* Domino's Spectacular Pinball Adventure (pinHeck): switch, lamp and output names for table scripts.

   Sources: Dominos-Switch-Matrix.pdf, Dominos-Lamp-Matrix.pdf and Spooky_Pinball_Domino's_Solenoid_List.pdf
   (Ken Layton, 2019). Not in them: User (2) and Back (5), the board's USER_0 and BACK inputs in the firmware's
   cabinet read order (design spec 2.2); the optos 95 (Center Ramp) and 96 (Oven Ramp), from the firmware's
   switch handlers (Plan 8b, Ruling 5). Numbering of the pinHeck driver (src/wpc/pinheck.c):
     matrix switch or lamp n (0-63)  (n / 8 + 1) * 10 + n % 8 + 1
     cabinet switch n                n (1-8), n + 82 (9-15)
     start button lamp               91
     coil n (0-23)                   solenoid n + 1
     GI_0..7, GI_8..15               solenoids 25-32, 37-44
     on-board RGB left R, G, B, right R, G, B    51-56 (0-255)
     servos 0-4                      57-61 (0-255 = 1.0-2.0 ms pulse)
     external RGB LED R, G, B        62-64 (0-255)
   A table presses the flipper buttons through PinMAME's flipper column, 114 (left) and 112 (right),
   which PinMAME copies to cabinet switches 4 and 3 every frame. Numbers not listed are unused.
   HandleMechanics bit 0 simulates the Noid (servo 0, closing Noid Home 58), bit 1 the target bank (servo 1);
   a table that moves them itself clears the bit. */
#ifndef PINHECK_NAMES_H
#define PINHECK_NAMES_H

typedef struct { int num; const char *id; const char *name; } pinheck_name_t;

static const pinheck_name_t pinheck_dominos_switch_names[] = {
  {   1, "swCoinDoor",  "Coin Door (closed)" },
  {   2, "swUser",      "User" },
  {   3, "swRFlip",     "Right Flipper" },
  {   4, "swLFlip",     "Left Flipper" },
  {   5, "swBack",      "Back" },
  {   6, "swEnter",     "Enter (menu)" },
  {   7, "swCoin",      "Coin Mech" },
  {   8, "swTilt",      "Tilt" },
  {  11, "swShooter",   "Shooter Lane" },
  {  12, "swTrough1",   "Trough Ball 1" },
  {  13, "swTrough2",   "Trough Ball 2" },
  {  14, "swTrough3",   "Trough Ball 3" },
  {  15, "swRFlipEOS",  "Right Flipper EOS" },
  {  16, "swRSling",    "Right Sling" },
  {  17, "swRInlane",   "Right Inlane" },
  {  18, "swROutlane",  "Right Outlane" },
  {  21, "swLFlipEOS",  "Left Flipper EOS" },
  {  22, "swLSling",    "Left Sling" },
  {  23, "swLInlane",   "Left Inlane" },
  {  24, "swLOutlane",  "Left Outlane" },
  {  25, "swLScoop",    "Left Scoop" },
  {  26, "swBankR",     "Noid Bank Right" },
  {  27, "swBankM",     "Noid Bank Middle" },
  {  28, "swBankL",     "Noid Bank Left" },
  {  31, "swDelivery",  "Target Delivery" },
  {  32, "swQuality",   "Target Quality Check" },
  {  33, "swBake",      "Target Bake" },
  {  34, "swPrepare",   "Target Prepare" },
  {  35, "swOrder",     "Target Order Placed" },
  {  36, "swROrbit",    "Right Orbit" },
  {  37, "swRNoidOrb",  "Right Noid Orbit" },
  {  38, "swLNoidOrb",  "Left Noid Orbit" },
  {  41, "swStarLane",  "Star Lane" },
  {  42, "sw5Lane",     "5 Lane" },
  {  43, "swRPop",      "Right Pop Bumper" },
  {  44, "swLowerPop",  "Lower Pop Bumper" },
  {  45, "swLPop",      "Left Pop Bumper" },
  {  46, "swLOrbit",    "Left Orbit" },
  {  47, "swSpinner",   "Spinner" },
  {  48, "swRScoop",    "Right Scoop" },
  {  58, "swNoidHome",  "Noid Home" },
  {  94, "swStart",     "Start Button" },
  {  95, "swRampOpto",  "Opto (Center Ramp)" },
  {  96, "swOvenOpto",  "Opto (Oven Ramp)" },
  { 112, "swLRFlip",    "Right Flipper button (reaches switch 3)" },
  { 114, "swLLFlip",    "Left Flipper button (reaches switch 4)" },
  { 0 }
};

static const pinheck_name_t pinheck_dominos_lamp_names[] = {
  {  11, "lLetterO",           "O" },
  {  12, "lLetterV",           "V" },
  {  13, "lMystery",           "Mystery" },
  {  14, "lStartCareer",       "Start Career" },
  {  15, "lGoldFranny",        "Gold Franny" },
  {  16, "lFranchisee",        "Franchisee" },
  {  17, "lManager",           "Manager" },
  {  18, "lDriver",            "Driver" },
  {  21, "lOrderAgain",        "Order Again" },
  {  22, "lMakePizza",         "Make Pizza" },
  {  23, "lLostTopping",       "Lost Topping" },
  {  24, "lPizzaDispatch",     "Pizza Dispatch" },
  {  25, "lMegaWeek",          "Mega Week" },
  {  26, "lHandleTheRush",     "Handle The Rush" },
  {  27, "lLetterE",           "E" },
  {  28, "lLetterN",           "N" },
  {  31, "lLDominoOrbit",      "Left Domino Orbit" },
  {  32, "lLMakePizzaOrbit",   "Left Make Pizza Orbit" },
  {  33, "lLostToppingL",      "Lost Topping Left (Noid Orbit)" },
  {  34, "lTarget1",           "Target 1" },
  {  35, "lTarget2",           "Target 2" },
  {  36, "lTarget3",           "Target 3" },
  {  37, "lBattleNoidN",       "Battle Noid N" },
  {  41, "lLostToppingR",      "Lost Topping Right (Noid Orbit)" },
  {  42, "lRampPizzaDispatch", "Ramp Pizza Dispatch" },
  {  43, "lExtraBall",         "Extra Ball" },
  {  44, "lRampDomino",        "Ramp Domino" },
  {  45, "lOvenRampRush",      "Oven Ramp Rush" },
  {  46, "lOvenRampJackpot",   "Oven Ramp Jackpot" },
  {  47, "lOvenRampMegaWeek",  "Oven Ramp Mega Week" },
  {  48, "lTrackerGreen",      "Pizza Tracker Green Side" },
  {  51, "lDominoDotTop",      "Domino Dot Top" },
  {  52, "lDominoDotMiddle",   "Domino Dot Middle" },
  {  53, "lDominoDotBottomL",  "Domino Dot Bottom Left" },
  {  54, "lPizzaWars",         "Pizza Wars" },
  {  55, "lGlobalConquest",    "Global Conquest" },
  {  56, "lFastestPizzaMaker", "Fastest Pizza Maker" },
  {  57, "l5Lane",             "5 Lane" },
  {  58, "lStarLane",          "Star Lane" },
  {  61, "lOvenPizzaScoop",    "Oven Pizza Scoop" },
  {  62, "lMakePizzasROrbit",  "Make Pizzas Right Orbit" },
  {  63, "lOrderPlaced",       "Order Placed" },
  {  64, "lPrepare",           "Prepare" },
  {  65, "lBake",              "Bake" },
  {  66, "lQualityCheck",      "Quality Check" },
  {  67, "lDelivery",          "Delivery" },
  {  68, "lTrackerBlue",       "Pizza Tracker Blue Side" },
  {  91, "lStart",             "Start Button" },
  { 0 }
};

/* solenoid outputs: coils, GI strings, RGB channels and servos */
static const pinheck_name_t pinheck_dominos_solenoid_names[] = {
  {  1, "sKnocker",    "Knocker (optional)" },
  {  2, "sShaker",     "Shaker Motor (optional)" },
  {  3, "sLowerPop",   "Lower Pop Bumper" },
  {  4, "sLPop",       "Left Pop Bumper" },
  {  5, "sRPop",       "Right Pop Bumper" },
  {  6, "sMagnet",     "Magnet Coil" },
  {  7, "sPost",       "Up Post" },
  {  9, "sLScoop",     "Left Scoop" },
  { 10, "sLFlipHigh",  "Left Flipper High Center (power)" },
  { 11, "sLSling",     "Left Slingshot" },
  { 12, "sLFlipLow",   "Left Flipper Low (hold)" },
  { 13, "sRScoop",     "Right Scoop" },
  { 17, "sLaunch",     "Auto Launcher" },
  { 18, "sLoad",       "Ball Trough" },
  { 19, "sRFlipLow",   "Right Flipper Low (hold)" },
  { 20, "sRSling",     "Right Slingshot" },
  { 21, "sRFlipHigh",  "Right Flipper High Center (power)" },
  { 25, "sGI0",        "GI_0 (backbox)" },
  { 26, "sGI1",        "GI_1 (backbox)" },
  { 27, "sGI2",        "GI_2 (backbox)" },
  { 28, "sGI3",        "GI_3 (backbox)" },
  { 29, "sGI4",        "GI_4 (backbox)" },
  { 30, "sGI5",        "GI_5 (backbox)" },
  { 31, "sGI6",        "GI_6 (backbox)" },
  { 32, "sGI7",        "GI_7 (backbox)" },
  { 37, "sGI8",        "GI_8 (playfield)" },
  { 38, "sGI9",        "GI_9 (playfield)" },
  { 39, "sGI10",       "GI_10 (playfield)" },
  { 40, "sGI11",       "GI_11 (playfield)" },
  { 41, "sGI12",       "GI_12 (playfield)" },
  { 42, "sGI13",       "GI_13 (playfield)" },
  { 43, "sGI14",       "GI_14 (playfield)" },
  { 44, "sGI15",       "GI_15 (playfield)" },
  { 51, "sRGB1R",      "RGB1 (left) red" },
  { 52, "sRGB1G",      "RGB1 (left) green" },
  { 53, "sRGB1B",      "RGB1 (left) blue" },
  { 54, "sRGB2R",      "RGB2 (right) red" },
  { 55, "sRGB2G",      "RGB2 (right) green" },
  { 56, "sRGB2B",      "RGB2 (right) blue" },
  { 57, "sNoid",       "Servo 0: Noid (continuous rotation)" },
  { 58, "sBank",       "Servo 1: target bank" },
  { 59, "sServo2",     "Servo 2" },
  { 60, "sServo3",     "Servo 3" },
  { 61, "sServo4",     "Servo 4" },
  { 62, "sExtR",       "External RGB LED red" },
  { 63, "sExtG",       "External RGB LED green" },
  { 64, "sExtB",       "External RGB LED blue" },
  { 0 }
};

#endif
