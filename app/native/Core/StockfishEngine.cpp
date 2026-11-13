#include "StockfishEngine.hpp"
#include "ChessGame.hpp"

#include "../Vendor/stockfish/bitboard.h"
#include "../Vendor/stockfish/engine.h"
#include "../Vendor/stockfish/misc.h"
#include "../Vendor/stockfish/movegen.h"
#include "../Vendor/stockfish/uci.h"

#include <algorithm>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <sstream>

namespace project64 {
namespace {
std::once_flag initialization;
// Stockfish has process-global tablebase state, including in search_clear().
std::mutex engineMutex;
void initialize() {
    std::call_once(initialization, [] { Stockfish::Bitboards::init(); Stockfish::Position::init(); });
}
}

struct StockfishEngine::Impl {
    std::mutex serial;
    std::mutex state;
    std::unique_ptr<Stockfish::Engine> engine;
    std::string directory;
    unsigned long long generation = 0;
    bool running = false;
    bool clear = false;
    explicit Impl(std::string path) : directory(std::move(path)) { initialize(); }
};

StockfishEngine::StockfishEngine(const std::string& path) : impl_(new Impl(path)) {}
StockfishEngine::~StockfishEngine() {
    std::lock_guard<std::mutex> lock(engineMutex);
    impl_->engine.reset();
}

StockfishResult StockfishEngine::search(const std::string& fen, const std::vector<std::string>& moves,
                                        int depth, int movetimeMs, int skill) {
    unsigned long long generation;
    { std::lock_guard<std::mutex> lock(impl_->state); generation = impl_->generation; }
    std::lock_guard<std::mutex> serial(impl_->serial);
    std::lock_guard<std::mutex> global(engineMutex);
    { std::lock_guard<std::mutex> lock(impl_->state);
      if (generation != impl_->generation) return {{}, true, {}}; }
    if (depth < 0 || movetimeMs < 0 || (depth == 0 && movetimeMs == 0) || skill < 0 || skill > 20)
        return {{}, false, "Invalid search limits or skill level"};
    try {
        ChessGame position(fen);
        for (const auto& move : moves)
            if (!position.apply(move)) return {{}, false, "Illegal move in history: " + move};
        if (!impl_->engine) {
            impl_->engine = std::make_unique<Stockfish::Engine>(impl_->directory + "/stockfish");
            impl_->engine->verify_networks();
        }
        auto& engine = *impl_->engine;
        bool clear;
        { std::lock_guard<std::mutex> lock(impl_->state); clear = impl_->clear; impl_->clear = false; }
        if (clear) engine.search_clear();
        std::istringstream skillOption("name Skill Level value " + std::to_string(skill));
        engine.get_options().setoption(skillOption);
        engine.set_position(fen, moves);
        std::string best;
        engine.set_on_update_no_moves([](const Stockfish::Engine::InfoShort&) {});
        engine.set_on_update_full([](const Stockfish::Engine::InfoFull&) {});
        engine.set_on_iter([](const Stockfish::Engine::InfoIter&) {});
        engine.set_on_bestmove([&best](std::string_view move, std::string_view) { best.assign(move); });
        Stockfish::Search::LimitsType limits;
        limits.startTime = Stockfish::now();
        limits.depth = depth;
        limits.movetime = movetimeMs;
        {
            std::lock_guard<std::mutex> lock(impl_->state);
            if (generation != impl_->generation) return {{}, true, {}};
            impl_->running = true;
            engine.go(limits);
        }
        engine.wait_for_search_finished();
        {
            std::lock_guard<std::mutex> lock(impl_->state);
            impl_->running = false;
            if (generation != impl_->generation) return {{}, true, {}};
        }
        if (best.empty() || best == "(none)" || best == "0000") return {{}, false, "No legal move"};
        if (!position.apply(best)) return {{}, false, "Engine returned an illegal move"};
        return {best, false, {}};
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(impl_->state);
        impl_->running = false;
        impl_->engine.reset();
        return {{}, false, e.what()};
    }
}
void StockfishEngine::cancel() {
    std::lock_guard<std::mutex> lock(impl_->state);
    ++impl_->generation;
    if (impl_->running) impl_->engine->stop();
}
void StockfishEngine::newGame() {
    std::lock_guard<std::mutex> lock(impl_->state);
    ++impl_->generation;
    impl_->clear = true;
    if (impl_->running) impl_->engine->stop();
}
}
