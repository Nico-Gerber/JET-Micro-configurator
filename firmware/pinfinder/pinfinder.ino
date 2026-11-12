// JET Micro pin finder — Arduino Nano (ATmega328P, old bootloader)
// Step 1: press each footswitch (left, right, top) — pin changes are printed.
// Step 2: type "l" + Enter to run the LED test, Enter to advance each step.
// D0/D1 are skipped (USB serial / likely MIDI TX).

const uint8_t pins[] = {2,3,4,5,6,7,8,9,10,11,12,13,A0,A1,A2,A3,A4,A5};
const uint8_t N = sizeof(pins);
int  lastState[N];
bool isButton[N];
int  lastA6, lastA7;

const char* pinName(uint8_t p) {
  static char buf[5];
  if (p >= A0) { sprintf(buf, "A%d", p - A0); } else { sprintf(buf, "D%d", p); }
  return buf;
}

void waitEnter() {
  while (true) {
    if (Serial.available() && Serial.read() == '\n') return;
  }
}

void ledTest() {
  Serial.println(F("\n--- LED TEST --- Do NOT press any footswitch during this test."));
  for (uint8_t i = 0; i < N; i++) {
    if (isButton[i]) continue;               // never drive a button pin
    uint8_t p = pins[i];
    pinMode(p, OUTPUT);
    digitalWrite(p, HIGH);
    Serial.print(pinName(p)); Serial.println(F(" driven HIGH - what does the LED show? (Enter to continue)"));
    waitEnter();
    digitalWrite(p, LOW);
    Serial.print(pinName(p)); Serial.println(F(" driven LOW  - what does the LED show? (Enter to continue)"));
    waitEnter();
    pinMode(p, INPUT_PULLUP);
  }
  Serial.println(F("--- LED TEST DONE ---\n"));
}

void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < N; i++) {
    pinMode(pins[i], INPUT_PULLUP);
    isButton[i] = false;
  }
  delay(50);
  Serial.println(F("\nJET Micro pin finder. Initial states:"));
  for (uint8_t i = 0; i < N; i++) {
    lastState[i] = digitalRead(pins[i]);
    Serial.print(pinName(pins[i])); Serial.print('='); Serial.print(lastState[i]); Serial.print("  ");
  }
  lastA6 = analogRead(A6); lastA7 = analogRead(A7);
  Serial.print(F("\nA6(analog)=")); Serial.print(lastA6);
  Serial.print(F(" A7(analog)=")); Serial.println(lastA7);
  Serial.println(F("\nPress LEFT, then RIGHT, then TOP footswitch. Type 'l' + Enter for LED test."));
}

void loop() {
  for (uint8_t i = 0; i < N; i++) {
    int s = digitalRead(pins[i]);
    if (s != lastState[i]) {
      delay(20);
      s = digitalRead(pins[i]);
      if (s != lastState[i]) {
        lastState[i] = s;
        if (s == LOW) isButton[i] = true;
        Serial.print(pinName(pins[i]));
        Serial.println(s == LOW ? F(" -> LOW  (pressed?)") : F(" -> HIGH (released?)"));
      }
    }
  }
  int a6 = analogRead(A6), a7 = analogRead(A7);
  if (abs(a6 - lastA6) > 200) { Serial.print(F("A6 analog -> ")); Serial.println(a6); lastA6 = a6; }
  if (abs(a7 - lastA7) > 200) { Serial.print(F("A7 analog -> ")); Serial.println(a7); lastA7 = a7; }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'l' || c == 'L') ledTest();
  }
}
