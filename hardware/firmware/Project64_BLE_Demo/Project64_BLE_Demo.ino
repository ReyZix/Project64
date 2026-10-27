#include <Arduino.h>
#include <esp_system.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include <cstring>

constexpr char SERVICE_UUID[] = "0000cb00-0000-1000-8000-00805f9b34fb";
constexpr char BOARD_UUID[]   = "0000cb01-0000-1000-8000-00805f9b34fb";
constexpr char MOVE_UUID[]    = "0000cb02-0000-1000-8000-00805f9b34fb";
constexpr char STATUS_UUID[]  = "0000cb03-0000-1000-8000-00805f9b34fb";
constexpr char COMMAND_UUID[] = "0000cb04-0000-1000-8000-00805f9b34fb";

enum GantryStatus : uint8_t { IDLE = 0, MOVING = 1, HOMING = 2, ERROR_STATE = 3 };
enum EventType : uint8_t { MOVE_EVENT, SYSTEM_EVENT, INVALID_EVENT };

struct IncomingEvent {
  EventType type;
  uint8_t opcode;
  char move[6]; 
};

QueueHandle_t incomingQueue = nullptr;
BLECharacteristic* boardCharacteristic = nullptr;
BLECharacteristic* statusCharacteristic = nullptr;
std::atomic<bool> phoneConnected{false};
std::atomic<bool> disconnected{false};
std::atomic<bool> queueOverflow{false};
GantryStatus gantryStatus = IDLE;
bool simulatedMotionActive = false;
uint32_t simulationStartedAt = 0;
uint32_t lastStatusAt = 0;
char serialLine[40];
size_t serialLength = 0;
bool serialOverflow = false;
uint32_t lastHeartbeatAt = 0;

const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWER_ON";
    case ESP_RST_SW: return "SOFTWARE_RESTART";
    case ESP_RST_PANIC: return "CRASH_OR_PANIC";
    case ESP_RST_INT_WDT: return "INTERRUPT_WATCHDOG";
    case ESP_RST_TASK_WDT: return "TASK_WATCHDOG";
    case ESP_RST_WDT: return "WATCHDOG";
    case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP_WAKE";
    case ESP_RST_BROWNOUT: return "BROWNOUT_POWER_DROP";
    default: return "OTHER_OR_UNKNOWN";
  }
}

bool validSquare(const char* square) {
  return square[0] >= 'a' && square[0] <= 'h' &&
         square[1] >= '1' && square[1] <= '8';
}

// Checks message shape only. chess.js in the app checks chess legality.
bool validMoveText(const char* text, size_t length) {
  if (length != 4 && length != 5) return false;
  if (!validSquare(text) || !validSquare(text + 2)) return false;
  if (text[0] == text[2] && text[1] == text[3]) return false;
  return length == 4 || text[4] == 'q' || text[4] == 'r' ||
         text[4] == 'b' || text[4] == 'n';
}

void reportStatus(GantryStatus nextStatus) {
  gantryStatus = nextStatus;
  uint8_t value = static_cast<uint8_t>(nextStatus);
  statusCharacteristic->setValue(&value, 1); 
  if (phoneConnected.load()) statusCharacteristic->notify();
  Serial.print("Status byte: ");
  Serial.println(value);
}

class ConnectionCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    phoneConnected.store(true);
    Serial.println("Phone connected; app must subscribe to CB01 and CB03.");
  }

  void onDisconnect(BLEServer*) override {
    phoneConnected.store(false);
    disconnected.store(true);
  }
};

// Callbacks only validate/copy messages. Work happens in loop(), not in BLE callbacks.
class WriteCallbacks : public BLECharacteristicCallbacks {
 public:
  explicit WriteCallbacks(bool isMove) : isMove_(isMove) {}

  void onWrite(BLECharacteristic* characteristic) override {
    auto value = characteristic->getValue();
    IncomingEvent event{};
    event.type = INVALID_EVENT;
    if (isMove_ && validMoveText(value.c_str(), value.length())) {
      event.type = MOVE_EVENT;
      memcpy(event.move, value.c_str(), value.length());
    } else if (!isMove_ && value.length() == 1) {
      event.opcode = static_cast<uint8_t>(value[0]);
      if (event.opcode >= 1 && event.opcode <= 3) event.type = SYSTEM_EVENT;
    }
    
    if (event.type == MOVE_EVENT && uxQueueSpacesAvailable(incomingQueue) <= 1) {
      queueOverflow.store(true);
      return;
    }
    BaseType_t result = event.type == SYSTEM_EVENT
        ? xQueueSendToFront(incomingQueue, &event, 0)
        : xQueueSend(incomingQueue, &event, 0);
    if (result != pdTRUE) queueOverflow.store(true);
  }

 private:
  bool isMove_;
};

void startSimulatedMotion(GantryStatus status) {
  simulatedMotionActive = true;
  simulationStartedAt = millis();
  reportStatus(status);
}

void processEvent(const IncomingEvent& event) {
  if (event.type == INVALID_EVENT) {
    simulatedMotionActive = false;
    Serial.println("Invalid protocol message. Reset ESP32 or send HOME to clear ERROR.");
    reportStatus(ERROR_STATE);
    return;
  }
  if (event.type == SYSTEM_EVENT) {
    if (event.opcode == 1) {
      Serial.println("HOME received: SIMULATING homing, no motors connected.");
      startSimulatedMotion(HOMING);
    } else if (event.opcode == 2) {
      simulatedMotionActive = false;
      xQueueReset(incomingQueue);
      Serial.println("E-STOP received: simulation stopped; ERROR latched until HOME/reset.");
      reportStatus(ERROR_STATE);
    } else {
      Serial.println("REBOOT received.");
      ESP.restart();
    }
    return;
  }
  if (gantryStatus != IDLE) {
    Serial.println("Move ignored: gantry is not IDLE. App should send one move at a time.");
    return;
  }
  Serial.print("App move received: ");
  Serial.println(event.move);
  Serial.println("SIMULATED movement only. Replace this call with the real motion executor later.");
  startSimulatedMotion(MOVING);
}

void processSerialLine(const char* line) {
  if (strncmp(line, "board ", 6) != 0 ||
      !validMoveText(line + 6, strlen(line + 6))) {
    Serial.println("Use: board e2e4   (or board e7e8q for promotion).");
    return;
  }
  if (!phoneConnected.load() || gantryStatus != IDLE) {
    Serial.println("Board event not sent: connect phone and wait for IDLE first.");
    return;
  }
  const char* move = line + 6;
  boardCharacteristic->setValue(reinterpret_cast<uint8_t*>(const_cast<char*>(move)), strlen(move));
  boardCharacteristic->notify();
  Serial.print("SIMULATED player move sent over CB01: ");
  Serial.println(move);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== PROJECT64 BLE DIAGNOSTIC V2 START ===");
  esp_reset_reason_t resetReason = esp_reset_reason();
  Serial.printf("Reset reason: %s (%d)\n", resetReasonName(resetReason),
                static_cast<int>(resetReason));
  incomingQueue = xQueueCreate(8, sizeof(IncomingEvent));
  if (!incomingQueue) {
    Serial.println("Could not allocate command queue; BLE not started.");
    while (true) delay(1000);
  }

  BLEDevice::init("Project64");
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ConnectionCallbacks());
  BLEService* service = server->createService(SERVICE_UUID);

  boardCharacteristic = service->createCharacteristic(
      BOARD_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  boardCharacteristic->addDescriptor(new BLE2902());
  BLECharacteristic* moveCharacteristic = service->createCharacteristic(
      MOVE_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  moveCharacteristic->setCallbacks(new WriteCallbacks(true));
  statusCharacteristic = service->createCharacteristic(
      STATUS_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  statusCharacteristic->addDescriptor(new BLE2902());
  BLECharacteristic* commandCharacteristic = service->createCharacteristic(
      COMMAND_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  commandCharacteristic->setCallbacks(new WriteCallbacks(false));
  reportStatus(IDLE);
  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID); // Required by the app's scan filter.
  advertising->setScanResponse(true);
  advertising->start();
  Serial.println("Project64 BLE ready. USB-only communication test, no physical movement.");
  Serial.println("In the app: Settings > BLE > Project64 > Connect.");
  Serial.println("To simulate a player move, type: board e2e4 (Newline enabled).");
}

void loop() {
  if (disconnected.exchange(false)) {
    simulatedMotionActive = false;
    xQueueReset(incomingQueue);
    reportStatus(IDLE);
    if (!phoneConnected.load()) BLEDevice::startAdvertising();
    Serial.println("Phone disconnected; simulation reset and advertising restarted.");
  }
  if (queueOverflow.exchange(false)) {
    simulatedMotionActive = false;
    xQueueReset(incomingQueue);
    Serial.println("Command queue overflow. Reset ESP32 or send HOME to clear ERROR.");
    reportStatus(ERROR_STATE);
  }
  IncomingEvent event{};
  while (xQueueReceive(incomingQueue, &event, 0) == pdTRUE) processEvent(event);

  if (simulatedMotionActive && millis() - simulationStartedAt >= 1000) {
    simulatedMotionActive = false;
    Serial.println("SIMULATED movement/homing complete. Physical position was not verified.");
    reportStatus(IDLE);
  }
 
  if (phoneConnected.load() && millis() - lastStatusAt >= 1000) {
    lastStatusAt = millis();
    uint8_t value = static_cast<uint8_t>(gantryStatus);
    statusCharacteristic->setValue(&value, 1);
    statusCharacteristic->notify();
  }
  while (Serial.available()) {
    char input = static_cast<char>(Serial.read());
    if (input == '\r') continue;
    if (input == '\n') {
      serialLine[serialLength] = '\0';
      if (serialOverflow) Serial.println("Serial command too long; line discarded.");
      else if (serialLength) processSerialLine(serialLine);
      serialLength = 0;
      serialOverflow = false;
    } else if (serialLength + 1 < sizeof(serialLine)) {
      serialLine[serialLength++] = input;
    } else {
      serialOverflow = true;
    }
  }
  if (millis() - lastHeartbeatAt >= 5000) {
    lastHeartbeatAt = millis();
    Serial.printf("V2 ALIVE uptime=%lu seconds | phone=%s | status=%u\n",
                  static_cast<unsigned long>(millis() / 1000),
                  phoneConnected.load() ? "connected" : "disconnected",
                  static_cast<unsigned int>(gantryStatus));
  }
  delay(1);
}
