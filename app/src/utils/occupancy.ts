import { Chess } from 'chess.js';

export const UCI_MOVE_RE = /^[a-h][1-8][a-h][1-8][qrbn]?$/;
export const BITMASK_HEX_RE = /^[0-9a-f]{16}$/;

/** Occupancy of a FEN as 16 hex chars: byte 0 = rank 1 ... byte 7 = rank 8, bit n = file a+n. */
export function occupancyHex(fen: string): string {
  const ranks = fen.split(' ')[0].split('/'); // rank 8 first
  let hex = '';
  for (let rank = 1; rank <= 8; rank++) {
    let byte = 0;
    let file = 0;
    for (const ch of ranks[8 - rank]) {
      if (ch >= '1' && ch <= '8') file += Number(ch);
      else byte |= 1 << file++;
    }
    hex += byte.toString(16).padStart(2, '0');
  }
  return hex;
}

/** Bytes from the sensor matrix -> hex string in the same layout as occupancyHex. */
export function bytesToHex(bytes: ArrayLike<number>): string {
  return Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('');
}

/**
 * Finds the legal move whose resulting occupancy equals the sensor bitmask.
 * Returns null while the board is mid-move (piece lifted) or the position is unreachable.
 * Under-promotion can't be seen by Hall sensors, so promotions resolve to a queen.
 */
export function inferMoveFromOccupancy(fen: string, hexMask: string): string | null {
  const mask = hexMask.toLowerCase();
  const match = new Chess(fen)
    .moves({ verbose: true })
    .find((m) => (!m.promotion || m.promotion === 'q') && occupancyHex(m.after) === mask);
  return match ? match.lan : null;
}
