// TEST CODE: Lamp Blink
// This code will turn the lamp ON and OFF every second.

const int PIN_MOSFET = 16; // Using Pin 16 as discussed

void setup() {
  // Initialize serial communication for debugging
  Serial.begin(115200);
  
  // Set the MOSFET pin as an OUTPUT
  pinMode(PIN_MOSFET, OUTPUT);
  
  Serial.println("Test started! The lamp should blink.");
}

void loop() {
  // Turn the lamp ON (send 3.3V to the Gate)
  digitalWrite(PIN_MOSFET, HIGH);
  Serial.println("Lamp is ON");
  delay(1000); // Wait for 1 second

  // Turn the lamp OFF (send 0V to the Gate)
  digitalWrite(PIN_MOSFET, LOW);
  Serial.println("Lamp is OFF");
  delay(1000); // Wait for 1 second
}
