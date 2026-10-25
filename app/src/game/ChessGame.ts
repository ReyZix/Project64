import type { Move } from "chess.js";
import type { AppliedMove, PromotionPiece } from "./Types";

// Converts a completed chess.js move into the move details needed by the app.
export function toAppliedMove(m: Move): AppliedMove {
  const isEnPassant = m.isEnPassant();
  const isCastle = m.isKingsideCastle() || m.isQueensideCastle();
  const rank = m.from[1];

  let rookFrom: string | undefined;
  let rookTo: string | undefined;

  // Include the rook movement when castling.
  if (isCastle) {
    if (m.isKingsideCastle()) {
      rookFrom = `h${rank}`;
      rookTo = `f${rank}`;
    } else {
      rookFrom = `a${rank}`;
      rookTo = `d${rank}`;
    }
  }

  // En passant must also be treated as a capture.
  const isCapture = m.isCapture() || isEnPassant;
  const promotion = m.promotion as PromotionPiece | undefined;

  return {
    from: m.from,
    to: m.to,
    promotion,
    uci: `${m.from}${m.to}${promotion ?? ""}`,
    san: m.san,
    by: m.color === "w" ? "white" : "black",
    piece: m.piece,
    isCapture,
    captured: m.captured,
    capturedSquare: isCapture
      ? (isEnPassant ? `${m.to[0]}${m.from[1]}` : m.to)
      : undefined,
    isEnPassant,
    isCastle,
    rookFrom,
    rookTo,
    isPromotion: promotion !== undefined,
    fenAfter: m.after,
  };
}