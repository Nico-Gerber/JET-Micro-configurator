// MIDI pin check: assumes MIDI out on hardware TX (D1) at 31250 baud.
// Cycles HX Stomp snapshots 1->2->3 via CC69 (values 0,1,2) on channel 1 every 2 s.
void setup() { Serial.begin(31250); }
void loop() {
  static uint8_t v = 0;
  Serial.write(0xB0); Serial.write(69); Serial.write(v);
  v = (v + 1) % 3;
  delay(2000);
}
