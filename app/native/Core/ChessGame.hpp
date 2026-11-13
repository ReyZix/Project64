#pragma once

#include "../Vendor/chess-library/chess.hpp"
#include <cstdint>
#include <string>
#include <optional>
#include <vector>

namespace project64 {
struct GameStatus {
    bool over = false;
    bool inCheck = false;
    std::string result = "ongoing";
    std::string winner;
    std::string drawReason;
};

struct AppliedMove {
    std::string from, to, promotion, uci, san, by, piece;
    bool isCapture = false, isEnPassant = false, isCastle = false, isPromotion = false;
    std::string captured, capturedSquare, rookFrom, rookTo, fenAfter;
};

class ChessGame {
public:
    ChessGame();
    explicit ChessGame(const std::string& fen);
    std::string fen() const;
    std::vector<std::string> legalMoves() const;
    bool apply(const std::string& uci);
    GameStatus status() const;
    const std::optional<AppliedMove>& lastMove() const noexcept;
    const std::vector<std::string>& history() const noexcept;
    // Bit 0 is a1, bit 7 is h1, and bit 63 is h8.
    std::uint64_t occupancy() const noexcept;
    // Return every legal move producing this occupancy. Promotions and captures
    // may be ambiguous; callers must not assume a single candidate.
    std::vector<std::string> inferMoves(std::uint64_t occupancy) const;
private:
    chess::Board board_;
    std::vector<std::string> history_;
    std::optional<AppliedMove> lastMove_;
};
}
