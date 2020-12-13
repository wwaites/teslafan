/*
 * Very simple PWM Fan controller. Sets the PWM frequency as
 * high as possible to avoide audible while and then listens
 * on the serial port for what value to put on the pin.
 */

#define FAN_PIN 3

void setup() {
  Serial.begin(9600);
  pinMode(FAN_PIN, OUTPUT);
  // set the PWM timer to high frequency (low divisor) to stop
  // audible whine
  TCCR2B = (TCCR2B & 0b11111000) | 0x01;
  analogWrite(FAN_PIN, 128);
}

void loop() {
  if (Serial.available()) {
    char b = Serial.read();
    analogWrite(FAN_PIN, b);
  }
}
