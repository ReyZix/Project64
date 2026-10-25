import type { StockfishService } from '../services/StockfishService';
import type { ChessMove, ComputerMove, PromotionPiece } from './Types';

// Sends the current position to Stockfish and parses the returned best move.
export async function requestComputerMove(
  engine: Pick<StockfishService, 'getBestMove'>,
  fen: string,
  depth: number,
): Promise<ComputerMove> {
  const { uci, source } = await engine.getBestMove(fen, depth);
  const move = parseUciMove(uci);

  if (!move) throw new Error(`Engine reply is not a UCI move: ${uci}`);

  return { ...move, uci, source };
}

// Converts a UCI move such as "e7e5" or "e7e8q" into a move object.
export function parseUciMove(uci: string): ChessMove | null {
  const m = /^([a-h][1-8])([a-h][1-8])([qrbn])?$/.exec(uci.trim());

  if (!m) return null;

  return m[3]
    ? { from: m[1], to: m[2], promotion: m[3] as PromotionPiece }
    : { from: m[1], to: m[2] };
}