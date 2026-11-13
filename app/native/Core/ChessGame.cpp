#include "ChessGame.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace project64 {
namespace {
bool validFenSyntax(const std::string& fen) {
    std::istringstream in(fen);
    std::string position, side, castling, ep, half, full, extra;
    if (!(in >> position >> side >> castling >> ep >> half >> full) || (in >> extra)) return false;
    int ranks = 1, width = 0, whiteKing = 0, blackKing = 0;
    int whitePieces = 0, blackPieces = 0, whitePawns = 0, blackPawns = 0;
    char squares[64] = {};
    for (char c : position) {
        if (c == '/') { if (width != 8 || ++ranks > 8) return false; width = 0; }
        else if (c >= '1' && c <= '8') width += c - '0';
        else if (std::string("pnbrqkPNBRQK").find(c) != std::string::npos) {
            if (width >= 8 || ((c == 'P' || c == 'p') && (ranks == 1 || ranks == 8))) return false;
            squares[(8 - ranks) * 8 + width] = c;
            ++width; whiteKing += c == 'K'; blackKing += c == 'k';
            if (c >= 'A' && c <= 'Z') ++whitePieces; else ++blackPieces;
            whitePawns += c == 'P'; blackPawns += c == 'p';
        } else return false;
        if (width > 8) return false;
    }
    if (ranks != 8 || width != 8 || whiteKing != 1 || blackKing != 1 || (side != "w" && side != "b")) return false;
    if (whitePieces > 16 || blackPieces > 16 || whitePawns > 8 || blackPawns > 8) return false;
    if (castling != "-") {
        std::string seen;
        for (char c : castling) {
            if (std::string("KQkq").find(c) == std::string::npos || seen.find(c) != std::string::npos) return false;
            seen += c;
        }
    }
    if ((castling.find('K') != std::string::npos && (squares[4] != 'K' || squares[7] != 'R')) ||
        (castling.find('Q') != std::string::npos && (squares[4] != 'K' || squares[0] != 'R')) ||
        (castling.find('k') != std::string::npos && (squares[60] != 'k' || squares[63] != 'r')) ||
        (castling.find('q') != std::string::npos && (squares[60] != 'k' || squares[56] != 'r'))) return false;
    if (ep != "-") {
        if (ep.size() != 2 || ep[0] < 'a' || ep[0] > 'h' ||
            (side == "w" ? ep[1] != '6' : ep[1] != '3')) return false;
        const int target = (ep[1] - '1') * 8 + ep[0] - 'a';
        const int pawn = target + (side == "w" ? -8 : 8);
        const int origin = target + (side == "w" ? 8 : -8);
        if (squares[target] || squares[origin] || squares[pawn] != (side == "w" ? 'p' : 'P')) return false;
    }
    auto number = [](const std::string& s, bool positive) {
        if (s.empty() || s.size() > 4 || !std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); })) return false;
        return !positive || std::stoi(s) > 0;
    };
    // Upstream stores the halfmove clock in uint8_t; reject values that would
    // otherwise silently wrap and change draw detection.
    return number(half, false) && std::stoi(half) <= 255 && number(full, true);
}
}
ChessGame::ChessGame() = default;
ChessGame::ChessGame(const std::string& fen) {
    if (!validFenSyntax(fen) || !board_.setFen(fen)) throw std::invalid_argument("Invalid FEN");
    // The side that just moved cannot have left its king attacked.
    const auto previous = ~board_.sideToMove();
    if (board_.isAttacked(board_.kingSq(previous), board_.sideToMove())) throw std::invalid_argument("Invalid FEN");
}
std::string ChessGame::fen() const { return board_.getFen(); }
std::vector<std::string> ChessGame::legalMoves() const {
    chess::Movelist moves; chess::movegen::legalmoves(moves, board_);
    std::vector<std::string> result;
    for (const auto& move : moves) result.push_back(chess::uci::moveToUci(move));
    return result;
}
bool ChessGame::apply(const std::string& uci) {
    chess::Movelist moves; chess::movegen::legalmoves(moves, board_);
    for (const auto& move : moves) {
        if (chess::uci::moveToUci(move) == uci) {
            AppliedMove details;
            details.uci = uci; details.from = uci.substr(0, 2); details.to = uci.substr(2, 2);
            details.san = chess::uci::moveToSan(board_, move);
            details.by = board_.sideToMove() == chess::Color::WHITE ? "white" : "black";
            details.piece = static_cast<std::string>(board_.at(move.from()).type());
            details.isEnPassant = move.typeOf() == chess::Move::ENPASSANT;
            details.isCastle = move.typeOf() == chess::Move::CASTLING;
            details.isPromotion = move.typeOf() == chess::Move::PROMOTION;
            details.isCapture = board_.isCapture(move);
            if (details.isPromotion) details.promotion = uci.substr(4, 1);
            if (details.isCapture) {
                details.captured = static_cast<std::string>(board_.getCapturing<chess::PieceType>(move));
                details.capturedSquare = details.to;
                if (details.isEnPassant) details.capturedSquare[1] = details.from[1];
            }
            if (details.isCastle) {
                details.rookFrom = static_cast<std::string>(move.to());
                details.rookTo = std::string(1, details.to[0] == 'g' ? 'f' : 'd') + details.to[1];
            }
            board_.makeMove(move); details.fenAfter = board_.getFen();
            history_.push_back(uci); lastMove_ = std::move(details); return true;
        }
    }
    return false;
}
GameStatus ChessGame::status() const {
    GameStatus status; status.inCheck = board_.inCheck();
    const auto outcome = board_.isGameOver();
    using R = chess::GameResultReason;
    status.over = outcome.second != chess::GameResult::NONE;
    switch (outcome.first) {
        case R::CHECKMATE: status.result = "checkmate"; status.winner = board_.sideToMove() == chess::Color::WHITE ? "black" : "white"; break;
        case R::STALEMATE: status.result = "stalemate"; break;
        case R::INSUFFICIENT_MATERIAL: status.result = "draw"; status.drawReason = "insufficient material"; break;
        case R::FIFTY_MOVE_RULE: status.result = "draw"; status.drawReason = "fifty-move rule"; break;
        case R::THREEFOLD_REPETITION: status.result = "draw"; status.drawReason = "threefold repetition"; break;
        default: break;
    }
    return status;
}
const std::optional<AppliedMove>& ChessGame::lastMove() const noexcept { return lastMove_; }
const std::vector<std::string>& ChessGame::history() const noexcept { return history_; }
std::uint64_t ChessGame::occupancy() const noexcept { return board_.occ().getBits(); }
std::vector<std::string> ChessGame::inferMoves(std::uint64_t occupancy) const {
    chess::Movelist moves; chess::movegen::legalmoves(moves, board_);
    std::vector<std::string> result;
    for (const auto& move : moves) {
        auto next = board_; next.makeMove(move);
        if (next.occ().getBits() == occupancy) result.push_back(chess::uci::moveToUci(move));
    }
    return result;
}
}
