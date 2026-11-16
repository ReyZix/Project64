#pragma once
#include <string>
#include <vector>

/*
 * The link to the physical board (the ESP32 gantry). Stockfish and Lichess code only use this
 * interface, so the real link can be swapped in later without changing them.
 * It has the same job as IHardwareBridge in the phone app.
 */
class HardwareBridge {
 public:
  virtual ~HardwareBridge() = default;
  virtual std::string name() const = 0;
  virtual bool connected() const = 0;
  /** Sends one move like "e2e4" to the board. True only if the board accepted it. */
  virtual bool sendMove(const std::string& uci) = 0;
};

/** True for a move in UCI form: e2e4, or e7e8q for a promotion. */
bool isUciMove(const std::string& text);

/** A pretend board for testing without the ESP32. It remembers what it was sent. */
class MockBridge : public HardwareBridge {
 public:
  std::string name() const override { return "Mock"; }
  bool connected() const override { return plugged; }
  bool sendMove(const std::string& uci) override {
    if (!plugged || !isUciMove(uci)) return false;
    sent.push_back(uci);
    return true;
  }

  bool plugged = true; // set false to act like a board that is not connected
  std::vector<std::string> sent;
};
