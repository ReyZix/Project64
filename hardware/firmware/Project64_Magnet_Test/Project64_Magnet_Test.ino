#include <Arduino.h>
#include <cstring>

// Connect to the input of a 3.3 V-compatible, active-high MOSFET switch.
constexpr uint8_t MAGNET_PIN = 21;
constexpr unsigned long TEST_PULSE_MS = 2000;

bool magnetOn = false;
unsigned long switchedOnAt = 0;
char command[16];
size_t commandLength = 0;
bool invalidLine = false;

void switchOffMagnet() {
  digitalWrite(MAGNET_PIN, LOW);
  magnetOn = false;
}

void handleCommand(const char* line) {
  if (strcmp(line, "mag off") == 0) {
    switchOffMagnet();
    Serial.println("Magnet output off.");
  } else if (strcmp(line, "mag on") == 0) {
    if (magnetOn) return;
    switchedOnAt = millis();
    digitalWrite(MAGNET_PIN, HIGH);
    magnetOn = true;
    Serial.println("Magnet output on for 2 seconds.");
  } else {
    Serial.println("Use: mag on or mag off");
  }
}

void setup() {
  pinMode(MAGNET_PIN, OUTPUT);
  switchOffMagnet();
  Serial.begin(115200);
  Serial.println("Project64 magnet test ready. Use: mag on or mag off");
}

void checkPulseTimeout() {
  if (magnetOn && millis() - switchedOnAt >= TEST_PULSE_MS) {
    switchOffMagnet();
    Serial.println("Magnet output off (test pulse finished).");
  }
}

void loop() {
  checkPulseTimeout();
  while (Serial.available() > 0) {
    checkPulseTimeout();
    const char input = static_cast<char>(Serial.read());
    if (input == '\r') continue;

    if (input == '\n') {
      command[commandLength] = '\0';
      if (invalidLine) Serial.println("Invalid command line.");
      else if (commandLength > 0) handleCommand(command);
      commandLength = 0;
      invalidLine = false;
    } else if (input < ' ' || input > '~' || commandLength >= sizeof(command) - 1) {
      invalidLine = true;
    } else {
      command[commandLength++] = input;
    }
  }
}
