// Config data model for the JET Micro replacement firmware.
// Everything the configurator edits lives in struct Config (stored in EEPROM).
#pragma once
#include <Arduino.h>

#define NUM_MODES      3
#define NUM_SLOTS      17   // 3 buttons x (short,double,long) + 4 combos x (short,long)
#define ACTS_PER_SLOT  2
#define NUM_BTN        3    // 0 = LEFT, 1 = RIGHT, 2 = TOP  (mask bits 1, 2, 4)

// HX Stomp MIDI numbers. Likely-correct but VERIFY ON HARDWARE (see docs).
#define CC_TAP_TEMPO     64
#define CC_TUNER         68
#define CC_SNAPSHOT      69   // 0/1/2 = snap 1/2/3, 8 = next, 9 = previous
#define CC_PRESET_STEP   72   // 64..127 = next preset, 0..63 = previous
#define CC_FS_BASE       48   // FS1..FS5 = CC49..CC53

enum ActType : uint8_t {
  ACT_NONE = 0,
  ACT_CC,           // ch, a = cc,  b = value
  ACT_CC_TOGGLE,    // ch, a = cc,  b = "on" value (off sends 0)
  ACT_PC,           // ch, a = program 0..125
  ACT_PRESET_STEP,  // ch, a = 0 down / 1 up
  ACT_BANK_STEP,    // ch, a = 0 down / 1 up, b = method (BANK_*)
  ACT_SNAPSHOT,     // ch, a = CC69 value
  ACT_TUNER,        // ch  (toggles CC68 127/0)
  ACT_TAP,          // ch  (CC64 127)
  ACT_FS_EMU,       // ch, a = footswitch 1..5 (press 127, release 0)
  ACT_MODE_GOTO,    // a = mode
  ACT_MODE_NEXT,
  ACT_MODE_PREV,    // return to the previously active mode
  ACT_DELAY,        // a = delay in units of 10 ms (applies before the next message)
  ACT_TYPE_COUNT
};

enum BankMethod : uint8_t {
  BANK_CC72_X3 = 0,  // send CC72 three times (stateless; in-between presets blip)
  BANK_PC_TRACK = 1, // track preset locally, send one PC (drifts if Stomp changed by hand)
  BANK_FS_EMU = 2    // emulate Stomp FS4/FS5 (CC52/CC53) - Stomp must map them to bank/preset
};

struct Action { uint8_t type, ch, a, b; };

enum SlotKind : uint8_t { PRESS_SHORT = 0, PRESS_DOUBLE = 1, PRESS_LONG = 2 };

#define CFG_FLAG_FEEDBACK 0x01   // flash LED when an action fires

struct Config {
  uint16_t doubleWindowMs;   // max gap for a double press
  uint16_t longPressMs;      // hold time for long press
  uint8_t  debounceMs;
  uint8_t  bankDelayMs;      // gap between the 3 CC72 messages
  uint8_t  flags;
  uint8_t  doubleEnable[NUM_MODES];       // bit n = double press active on button n in that mode
  uint8_t  color[NUM_MODES][3];           // RGB 0..255
  Action   slot[NUM_MODES][NUM_SLOTS][ACTS_PER_SLOT];
};
