// Command-driven pin driver for LED discovery. Commands (newline-terminated):
//   H<pin> -> drive HIGH, L<pin> -> drive LOW, Z<pin> -> release (INPUT_PULLUP)
// <pin> is Arduino number: 2-13, 14-19 = A0-A5. Footswitch pins D4/D6/D8 are refused.
void setup() { Serial.begin(115200); Serial.println(F("ledscan ready")); }
void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c != 'H' && c != 'L' && c != 'Z') return;
  int p = Serial.parseInt();
  if (p < 2 || p > 19 || p == 4 || p == 6 || p == 8) { Serial.println(F("refused")); return; }
  if (c == 'Z') pinMode(p, INPUT_PULLUP);
  else { pinMode(p, OUTPUT); digitalWrite(p, c == 'H' ? HIGH : LOW); }
  Serial.print(c); Serial.println(p);
}
