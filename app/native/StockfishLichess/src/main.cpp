/*
 * Project 64 - Stockfish and Lichess in C++.
 *
 *   project64 stockfish   play against the Stockfish engine
 *   project64 lichess     play a person online through Lichess
 *
 * This is a terminal program so the logic can be run and tested by itself. The classes it uses
 * (Game, StockfishEngine, LichessClient, LichessSession, HardwareBridge) do not depend on the
 * terminal, so the same code can sit behind an app or a web page later.
 */
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>

#include "EventQueue.h"
#include "Game.h"
#include "HardwareBridge.h"
#include "LichessSession.h"
#include "StockfishEngine.h"

namespace {

std::string env(const char* name, const std::string& fallback = "") {
  const char* value = std::getenv(name);
  return value && *value ? value : fallback;
}

// A move waiting to go to the physical board. It stays in the queue until the board accepts it.
struct BoardMove {
  std::string uci;
  std::string from;   // "Stockfish", the opponent's name, or "You"
  bool alreadyPlayed; // true for your own move: it is on screen already and only the board still needs it
};
using BoardQueue = std::deque<BoardMove>;

/**
 * Sends the waiting moves to the board in order. A Stockfish or opponent move is board first:
 * it is played on screen only after the board took it. Returns true once nothing is waiting.
 */
bool deliver(Game& game, HardwareBridge& bridge, BoardQueue& waiting, bool whiteAtBottom) {
  while (!waiting.empty()) {
    const BoardMove& move = waiting.front();
    bool sent = move.alreadyPlayed ? bridge.sendMove(move.uci) : game.playBoardFirst(bridge, move.uci);
    if (!sent) {
      std::cout << "Could not send " << move.uci << " to the board. Type: retry\n";
      return false;
    }
    std::cout << "[Board] " << bridge.name() << " board moved " << move.uci << "\n";
    if (!move.alreadyPlayed) {
      std::cout << "\n" << move.from << " played " << move.uci << "\n\n" << game.draw(whiteAtBottom) << "\n";
    }
    waiting.pop_front();
  }
  return true;
}

const char* const WAITING_FOR_BOARD = "A move is still waiting for the board. Type: retry\n";

/** Commands that work in both modes. Returns true if `line` was one of them. */
bool boardCommand(const std::string& line, MockBridge& bridge) {
  if (line == "unplug") {
    bridge.plugged = false;
    std::cout << "The mock board is now disconnected.\n";
  } else if (line == "plug") {
    bridge.plugged = true;
    std::cout << "The mock board is connected again. Type retry if a move is waiting.\n";
  } else {
    return false;
  }
  return true;
}

// --- playing against Stockfish ----------------------------------------------------------------

int playStockfish() {
  Game game;
  MockBridge bridge;
  StockfishEngine engine;
  if (!engine.start(env("STOCKFISH_CMD", "stockfish"))) {
    std::cout << "Could not start Stockfish. Install it (brew install stockfish) or set STOCKFISH_CMD.\n";
    return 1;
  }
  int level = 5, depth = 12;
  bool playerWhite = true, over = false;
  BoardQueue waiting;
  engine.setSkillLevel(level);

  std::cout << "You are white. Type a move like e2e4, or: new, black, white, level 0-20, board, unplug, plug, retry, quit\n\n"
            << game.draw(playerWhite) << "\n";

  // Says so and returns true if the side to move has no legal moves.
  const char* const ENGINE_DOWN = "Stockfish is not answering and could not be restarted. Check it, then type your move again.\n";

  auto checkGameOver = [&] {
    bool noMoves = engine.legalMoves(game).empty();
    if (!engine.ok()) return std::cout << ENGINE_DOWN, false; // no answer is not the same as no moves
    if (!noMoves) return false;
    bool mate = engine.inCheck(game);
    if (!engine.ok()) return std::cout << ENGINE_DOWN, false;
    std::cout << (mate ? std::string("Checkmate. ") + (game.whiteToMove() ? "Black" : "White") + " wins.\n"
                       : "Stalemate. The game is a draw.\n");
    return over = true;
  };

  // Stockfish thinks, then its move goes to the board first.
  auto engineTurn = [&] {
    if (over || game.whiteToMove() == playerWhite || !waiting.empty()) return;
    std::string move = engine.bestMove(game, depth);
    if (move.empty()) return void(std::cout << (engine.ok() ? "Stockfish has no move.\n" : ENGINE_DOWN));
    std::cout << "[Stockfish] bestmove " << move << "\n";
    waiting.push_back({move, "Stockfish", false});
    if (deliver(game, bridge, waiting, playerWhite)) checkGameOver();
  };

  std::string line;
  while (std::cout << "> " << std::flush, std::getline(std::cin, line)) {
    if (line == "quit") break;
    if (line == "board") {
      std::cout << game.draw(playerWhite) << "\n";
    } else if (boardCommand(line, bridge)) {
    } else if (line == "retry") {
      // Once the board has caught up, carry on: Stockfish replies if it is its turn.
      if (deliver(game, bridge, waiting, playerWhite) && !checkGameOver()) engineTurn();
    } else if (line == "new" || line == "white" || line == "black") {
      if (line != "new") playerWhite = line == "white";
      game.reset();
      waiting.clear();
      over = false;
      std::cout << "New game. You are " << (playerWhite ? "white" : "black") << ".\n\n" << game.draw(playerWhite) << "\n";
      engineTurn();
    } else if (line.rfind("level ", 0) == 0) {
      level = std::atoi(line.c_str() + 6);
      engine.setSkillLevel(level);
      std::cout << "Stockfish skill level set to " << level << ".\n";
    } else if (isUciMove(line)) {
      if (over) std::cout << "The game is over. Type: new\n";
      else if (!waiting.empty()) std::cout << WAITING_FOR_BOARD;
      else if (game.whiteToMove() != playerWhite) std::cout << "It is not your turn.\n";
      else {
        // Stockfish is the rule book: the move must be in its list of legal moves.
        auto legal = engine.legalMoves(game);
        if (!engine.ok()) {
          std::cout << ENGINE_DOWN;
          continue;
        }
        if (std::find(legal.begin(), legal.end(), line) == legal.end()) {
          std::cout << line << " is not a legal move.\n";
          continue;
        }
        game.play(line);
        std::cout << "\n" << game.draw(playerWhite) << "\n";
        // Your move is on screen; the board must take it too before the game goes on.
        waiting.push_back({line, "You", true});
        if (deliver(game, bridge, waiting, playerWhite) && !checkGameOver()) engineTurn();
      }
    } else if (!line.empty()) {
      std::cout << "Not a move or a command. Moves look like e2e4 (or e7e8q to promote).\n";
    }
  }
  return 0;
}

// --- playing online on Lichess ----------------------------------------------------------------

int playLichess() {
  // The token comes from the environment or is typed without being shown. It is never saved or printed.
  std::string token = env("LICHESS_TOKEN");
  if (token.empty()) {
    const char* typed = getpass("Lichess token (it will not be shown): ");
    token = typed ? typed : "";
  }
  if (token.empty()) return std::cout << "No token given.\n", 1;
  std::string baseUrl = env("LICHESS_URL", "https://lichess.org");
  if (baseUrl != "https://lichess.org") std::cout << "Using a test server: " << baseUrl << "\n";

  EventQueue queue;
  Game game;
  MockBridge bridge;
  BoardQueue waiting;
  TimeControl tc;
  LichessSession* sessionPtr = nullptr;

  LichessSession::Callbacks callbacks;
  callbacks.say = [](const std::string& text) { std::cout << text << "\n"; };
  callbacks.onOpponentMove = [&](const std::string& uci) {
    // Lichess repeats the move list, so the same move can be announced twice while it waits here.
    for (const BoardMove& move : waiting) {
      if (!move.alreadyPlayed && move.uci == uci) return;
    }
    waiting.push_back({uci, sessionPtr->opponent(), false});
    deliver(game, bridge, waiting, sessionPtr->playingWhite());
  };
  callbacks.onRebuilt = [&] {
    waiting.clear();
    std::cout << "\n" << game.draw(sessionPtr->playingWhite()) << "\n";
  };

  LichessSession session(queue, game, callbacks);
  sessionPtr = &session;
  if (!session.signIn(token, baseUrl)) return 1;
  std::cout << "Commands: find, challenge <username>, accept <id>, decline <id>, cancel, time <minutes> <increment>,\n"
               "          a move like e2e4, resign, leave, board, unplug, plug, retry, quit\n";

  using Phase = LichessSession::Phase;
  auto handle = [&](const std::string& line) {
    std::istringstream words(line);
    std::string command, arg;
    words >> command >> arg;

    if (command == "quit") {
      queue.stop();
    } else if (command == "board") {
      std::cout << game.draw(session.playingWhite()) << "\n";
    } else if (boardCommand(line, bridge)) {
    } else if (command == "retry") {
      deliver(game, bridge, waiting, session.playingWhite());
    } else if (command == "time") {
      int increment = 0;
      words >> increment;
      tc = {std::atoi(arg.c_str()), increment};
      if (tc.minutes < 10) std::cout << "Note: Lichess only lets apps look for games of 10 minutes or longer.\n";
      std::cout << "Time control: " << tc.minutes << "+" << tc.increment << "\n";
    } else if (command == "find") {
      if (session.phase() != Phase::Lobby) return void(std::cout << "You can only do that from the lobby.\n");
      session.findOpponent(tc);
      std::cout << "Looking for an opponent (" << tc.minutes << "+" << tc.increment << "). Type cancel to stop.\n";
    } else if (command == "challenge") {
      if (session.phase() != Phase::Lobby || arg.empty()) return void(std::cout << "Use: challenge <username> (from the lobby)\n");
      session.challengeFriend(arg, tc);
      std::cout << "Waiting for " << arg << " to accept. Type cancel to stop.\n";
    } else if (command == "cancel") {
      session.cancelSearch();
      std::cout << "Cancelled.\n";
    } else if (command == "accept" || command == "decline") {
      if (arg.empty()) return void(std::cout << "Use: " << command << " <id> (the id is shown with the challenge)\n");
      session.answerChallenge(arg, command == "accept");
    } else if (command == "resign") {
      session.resign();
    } else if (command == "leave") {
      session.leaveGame();
      waiting.clear();
      std::cout << "Back in the lobby.\n";
    } else if (isUciMove(command)) {
      if (session.phase() != Phase::Playing) std::cout << "You are not in a game.\n";
      else if (!waiting.empty()) std::cout << WAITING_FOR_BOARD;
      else if (!session.myTurn()) std::cout << "It is not your turn.\n";
      else if (session.sendMove(command)) {
        // Lichess accepted it, so now it is played here and sent to the board.
        game.play(command);
        std::cout << "\n" << game.draw(session.playingWhite()) << "\n";
        waiting.push_back({command, "You", true});
        deliver(game, bridge, waiting, session.playingWhite());
      }
    } else if (!command.empty()) {
      std::cout << "Not a move or a command.\n";
    }
  };

  // The keyboard is read on its own thread; each line becomes a job for the main thread.
  std::thread keyboard([&] {
    std::string line;
    while (std::getline(std::cin, line)) {
      queue.post([&handle, line] { handle(line); });
      if (line == "quit") return;
    }
    queue.post([&queue] { queue.stop(); }); // input closed
  });

  queue.run();
  keyboard.detach(); // it may still be waiting for a key; the program is ending anyway
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  std::cout << std::unitbuf; // show every message straight away, even when output goes to a file
  std::string mode = argc > 1 ? argv[1] : "";
  if (mode == "stockfish") return playStockfish();
  if (mode == "lichess") {
    int code = playLichess();
    std::cout.flush();
    _exit(code); // leave now; the keyboard thread may still be blocked reading
  }
  std::cout << "Use: project64 stockfish   (play the engine)\n"
               "     project64 lichess     (play a person online)\n";
  return 1;
}
