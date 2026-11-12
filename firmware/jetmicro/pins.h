// JET Micro HX 3.0 pin map (Arduino Nano, ATmega328P) — confirmed on hardware.
#pragma once

// Footswitches: wired to GND, read with INPUT_PULLUP, active LOW.
// Mapping assumes presses were made in order LEFT, RIGHT, TOP (confirmed by Nico).
#define PIN_BTN_LEFT   4   // "A"
#define PIN_BTN_RIGHT  8   // "B"
#define PIN_BTN_TOP    6   // "C"

// RGB LED: all three are PWM pins. Common cathode (HIGH = on).
#define PIN_LED_R      10
#define PIN_LED_G      11
#define PIN_LED_B      9
#define LED_COMMON_ANODE 0

// MIDI out: CONFIRMED on hardware TX (D1), 31250 baud (HX Stomp followed CC69 on ch 1).
