#include "StockfishEngine.h"

#include <algorithm>
#include <csignal>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

bool StockfishEngine::start(const std::string& command) {
  stop();
  command_ = command;
  std::signal(SIGPIPE, SIG_IGN); // if the engine dies, writing to it should fail, not kill us

  // Two pipes: one carries our commands to the engine, the other carries its answers back.
  int in[2], out[2];
  if (pipe(in) != 0) return false;
  if (pipe(out) != 0) {
    close(in[0]);
    close(in[1]);
    return false;
  }

  pid_ = fork();
  if (pid_ < 0) {
    close(in[0]);
    close(in[1]);
    close(out[0]);
    close(out[1]);
    return false;
  }
  if (pid_ == 0) {
    // Child process: connect the pipes to its keyboard and screen, then become Stockfish.
    dup2(in[0], STDIN_FILENO);
    dup2(out[1], STDOUT_FILENO);
    close(in[0]);
    close(in[1]);
    close(out[0]);
    close(out[1]);
    execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  close(in[0]);
  close(out[1]);
  toEngine_ = in[1];
  fromEngine_ = out[0];

  // The UCI handshake: "uci" -> "uciok", then "isready" -> "readyok".
  send("uci");
  std::string line;
  for (;;) {
    if (!readLine(line, 10000)) return stop(), false;
    if (line.rfind("id name ", 0) == 0) name_ = line.substr(8);
    if (line == "uciok") break;
  }
  send("isready");
  if (!waitFor("readyok", line, 10000)) return stop(), false;
  send("ucinewgame");
  std::cout << "[Stockfish] connected: " << name_ << "\n";
  return true;
}

void StockfishEngine::stop() {
  if (pid_ <= 0) return;
  send("quit");
  close(toEngine_);
  close(fromEngine_);
  // Give it a moment to quit by itself, then make sure it is gone.
  for (int i = 0; i < 20 && waitpid(pid_, nullptr, WNOHANG) == 0; i++) usleep(50 * 1000);
  kill(pid_, SIGKILL);
  waitpid(pid_, nullptr, 0);
  pid_ = toEngine_ = fromEngine_ = -1;
  buffer_.clear();
}

void StockfishEngine::send(const std::string& line) {
  if (toEngine_ < 0) return;
  std::string text = line + "\n";
  // If the engine has died this write fails. The missing answer is what tells us, so nothing to do here.
  if (write(toEngine_, text.data(), text.size()) < 0) return;
}

bool StockfishEngine::restart() {
  std::cout << "[Stockfish] the engine stopped answering, restarting it\n";
  if (command_.empty() || !start(command_)) return false;
  if (skillLevel_ >= 0) setSkillLevel(skillLevel_);
  return true;
}

bool StockfishEngine::readLine(std::string& line, int timeoutMs) {
  for (;;) {
    size_t newline = buffer_.find('\n');
    if (newline != std::string::npos) {
      line = buffer_.substr(0, newline);
      buffer_.erase(0, newline + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      return true;
    }
    if (fromEngine_ < 0) return false;
    pollfd waiting{fromEngine_, POLLIN, 0};
    if (poll(&waiting, 1, timeoutMs) <= 0) return false; // nothing arrived in time
    char chunk[4096];
    ssize_t n = read(fromEngine_, chunk, sizeof chunk);
    if (n <= 0) return false; // the engine closed
    buffer_.append(chunk, static_cast<size_t>(n));
  }
}

bool StockfishEngine::waitFor(const std::string& prefix, std::string& line, int timeoutMs) {
  while (readLine(line, timeoutMs)) {
    if (line.rfind(prefix, 0) == 0) return true;
  }
  return false;
}

void StockfishEngine::setSkillLevel(int level) {
  level = std::max(0, std::min(20, level));
  skillLevel_ = level;
  send("setoption name Skill Level value " + std::to_string(level));
}

void StockfishEngine::setPosition(const Game& game) {
  std::string command = game.fromStartPosition() ? "position startpos" : "position fen " + game.startFen();
  if (!game.moves().empty()) {
    command += " moves";
    for (const std::string& move : game.moves()) command += " " + move;
  }
  send(command);
}

bool StockfishEngine::askBestMove(const Game& game, int depth, int timeoutMs, std::string& move) {
  if (!running()) return false;
  setPosition(game);
  send("go depth " + std::to_string(depth));
  std::string line;
  if (!waitFor("bestmove", line, timeoutMs)) {
    // Too slow: tell it to stop and take what it has.
    send("stop");
    if (!waitFor("bestmove", line, 2000)) return false;
  }
  std::istringstream words(line);
  std::string word;
  words >> word >> move; // "bestmove" "e2e4", or "bestmove (none)" when the game is over
  if (!isUciMove(move)) move.clear();
  return true;
}

bool StockfishEngine::askLegalMoves(const Game& game, std::vector<std::string>& moves) {
  if (!running()) return false;
  setPosition(game);
  send("go perft 1");
  // The answer is one line per legal move, like "e2e4: 1", then "Nodes searched: 20".
  std::string line;
  while (readLine(line, 5000)) {
    if (line.rfind("Nodes searched", 0) == 0) return true; // the complete list arrived
    size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string move = line.substr(0, colon);
    if (isUciMove(move)) moves.push_back(move);
  }
  return false;
}

bool StockfishEngine::askInCheck(const Game& game, bool& check) {
  if (!running()) return false;
  setPosition(game);
  send("d"); // prints the position; the last line is "Checkers:" plus any pieces giving check
  std::string line;
  if (!waitFor("Checkers:", line, 5000)) return false;
  check = line.find_first_not_of(' ', 9) != std::string::npos;
  return true;
}

// Each public question is tried once. If the engine gives no answer (it crashed or hung),
// the engine is restarted and the question is asked one more time. ok() reports how that went.

std::string StockfishEngine::bestMove(const Game& game, int depth, int timeoutMs) {
  std::string move;
  ok_ = askBestMove(game, depth, timeoutMs, move) || (restart() && askBestMove(game, depth, timeoutMs, move));
  return ok_ ? move : "";
}

std::vector<std::string> StockfishEngine::legalMoves(const Game& game) {
  std::vector<std::string> moves;
  ok_ = askLegalMoves(game, moves);
  if (!ok_) {
    moves.clear(); // drop a half-received list
    ok_ = restart() && askLegalMoves(game, moves);
  }
  return ok_ ? moves : std::vector<std::string>();
}

bool StockfishEngine::inCheck(const Game& game) {
  bool check = false;
  ok_ = askInCheck(game, check) || (restart() && askInCheck(game, check));
  return ok_ && check;
}
