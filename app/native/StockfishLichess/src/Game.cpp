#include "Game.h"

#include <cctype>
#include <cstdlib>
#include <regex>

const char* const Game::START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

bool isUciMove(const std::string& text) {
  static const std::regex pattern("^[a-h][1-8][a-h][1-8][qrbn]?$");
  return std::regex_match(text, pattern);
}

bool Game::reset(const std::string& fen, const std::vector<std::string>& moves) {
  for (char& c : squares_) c = ' ';
  moves_.clear();
  startFen_ = fen;

  // The first part of a FEN lists the pieces from rank 8 down to rank 1.
  int file = 0, rank = 7;
  size_t i = 0;
  for (; i < fen.size() && fen[i] != ' '; i++) {
    char c = fen[i];
    if (c == '/') {
      file = 0;
      rank--;
    } else if (std::isdigit(static_cast<unsigned char>(c))) {
      file += c - '0';
    } else {
      if (file > 7 || rank < 0) return false;
      squares_[rank * 8 + file++] = c;
    }
  }
  whiteToMove_ = !(i + 1 < fen.size() && fen[i + 1] == 'b');

  for (const std::string& move : moves) play(move);
  return rank == 0;
}

void Game::play(const std::string& uci) {
  if (!isUciMove(uci)) return;
  int fromFile = uci[0] - 'a', fromRank = uci[1] - '1';
  int toFile = uci[2] - 'a', toRank = uci[3] - '1';
  char piece = squares_[fromRank * 8 + fromFile];
  bool white = std::isupper(static_cast<unsigned char>(piece));
  char kind = static_cast<char>(std::tolower(static_cast<unsigned char>(piece)));

  // En passant: a pawn moves diagonally onto an empty square, so the pawn it takes is beside it.
  if (kind == 'p' && fromFile != toFile && squares_[toRank * 8 + toFile] == ' ') {
    squares_[fromRank * 8 + toFile] = ' ';
  }
  // Castling: the king moves two squares, so the rook jumps to the other side of it.
  if (kind == 'k' && std::abs(toFile - fromFile) == 2) {
    int rookFrom = toFile > fromFile ? 7 : 0;
    int rookTo = toFile > fromFile ? 5 : 3;
    squares_[fromRank * 8 + rookTo] = squares_[fromRank * 8 + rookFrom];
    squares_[fromRank * 8 + rookFrom] = ' ';
  }
  // Promotion: the fifth letter says which piece the pawn becomes.
  if (uci.size() == 5) {
    piece = white ? static_cast<char>(std::toupper(static_cast<unsigned char>(uci[4]))) : uci[4];
  }

  squares_[fromRank * 8 + fromFile] = ' ';
  squares_[toRank * 8 + toFile] = piece;
  whiteToMove_ = !whiteToMove_;
  moves_.push_back(uci);
}

bool Game::playBoardFirst(HardwareBridge& bridge, const std::string& uci) {
  if (!bridge.sendMove(uci)) return false;
  play(uci);
  return true;
}

std::string Game::normalizeCastle(const std::string& uci) const {
  if (!isUciMove(uci) || uci.size() != 4) return uci;
  int fromFile = uci[0] - 'a', fromRank = uci[1] - '1';
  int toFile = uci[2] - 'a', toRank = uci[3] - '1';
  char king = at(fromFile, fromRank);
  char rook = at(toFile, toRank);
  bool kingOnHome = fromFile == 4 && fromRank == toRank && (fromRank == 0 || fromRank == 7);
  bool ownRook = (king == 'K' && rook == 'R') || (king == 'k' && rook == 'r');
  if (!kingOnHome || !ownRook || (toFile != 0 && toFile != 7)) return uci;
  std::string fixed = uci;
  fixed[2] = toFile == 7 ? 'g' : 'c';
  return fixed;
}

std::string Game::draw(bool whiteAtBottom) const {
  std::string out;
  for (int row = 0; row < 8; row++) {
    int rank = whiteAtBottom ? 7 - row : row;
    out += static_cast<char>('1' + rank);
    out += "  ";
    for (int col = 0; col < 8; col++) {
      int file = whiteAtBottom ? col : 7 - col;
      char piece = at(file, rank);
      out += piece == ' ' ? '.' : piece;
      out += ' ';
    }
    out += '\n';
  }
  out += whiteAtBottom ? "\n   a b c d e f g h\n" : "\n   h g f e d c b a\n";
  return out;
}
