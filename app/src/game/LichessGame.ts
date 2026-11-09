import { Chess } from 'chess.js';

/*
 * Keeps the app's chess.js game in step with a Lichess game.
 * Lichess's move list is the source of truth: the app only adds moves to its own game,
 * and rebuilds the game from Lichess's list when the two disagree.
 */

export type LichessColor = 'white' | 'black';

export type SyncPlan =
  | { kind: 'none' } // already identical
  | { kind: 'apply'; uci: string } // exactly one new move: play it like a normal move
  | { kind: 'wait' } // our own move is sent but Lichess hasn't confirmed it yet
  | { kind: 'rebuild'; moves: string[] }; // anything else: reload the whole game

const STANDARD_START = new Chess().fen();

export function startFen(initialFen: string | undefined) {
  return !initialFen || initialFen === 'startpos' ? STANDARD_START : initialFen;
}

/**
 * Lichess may send castling as king-takes-rook (e1h1) for Chess960 compatibility.
 * chess.js only understands the king's real destination (e1g1), so convert it.
 */
export function normalizeCastling(chess: Chess, uci: string) {
  const from = uci.slice(0, 2) as Parameters<Chess['get']>[0];
  const to = uci.slice(2, 4) as Parameters<Chess['get']>[0];
  const king = chess.get(from);
  const rook = chess.get(to);
  if (king?.type !== 'k' || rook?.type !== 'r' || rook.color !== king.color) return uci;
  return `${from}${to[0] > from[0] ? 'g' : 'c'}${from[1]}`;
}

/** Turns Lichess's "e2e4 e7e5 e1h1" into chess.js moves; throws if any move is illegal. */
export function lichessMovesToLan(initialFen: string | undefined, moves: string) {
  const chess = new Chess(startFen(initialFen));
  return (moves.trim() ? moves.trim().split(/\s+/) : []).map((uci) => {
    const fixed = normalizeCastling(chess, uci);
    return chess.move({ from: fixed.slice(0, 2), to: fixed.slice(2, 4), promotion: fixed[4] }).lan;
  });
}

/** Decides how to bring `local` (moves the app has played) in line with `remote` (Lichess's moves). */
export function planSync(local: string[], remote: string[]): SyncPlan {
  const common = commonPrefix(local, remote);
  if (common === local.length && common === remote.length) return { kind: 'none' };
  if (common === local.length && remote.length === local.length + 1) {
    return { kind: 'apply', uci: remote[remote.length - 1] };
  }
  if (common === remote.length && local.length === remote.length + 1) return { kind: 'wait' };
  return { kind: 'rebuild', moves: remote };
}

/** Resets `chess` to the Lichess start position and replays `moves`, keeping a normal move history. */
export function rebuildGame(chess: Chess, initialFen: string | undefined, moves: string[]) {
  chess.load(startFen(initialFen));
  for (const uci of moves) chess.move({ from: uci.slice(0, 2), to: uci.slice(2, 4), promotion: uci[4] });
}

export function localMoves(chess: Chess) {
  return chess.history({ verbose: true }).map((m) => m.lan);
}

const RESULT_TEXT: Record<string, string> = {
  mate: 'Checkmate',
  resign: 'Resignation',
  stalemate: 'Stalemate',
  timeout: 'Opponent left the game',
  draw: 'Draw',
  outoftime: 'Time out',
  aborted: 'Game aborted',
  noStart: 'Game not started in time',
  cheat: 'Game ended by Lichess',
  insufficientMaterialClaim: 'Draw (insufficient material)',
  variantEnd: 'Game over',
  unknownFinish: 'Game over',
};

/** True while the Lichess game is still being played. */
export function isLive(status: string) {
  return status === 'created' || status === 'started';
}

/** "You won · Checkmate", "Draw", "Game aborted"... from the player's point of view. */
export function resultText(status: string, winner: LichessColor | undefined, myColor: LichessColor) {
  const why = RESULT_TEXT[status] ?? 'Game over';
  if (!winner) return why;
  return `${winner === myColor ? 'You won' : 'You lost'} · ${why}`;
}

function commonPrefix(a: string[], b: string[]) {
  let i = 0;
  while (i < a.length && i < b.length && a[i] === b[i]) i++;
  return i;
}
