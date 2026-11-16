#pragma once
#include <string>
#include <vector>

#include "Game.h"

/*
 * Runs the real Stockfish program and talks to it in UCI, the text protocol every chess
 * engine understands. We write commands to its input and read its answers from its output.
 *
 *   us:        position startpos moves e2e4 e7e5
 *   us:        go depth 12
 *   Stockfish: bestmove g1f3
 *
 * Stockfish is also our rule book: "go perft 1" makes it list every legal move in a position.
 */
class StockfishEngine {
 public:
  ~StockfishEngine() { stop(); }

  /**
   * Starts the engine. `command` is what to run, for example "stockfish" or a full path.
   * Returns false if it did not answer like a UCI engine.
   */
  bool start(const std::string& command);
  void stop();
  bool running() const { return pid_ > 0; }

  /**
   * False if the last question got no answer even after restarting the engine.
   * Check this before trusting an empty result: "no answer" is not the same as "no legal moves".
   */
  bool ok() const { return ok_; }
  const std::string& engineName() const { return name_; }

  /** Playing strength, 0 (weakest) to 20 (strongest). */
  void setSkillLevel(int level);

  /** Stockfish's move for the side to play, or "" if there is none (game over) or it timed out. */
  std::string bestMove(const Game& game, int depth = 12, int timeoutMs = 15000);

  /** Every legal move in the position. Empty means checkmate or stalemate. */
  std::vector<std::string> legalMoves(const Game& game);

  /** True if the side to move is in check. */
  bool inCheck(const Game& game);

 private:
  void send(const std::string& line);
  /** Reads one line. False if nothing arrived within `timeoutMs` or the engine quit. */
  bool readLine(std::string& line, int timeoutMs);
  /** Reads lines until one starts with `prefix`. */
  bool waitFor(const std::string& prefix, std::string& line, int timeoutMs);
  void setPosition(const Game& game);

  // One try at each question. They return false if the engine did not give a complete answer.
  bool askBestMove(const Game& game, int depth, int timeoutMs, std::string& move);
  bool askLegalMoves(const Game& game, std::vector<std::string>& moves);
  bool askInCheck(const Game& game, bool& check);
  /** Stops whatever is left of the engine and starts a fresh one with the same settings. */
  bool restart();

  int pid_ = -1;
  int toEngine_ = -1;   // we write commands here
  int fromEngine_ = -1; // we read answers here
  std::string buffer_;
  std::string name_;
  std::string command_;  // how the engine was started, kept so it can be restarted
  int skillLevel_ = -1;  // -1 until setSkillLevel is called
  bool ok_ = true;
};
