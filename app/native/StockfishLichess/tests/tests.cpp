// Small tests with no test library: each check prints PASS or FAIL.
// Stockfish checks run only if STOCKFISH_CMD is set (or "stockfish" is installed).
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <unistd.h>

#include "../src/Game.h"
#include "../src/HardwareBridge.h"
#include "../src/Json.h"
#include "../src/LichessSession.h"
#include "../src/StockfishEngine.h"

static int failed = 0, passed = 0;
static void check(bool ok, const std::string& what) {
  (ok ? passed : failed)++;
  std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n";
}

int main() {
  // --- JSON ---
  Json j;
  check(Json::parse(R"({"type":"gameState","moves":"e2e4 e7e5","wtime":600000,"x":[1,true,null],"n":{"a":"b\né"}})", j),
        "JSON: parses a Lichess-style line");
  check(j["type"].str() == "gameState" && j["moves"].str() == "e2e4 e7e5", "JSON: reads strings");
  check(j["wtime"].num() == 600000 && j["x"].items.size() == 3, "JSON: reads numbers and arrays");
  check(j["n"]["a"].str() == "b\n\xC3\xA9", "JSON: reads escapes and nested objects");
  check(j["missing"]["deeper"].str("none") == "none", "JSON: a missing field is safe to read");
  check(!Json::parse("{\"a\":", j) && !Json::parse("{} extra", j), "JSON: rejects broken text");

  // --- moves ---
  check(isUciMove("e2e4") && isUciMove("e7e8q") && !isUciMove("e2e9") && !isUciMove("hello"), "UCI move format");

  Game g;
  check(g.at(4, 0) == 'K' && g.at(4, 7) == 'k' && g.whiteToMove(), "Game: starting position");
  for (const char* m : {"e2e4", "d7d5", "e4e5", "f7f5", "e5f6"}) g.play(m);
  check(g.at(5, 5) == 'P' && g.at(5, 4) == ' ', "Game: en passant removes the pawn that was passed");

  g.reset();
  for (const char* m : {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "g8f6", "e1g1"}) g.play(m);
  check(g.at(6, 0) == 'K' && g.at(5, 0) == 'R' && g.at(7, 0) == ' ', "Game: castling moves the rook too");

  g.reset("8/P7/8/8/8/8/8/k6K w - - 0 1");
  g.play("a7a8q");
  check(g.at(0, 7) == 'Q' && !g.whiteToMove(), "Game: promotion");

  g.reset();
  for (const char* m : {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "g8f6"}) g.play(m);
  check(g.normalizeCastle("e1h1") == "e1g1" && g.normalizeCastle("d2d4") == "d2d4",
        "Game: Lichess king-takes-rook castling becomes e1g1");

  // --- the board-first rule ---
  g.reset();
  MockBridge bridge;
  bridge.plugged = false;
  check(!g.playBoardFirst(bridge, "e2e4") && g.moves().empty(), "Board first: board not connected, so the move is not played");
  bridge.plugged = true;
  check(g.playBoardFirst(bridge, "e2e4") && g.moves().size() == 1 && bridge.sent.size() == 1 && bridge.sent[0] == "e2e4",
        "Board first: board accepted, so the move is played");

  // --- comparing with Lichess ---
  check(planSync({"e2e4"}, {"e2e4"}).kind == SyncPlan::Nothing, "Sync: same moves, nothing to do");
  SyncPlan plan = planSync({"e2e4"}, {"e2e4", "e7e5"});
  check(plan.kind == SyncPlan::Apply && plan.uci == "e7e5", "Sync: one new move is applied");
  check(planSync({"e2e4"}, {"d2d4", "d7d5"}).kind == SyncPlan::Rebuild && planSync({}, {"e2e4", "e7e5"}).kind == SyncPlan::Rebuild,
        "Sync: anything else reloads the game");
  check(isLiveStatus("started") && !isLiveStatus("mate") && !isLiveStatus("resign"), "Game status");

  // --- Stockfish ---
  const char* cmd = std::getenv("STOCKFISH_CMD");
  StockfishEngine engine;
  if (!engine.start(cmd && *cmd ? cmd : "stockfish")) {
    std::cout << "SKIP  Stockfish checks (engine not found; set STOCKFISH_CMD)\n";
  } else {
    g.reset();
    check(engine.legalMoves(g).size() == 20, "Stockfish: 20 legal moves at the start");
    std::string best = engine.bestMove(g, 8);
    check(isUciMove(best), "Stockfish: gives a best move (" + best + ")");
    check(!engine.inCheck(g), "Stockfish: not in check at the start");
    for (const char* m : {"f2f3", "e7e5", "g2g4", "d8h4"}) g.play(m); // fool's mate
    check(engine.legalMoves(g).empty() && engine.inCheck(g), "Stockfish: sees checkmate");
    check(engine.bestMove(g, 8).empty(), "Stockfish: no move when the game is over");
    g.reset("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    check(engine.legalMoves(g).empty() && !engine.inCheck(g), "Stockfish: sees stalemate");
    check(engine.ok(), "Stockfish: an empty move list from a working engine still counts as an answer");

    // Kill the engine behind our back. The next question should restart it and still be answered.
    // The start command writes the engine's process id to a file so the test can find it.
    const char* pidFile = "run-tests.enginepid";
    std::string base = cmd && *cmd ? cmd : "stockfish";
    StockfishEngine crashing;
    if (crashing.start("echo $$ > " + std::string(pidFile) + "; exec " + base)) {
      crashing.setSkillLevel(3);
      g.reset();
      check(crashing.legalMoves(g).size() == 20, "Crash test: engine answers before the crash");
      int pid = 0;
      std::ifstream(pidFile) >> pid;
      if (pid > 0) kill(pid, SIGKILL);
      usleep(200 * 1000);
      check(crashing.legalMoves(g).size() == 20 && crashing.ok(), "Crash test: engine is restarted and answers again");
      std::ifstream(pidFile) >> pid;
      if (pid > 0) kill(pid, SIGKILL);
      usleep(200 * 1000);
      check(isUciMove(crashing.bestMove(g, 6)) && crashing.ok(), "Crash test: best move also survives a crash");
      std::remove(pidFile);
    }

    // An engine that cannot be restarted must report "no answer", not "no legal moves".
    StockfishEngine gone;
    gone.start("true"); // a command that exits at once
    g.reset();
    check(gone.legalMoves(g).empty() && !gone.ok(), "Crash test: no engine at all is reported as not ok");
  }

  std::cout << "\n" << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
