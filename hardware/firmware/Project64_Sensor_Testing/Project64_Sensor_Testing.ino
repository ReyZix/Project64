#include <Arduino.h>
#include <cstring>

constexpr bool USE_REAL_SENSORS = false;  // Leave false for testing at home.
constexpr uint8_t SENSOR_E2 = 32;
constexpr uint8_t SENSOR_E3 = 33;
constexpr uint32_t STABLE_TIME_MS = 100;

// Internal bits: bit 0 = e2; bit 1 = e3. 255 means no reading yet.
uint8_t simulatedState = 1;
uint8_t candidateState = 255;
uint8_t stableState = 255;
uint32_t candidateSince = 0;
int lastSquare = -1;  // -1 = unknown, 0 = e2, 1 = e3.
bool pieceLifted = false;
bool invalidState = false;
unsigned int moveCount = 0;
const char* squareNames[] = {"e2", "e3"};

char commandLine[40];
size_t commandLength = 0;
bool commandOverflow = false;

uint8_t readSquares() {
  if (!USE_REAL_SENSORS) return simulatedState;

  uint8_t state = 0;
  if (digitalRead(SENSOR_E2) == LOW) state |= 1;
  if (digitalRead(SENSOR_E3) == LOW) state |= 2;
  return state;
}

void printState(uint8_t state) {
  Serial.print("STATE: e2=");
  Serial.print((state & 1) ? "1" : "0");
  Serial.print(" e3=");
  Serial.println((state & 2) ? "1" : "0");
}

void processStableState(uint8_t state) {
  printState(state);

  if (state == 3) {
    invalidState = true;
    pieceLifted = false;
    Serial.println("ERROR: both squares occupied. Fix the setup, then type reset.");
    return;
  }
  if (invalidState) {
    Serial.println("Tracking paused. Type reset after fixing the setup.");
    return;
  }

  if (state == 0) {
    if (lastSquare < 0) {
      Serial.println("WAIT: place one piece on e2 or e3 to set the starting position.");
    } else {
      pieceLifted = true;
      Serial.print("LIFTED: ");
      Serial.println(squareNames[lastSquare]);
    }
    return;
  }

  int currentSquare = (state == 1) ? 0 : 1;
  if (lastSquare < 0) {
    lastSquare = currentSquare;
    Serial.print("READY: starting piece on ");
    Serial.println(squareNames[currentSquare]);
  } else if (currentSquare == lastSquare) {
    if (pieceLifted) Serial.println("CANCELLED: piece returned to its original square.");
  } else {
    // Compare stable occupied positions even if a quick lift's empty
    // state was shorter than STABLE_TIME_MS. This assumes ONE piece.
    Serial.print("MOVE: ");
    Serial.print(squareNames[lastSquare]);
    Serial.println(squareNames[currentSquare]);
    ++moveCount;
    lastSquare = currentSquare;
  }
  pieceLifted = false;
}

void updateSensors() {
  uint8_t reading = readSquares();
  uint32_t now = millis();
  if (reading != candidateState) {
    candidateState = reading;
    candidateSince = now;
  }
  if (candidateState != stableState &&
      static_cast<uint32_t>(now - candidateSince) >= STABLE_TIME_MS) {
    stableState = candidateState;
    processStableState(stableState);
  }
}

void resetTracking() {
  if (!USE_REAL_SENSORS) simulatedState = 1;
  candidateState = 255;
  stableState = 255;
  lastSquare = -1;
  pieceLifted = false;
  invalidState = false;
  moveCount = 0;
  Serial.println("RESET: move count cleared; establishing a new starting position.");
}

void printHelp() {
  Serial.println("Commands: state 10, state 00, state 01, state 11, reset, status, help");
  Serial.println("state digits: first=e2, second=e3. 1=occupied, 0=empty.");
  Serial.println("Enter one command at a time; wait for STATE before the next.");
}

void processCommand(const char* line) {
  if (strcmp(line, "reset") == 0) {
    resetTracking();
  } else if (strcmp(line, "help") == 0) {
    printHelp();
  } else if (strcmp(line, "status") == 0) {
    Serial.println(USE_REAL_SENSORS ? "MODE: REAL SENSORS" : "MODE: SIMULATION");
    if (stableState == 255) Serial.println("Waiting for a stable reading.");
    else printState(stableState);
    Serial.print("Completed moves since reset: ");
    Serial.println(moveCount);
    if (invalidState) Serial.println("Tracking paused: fix setup and type reset.");
  } else if (strcmp(line, "state 10") == 0 || strcmp(line, "state 00") == 0 ||
             strcmp(line, "state 01") == 0 || strcmp(line, "state 11") == 0) {
    if (USE_REAL_SENSORS) {
      Serial.println("state commands are disabled in real-sensor mode.");
      return;
    }
    uint8_t nextState = (line[6] == '1' ? 1 : 0) | (line[7] == '1' ? 2 : 0);
    if (nextState == simulatedState) {
      Serial.println("Input unchanged; no additional move.");
      return;
    }
    simulatedState = nextState;
    Serial.println("Simulated input updated; waiting for a stable reading.");
  } else {
    Serial.println("Unknown command. Type help. State was not changed.");
  }
}

void readSerialCommands() {
  while (Serial.available()) {
    char input = static_cast<char>(Serial.read());
    if (input == '\r') continue;
    if (input == '\n') {
      commandLine[commandLength] = '\0';
      if (commandOverflow) Serial.println("Command too long; line discarded.");
      else if (commandLength > 0) processCommand(commandLine);
      commandLength = 0;
      commandOverflow = false;
    } else if (commandLength + 1 < sizeof(commandLine)) {
      commandLine[commandLength++] = input;
    } else {
      commandOverflow = true;
    }
  }
}

void setup() {
  Serial.begin(115200);
  if (USE_REAL_SENSORS) {
    // External 10k pull-ups to 3.3 V are required in this mode.
    pinMode(SENSOR_E2, INPUT);
    pinMode(SENSOR_E3, INPUT);
  }
  Serial.println("Project64 two-square test. NO Bluetooth or physical movement.");
  Serial.println(USE_REAL_SENSORS ? "MODE: REAL SENSORS" : "MODE: SIMULATION");
  printHelp();
  resetTracking();
}

void loop() {
  readSerialCommands();
  updateSensors();
  delay(5);
}
