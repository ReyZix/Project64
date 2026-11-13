#include "StockfishEngine.hpp"
#include "ChessGame.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    assert(argc == 2);
    project64::StockfishEngine engine(argv[1]);
    project64::ChessGame game;
    auto result = engine.search(game.fen(), {"e2e4", "e7e5"}, 4, 0, 20);
    assert(result.error.empty() && !result.cancelled);
    assert(game.apply("e2e4") && game.apply("e7e5"));
    auto legal = game.legalMoves();
    assert(std::find(legal.begin(), legal.end(), result.bestMove) != legal.end());
    assert(!engine.search(game.fen(), {"e2e5"}, 4, 0, 20).error.empty());
    assert(!engine.search(game.fen(), {}, 0, 0, 20).error.empty());
    auto mate = engine.search("7k/6Q1/6K1/8/8/8/8/8 b - - 0 1", {}, 2, 0, 20);
    assert(mate.bestMove.empty() && !mate.error.empty());
    auto timed = engine.search(game.fen(), {}, 0, 30, 10);
    assert(!timed.bestMove.empty() && timed.error.empty());
    auto future = std::async(std::launch::async, [&] { return engine.search(game.fen(), {}, 40, 0, 20); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.newGame();
    assert(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    assert(future.get().cancelled);
    auto cancelled = std::async(std::launch::async, [&] { return engine.search(game.fen(), {}, 40, 0, 20); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.cancel();
    assert(cancelled.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    assert(cancelled.get().cancelled);
    assert(!engine.search("invalid fen", {}, 2, 0, 20).error.empty());
    auto next = engine.search(game.fen(), {}, 3, 0, 20);
    assert(!next.cancelled && next.error.empty() && !next.bestMove.empty());
    // Hold the process-wide engine lock with one search, then invalidate a
    // second instance's queued request before it can enter Stockfish.
    project64::StockfishEngine queuedEngine(argv[1]);
    std::promise<void> entering;
    auto blocker = std::async(std::launch::async, [&] { return engine.search(game.fen(), {}, 40, 0, 20); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto queued = std::async(std::launch::async, [&] {
        entering.set_value();
        return queuedEngine.search(game.fen(), {}, 4, 0, 20);
    });
    entering.get_future().wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    queuedEngine.newGame();
    engine.cancel();
    assert(blocker.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    assert(queued.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    assert(blocker.get().cancelled && queued.get().cancelled);
    assert(!queuedEngine.search(game.fen(), {}, 3, 0, 20).bestMove.empty());
    project64::StockfishEngine missing("/path/that/does/not/exist");
    assert(!missing.search(game.fen(), {}, 2, 0, 20).error.empty());
    std::cout << "Stockfish tests passed\n";
}
