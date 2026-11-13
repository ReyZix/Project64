#include "../Core/ChessGame.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>
using project64::ChessGame;
static bool contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
static long perft(const ChessGame& game, int depth) {
    if (!depth) return 1;
    long n = 0; for (const auto& move : game.legalMoves()) { auto next = game; assert(next.apply(move)); n += perft(next, depth - 1); } return n;
}
int main() {
    ChessGame game;
    assert(game.legalMoves().size() == 20 && perft(game, 3) == 8902);
    assert(game.occupancy() == 0xffff00000000ffffULL);
    auto before = game.fen(); assert(!game.apply("e2e5") && !game.apply("garbage") && game.fen() == before && game.history().empty());
    assert(game.apply("e2e4")); assert(game.history().back() == "e2e4");
    assert(game.lastMove()->san == "e4" && game.lastMove()->piece == "p" && game.lastMove()->by == "white" && game.lastMove()->fenAfter == game.fen());
    assert(!game.apply("e7e4") && game.lastMove()->uci == "e2e4");
    ChessGame start; assert(contains(start.inferMoves(game.occupancy()), "e2e4"));
    ChessGame castle("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    assert(castle.apply("e1g1")); assert(castle.lastMove()->isCastle && castle.lastMove()->rookFrom == "h1" && castle.lastMove()->rookTo == "f1"); assert((castle.occupancy() & (1ULL << 5)) && (castle.occupancy() & (1ULL << 6)));
    ChessGame ep("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    assert(ep.apply("e5d6")); assert(ep.lastMove()->isEnPassant && ep.lastMove()->captured == "p" && ep.lastMove()->capturedSquare == "d5"); assert(!(ep.occupancy() & (1ULL << 35)));
    ChessGame promotion("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    auto promoted = promotion; assert(promoted.apply("a7a8q")); assert(promoted.lastMove()->isPromotion && promoted.lastMove()->promotion == "q"); assert(promotion.inferMoves(promoted.occupancy()).size() == 4);
    ChessGame mate; for (auto m : {"f2f3", "e7e5", "g2g4", "d8h4"}) assert(mate.apply(m));
    assert(mate.status().result == "checkmate" && mate.status().winner == "black" && mate.status().inCheck);
    ChessGame stale("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"); assert(stale.status().result == "stalemate");
    ChessGame material("4k3/8/8/8/8/8/8/4K3 w - - 0 1"); assert(material.status().drawReason == "insufficient material");
    ChessGame fifty("4k2r/8/8/8/8/8/8/R3K3 w - - 100 51"); assert(fifty.status().drawReason == "fifty-move rule");
    for (auto fen : {"4k2r/8/8/8/8/8/8/R3K3 w - - 256 51", "rnbqkbnr/pppppppp/8/8/8/QQQQQQQQ/PPPPPPPP/RNBQKBNR w KQkq - 0 1"}) {
        bool rejected = false;
        try { ChessGame invalid(fen); } catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected);
    }
    ChessGame repetition; for (auto m : {"g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1", "f6g8"}) assert(repetition.apply(m)); assert(repetition.status().drawReason == "threefold repetition");
    for (auto f : {"bad", "8/8/8/8/8/8/8/8 w - - 0 1", "8/8/8/8/8/8/8/4K2k w - - x 1", "4k3/8/8/8/8/8/8/4K3 w K - 0 1", "8/8/8/8/8/8/4k3/4K3 w - - 0 1", "4k3/8/8/8/8/8/4R3/4K3 w - - 0 1", "P3k3/8/8/8/8/8/8/4K3 w - - 0 1", "4k3/8/8/8/8/8/8/4K3 w - d3 0 1", "4k3/8/8/8/8/8/8/4K3 w - d6 0 1", "4k3/8/8/8/8/8/8/4K3 w - - 9999999999999 1"}) { bool threw = false; try { ChessGame bad(f); } catch (const std::invalid_argument&) { threw = true; } assert(threw); }
    std::cout << "ChessGame tests passed\n";
}
