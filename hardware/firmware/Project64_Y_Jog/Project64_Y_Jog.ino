#include <Arduino.h>
#include <cstring>

constexpr uint8_t LEFT_STEP_PIN = 25;
constexpr uint8_t RIGHT_STEP_PIN = 23;
constexpr uint8_t LEFT_DIRECTION_PIN = 26;
constexpr uint8_t RIGHT_DIRECTION_PIN = 27;
constexpr uint8_t LEFT_ENABLE_PIN = 32;
constexpr uint8_t RIGHT_ENABLE_PIN = 33;

// With the crossbar disconnected from the gantry plates, verify that left+
// and right+ move the belt clamps toward the same end of the frame.
constexpr bool LEFT_DIRECTION_INVERTED = false;
constexpr bool RIGHT_DIRECTION_INVERTED = false;

// With 1.8-degree motors, 8 microsteps and 20-tooth GT2 pulleys,
// 80 pulses correspond to about 2 mm of belt travel.
constexpr uint16_t JOG_PULSES = 80;
constexpr uint16_t STEP_HIGH_US = 10;
constexpr uint16_t STEP_LOW_US = 2990;
constexpr uint16_t FIRST_STEP_SETTLE_MS = 200;

char command[12];
size_t commandLength = 0;
bool invalidLine = false;

void printCommands() {
  Serial.println("Project64 USB Y-axis jog ready.");
  Serial.println("Send one line: left+ left- right+ right- y+ y-");
  Serial.println("One-side commands are for uncoupled belt tests only.");
}

void jog(bool left, bool right, bool forward) {
  digitalWrite(LEFT_DIRECTION_PIN, forward != LEFT_DIRECTION_INVERTED);
  digitalWrite(RIGHT_DIRECTION_PIN, forward != RIGHT_DIRECTION_INVERTED);
  digitalWrite(LEFT_ENABLE_PIN, left ? LOW : HIGH);
  digitalWrite(RIGHT_ENABLE_PIN, right ? LOW : HIGH);
  delay(20);

  for (uint16_t pulse = 0; pulse < JOG_PULSES; ++pulse) {
    if (left) digitalWrite(LEFT_STEP_PIN, HIGH);
    if (right) digitalWrite(RIGHT_STEP_PIN, HIGH);
    delayMicroseconds(STEP_HIGH_US);
    if (left) digitalWrite(LEFT_STEP_PIN, LOW);
    if (right) digitalWrite(RIGHT_STEP_PIN, LOW);
    delayMicroseconds(STEP_LOW_US);
    // Allow the driver's standstill current regulation to settle.
    if (pulse == 0) delay(FIRST_STEP_SETTLE_MS);
  }

  digitalWrite(LEFT_ENABLE_PIN, HIGH);
  digitalWrite(RIGHT_ENABLE_PIN, HIGH);
  Serial.println("Jog pulses sent. Check movement at the gantry.");
}

void handleCommand(const char* line) {
  if (strcmp(line, "left+") == 0) jog(true, false, true);
  else if (strcmp(line, "left-") == 0) jog(true, false, false);
  else if (strcmp(line, "right+") == 0) jog(false, true, true);
  else if (strcmp(line, "right-") == 0) jog(false, true, false);
  else if (strcmp(line, "y+") == 0) jog(true, true, true);
  else if (strcmp(line, "y-") == 0) jog(true, true, false);
  else printCommands();
}

void setup() {
  // The external 10-kilohm pull-ups keep each driver disabled during reset.
  pinMode(LEFT_ENABLE_PIN, INPUT_PULLUP);
  pinMode(RIGHT_ENABLE_PIN, INPUT_PULLUP);
  digitalWrite(LEFT_ENABLE_PIN, HIGH);
  digitalWrite(RIGHT_ENABLE_PIN, HIGH);
  pinMode(LEFT_ENABLE_PIN, OUTPUT);
  pinMode(RIGHT_ENABLE_PIN, OUTPUT);
  pinMode(LEFT_STEP_PIN, OUTPUT);
  pinMode(RIGHT_STEP_PIN, OUTPUT);
  digitalWrite(LEFT_STEP_PIN, LOW);
  digitalWrite(RIGHT_STEP_PIN, LOW);
  pinMode(LEFT_DIRECTION_PIN, OUTPUT);
  pinMode(RIGHT_DIRECTION_PIN, OUTPUT);
  Serial.begin(115200);
  printCommands();
}

void loop() {
  while (Serial.available() > 0) {
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
