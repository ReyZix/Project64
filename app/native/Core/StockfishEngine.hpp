#pragma once

#include <memory>
#include <string>
#include <vector>

namespace project64 {

struct StockfishResult {
    std::string bestMove;
    bool cancelled = false;
    std::string error;
};

// search() blocks and must run off the UI thread. Concurrent searches are
// serialized across all instances because Stockfish uses process-global state.
// cancel() and newGame() may be called from any other thread;
// both invalidate searches that have already started or are waiting to start.
// The object must outlive all calls to its methods.
class StockfishEngine {
public:
    explicit StockfishEngine(const std::string& networkDirectory);
    ~StockfishEngine();
    StockfishEngine(const StockfishEngine&) = delete;
    StockfishEngine& operator=(const StockfishEngine&) = delete;

    StockfishResult search(const std::string& initialFen,
                           const std::vector<std::string>& moves,
                           int depth, int movetimeMs, int skill);
    void cancel();
    void newGame();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
