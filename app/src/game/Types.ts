export type PromotionPiece = "q" | "r" | "b" | "n";
export type PieceSymbol = "p" | "n" | "b" | "r" | "q" | "k";
export type Side = "white" | "black";

export interface ChessMove {
  from: string;
  to: string;
  promotion?: PromotionPiece;
}

export interface AppliedMove {
  from: string;
  to: string;
  promotion?: PromotionPiece;

  uci: string;
  san: string;
  by: Side;
  piece: PieceSymbol;

  isCapture: boolean;
  captured?: PieceSymbol;
  // Can differ from `to` for en passant.
  capturedSquare?: string;

  isEnPassant: boolean;

  isCastle: boolean;
  rookFrom?: string;
  rookTo?: string;

  isPromotion: boolean;

  fenAfter: string;
}

// Identifies which engine produced the computer move.
export type EngineSource = "stockfish" | "rest" | "fallback";

export interface ComputerMove extends ChessMove {
  uci: string;
  source: EngineSource;
}

export type GameResult = "ongoing" | "checkmate" | "stalemate" | "draw";

export interface GameStatus {
  over: boolean;
  result: GameResult;
  winner?: Side;
  drawReason?:
    | "threefold repetition"
    | "insufficient material"
    | "fifty-move rule"
    | "other";
  inCheck: boolean;
}