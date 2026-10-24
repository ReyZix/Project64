import { Chess, type Move } from 'chess.js';

/**
 * Line-based UCI pipe to a real engine: a native Stockfish module, a WASM worker in a
 * WebView, or a socket to a desktop engine. Anything that can send and receive UCI lines.
 */
export interface UciTransport {
  send(command: string): void;
  onLine(callback: (line: string) => void): () => void;
}

const UCI_RE = /\b[a-h][1-8][a-h][1-8][qrbn]?\b/;

/**
 * Engine abstraction used by the game loop. Order of preference:
 *   1. attached UCI engine (once it answered uciok/readyok)
 *   2. REST analysis endpoint, if configured (GET <url>?fen=&depth=, JSON with a `bestmove` field)
 *   3. built-in chess.js search, so play never blocks while the native engine initializes
 */
export class StockfishService {
  private transport: UciTransport | null = null;
  private ready = false;
  private restUrl: string | null;
  private skillLevel = 8;

  constructor(options: { restUrl?: string } = {}) {
    this.restUrl = options.restUrl ?? null;
  }

  async attachEngine(transport: UciTransport, timeoutMs = 10000): Promise<boolean> {
    this.transport = transport;
    this.ready = false;
    const ok = (await this.request('uci', (l) => l === 'uciok', timeoutMs)) !== null;
    this.ready = ok && (await this.request('isready', (l) => l === 'readyok', timeoutMs)) !== null;
    if (this.ready) this.applySkill();
    return this.ready;
  }

  setSkillLevel(level: number) {
    this.skillLevel = Math.max(0, Math.min(20, Math.round(level)));
    if (this.ready) this.applySkill();
  }

  private applySkill() {
    this.transport?.send(`setoption name Skill Level value ${this.skillLevel}`);
  }

  /** Sends a command and resolves with the first line accepted by `done`, or null on timeout. */
  private request(command: string, done: (line: string) => boolean, timeoutMs: number) {
    const transport = this.transport;
    if (!transport) return Promise.resolve(null);
    return new Promise<string | null>((resolve) => {
      const finish = (line: string | null) => {
        clearTimeout(timer);
        off();
        resolve(line);
      };
      const timer = setTimeout(() => finish(null), timeoutMs);
      const off = transport.onLine((line) => done(line.trim()) && finish(line.trim()));
      transport.send(command);
    });
  }

  /** Returns a UCI move ("e7e5", "e7e8q") for the side to move in `fen`. */
  async getBestMove(fen: string, depth: number): Promise<string> {
    if (this.ready) {
      this.transport!.send(`position fen ${fen}`);
      const line = await this.request(`go depth ${depth}`, (l) => l.startsWith('bestmove'), 30000);
      const move = line?.split(/\s+/)[1];
      if (move && UCI_RE.test(move)) return move;
      this.transport!.send('stop');
    }
    if (this.restUrl) {
      try {
        const res = await fetch(`${this.restUrl}?fen=${encodeURIComponent(fen)}&depth=${Math.min(depth, 15)}`);
        const move = String((await res.json()).bestmove ?? '').match(UCI_RE)?.[0];
        // The REST answer is untrusted: only use it if it is legal here.
        if (move && isLegal(fen, move)) return move;
      } catch {
        // fall through to the local search
      }
    }
    return fallbackBestMove(fen, depth);
  }
}

function isLegal(fen: string, uci: string) {
  try {
    new Chess(fen).move({ from: uci.slice(0, 2), to: uci.slice(2, 4), promotion: uci[4] });
    return true;
  } catch {
    return false;
  }
}

const VALUES: Record<string, number> = { p: 100, n: 320, b: 330, r: 500, q: 900, k: 0 };

function material(chess: Chess) {
  let score = 0;
  for (const row of chess.board()) {
    for (const sq of row) if (sq) score += sq.color === 'w' ? VALUES[sq.type] : -VALUES[sq.type];
  }
  return chess.turn() === 'w' ? score : -score;
}

function negamax(chess: Chess, depth: number, alpha: number, beta: number): number {
  if (chess.isCheckmate()) return -100000 - depth; // prefer faster mates
  if (chess.isDraw()) return 0;
  if (depth === 0) return material(chess);
  for (const m of chess.moves({ verbose: true })) {
    chess.move(m);
    const score = -negamax(chess, depth - 1, -beta, -alpha);
    chess.undo();
    if (score >= beta) return beta;
    if (score > alpha) alpha = score;
  }
  return alpha;
}

/**
 * Material-only alpha-beta search used until a real engine is attached.
 * shortcut: capped at 2 plies so it stays fast on Hermes, plays weakly; attach real Stockfish for strength.
 */
export function fallbackBestMove(fen: string, depth: number): string {
  const chess = new Chess(fen);
  const plies = Math.max(1, Math.min(2, depth));
  // shuffle so equal moves vary between games
  const moves = chess.moves({ verbose: true }).sort(() => Math.random() - 0.5);
  if (moves.length === 0) throw new Error('No legal moves: game is over');
  let best: Move = moves[0];
  let bestScore = -Infinity;
  for (const m of moves) {
    chess.move(m);
    const score = -negamax(chess, plies - 1, -Infinity, Infinity);
    chess.undo();
    if (score > bestScore) {
      bestScore = score;
      best = m;
    }
  }
  return best.lan;
}
