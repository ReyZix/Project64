#pragma once
#include <string>
#include <vector>

#include "HardwareBridge.h"

/*
 * The one copy of the game: where every piece is and the list of moves played.
 *
 * This class does not decide whether a move is legal. Stockfish does that when playing the
 * computer (StockfishEngine::legalMoves) and Lichess does it when playing online.
 * It only moves the pieces, including the special cases: castling, en passant and promotion.
 */
class Game {
 public:
  static const char* const START_FEN;

  Game() { reset(); }

  /** Starts again from `fen` (the normal starting position by default) and replays `moves`. */
  bool reset(const std::string& fen = START_FEN, const std::vector<std::string>& moves = {});

  /** Plays one move on the screen copy of the game. */
  void play(const std::string& uci);

  /**
   * The board-first rule: send the move to the physical board, and only if that worked
   * play it here. Used for every Stockfish move and every Lichess opponent move.
   */
  bool playBoardFirst(HardwareBridge& bridge, const std::string& uci);

  /**
   * Lichess can send castling as "king takes rook" (e1h1). The board and Stockfish expect
   * the usual form (e1g1), so this converts it. Other moves come back unchanged.
   */
  std::string normalizeCastle(const std::string& uci) const;

  bool whiteToMove() const { return whiteToMove_; }
  const std::vector<std::string>& moves() const { return moves_; }
  const std::string& startFen() const { return startFen_; }
  bool fromStartPosition() const { return startFen_ == START_FEN; }

  /** Piece letter on a square ('P' white pawn, 'p' black pawn, ' ' empty). file and rank are 0-7. */
  char at(int file, int rank) const { return squares_[rank * 8 + file]; }

  /** Text drawing of the board. */
  std::string draw(bool whiteAtBottom = true) const;

 private:
  char squares_[64];
  bool whiteToMove_ = true;
  std::string startFen_;
  std::vector<std::string> moves_;
};
