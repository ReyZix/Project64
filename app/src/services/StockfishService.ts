import { Chess, type Move } from 'chess.js';

import type { EngineSource } from '../game/Types';

/**
 * Line-based UCI pipe to a real engine: a native Stockfish module, a WASM worker in a
 * WebView, or a socket to a desktop engine. Anything that can send and receive UCI lines.
 */
export interface UciTransport {
  send(command: string): void;
  onLine(callback: (line: string) => void): () => void;
  // Releases the connection or process behind the transport.
  close?(): void;
}

// A best move plus where it came from, so the UI and logs can prove it was real Stockfish.
export interface EngineMove {
  uci: string;
  source: EngineSource;
}

export type StockfishErrorCode = 'TIMEOUT' | 'DISCONNECTED' | 'NO_MOVE' | 'ILLEGAL_MOVE';

export class StockfishError extends Error {
  code: StockfishErrorCode;
  constructor(code: StockfishErrorCode, message: string) {
    super(message);
    this.name = 'StockfishError';
    this.code = code;
  }
}

const UCI_RE = /\b[a-h][1-8][a-h][1-8][qrbn]?\b/;
const log = (message: string) => console.log(`[Stockfish] ${message}`);
export const FALLBACK_LABEL = 'FALLBACK ENGINE — NOT STOCKFISH';

/**
 * Engine abstraction used by the game loop. Order of preference:
 *   1. attached UCI engine (once it answered uciok/readyok)
 *   2. REST analysis endpoint, if configured (GET <url>?fen=&depth=, JSON with a `bestmove` field)
 *   3. built-in chess.js search, so play never blocks while the native engine initializes
 */
// Every result says which engine produced it.
export class StockfishService {
  private transport: UciTransport | null = null;
  private ready = false;
  private engineName: string | null = null;
  private restUrl: string | null;
  private skillLevel = 8;
  // UCI is one conversation: searches run one at a time so a bestmove is never read by the wrong caller.
  private queue: Promise<unknown> = Promise.resolve();
  private pending = new Set<(line: string | null) => void>();

  constructor(options: { restUrl?: string } = {}) {
    this.restUrl = options.restUrl ?? null;
  }

  async attachEngine(transport: UciTransport, timeoutMs = 10000): Promise<boolean> {
    this.detachEngine();
    this.transport = transport;
    this.ready = false;
    const offId = transport.onLine((l) => {
      if (l.startsWith('id name ')) this.engineName = l.slice('id name '.length).trim();
    });
    const ok = (await this.request('uci', (l) => l === 'uciok', timeoutMs)) !== null;
    offId();
    if (this.transport !== transport) return false; // replaced while the handshake was running
    this.ready = ok && (await this.request('isready', (l) => l === 'readyok', timeoutMs)) !== null;
    if (this.ready) {
      this.applySkill();
      log(`Real Stockfish connected: ${this.engineName ?? 'UCI engine'}`);
    } else {
      this.transport = null;
      log('engine did not finish the UCI handshake');
    }
    return this.ready;
  }

  // Drops the attached engine. Waiting searches fail with DISCONNECTED.
  detachEngine(transport?: UciTransport) {
    if (!this.transport || (transport && transport !== this.transport)) return;
    const old = this.transport;
    this.transport = null;
    this.ready = false;
    this.engineName = null;
    this.pending.forEach((finish) => finish(null));
    old.close?.();
  }

  isConnected() {
    return this.ready;
  }

  getEngineName() {
    return this.engineName;
  }

  // Stops the current Stockfish search.
  stop() {
    if (this.ready) this.transport?.send('stop');
  }

  // Starts a fresh UCI game when the real engine is connected.
  async newGame(): Promise<void> {
    if (!this.transport || !this.ready) return;
    this.stop();
    await this.enqueue(async () => {
      this.transport?.send('ucinewgame');
      await this.request('isready', (l) => l === 'readyok', 10000);
      this.applySkill();
    });
  }

  setSkillLevel(level: number) {
    this.skillLevel = Math.max(0, Math.min(20, Math.round(level)));
    if (this.ready) this.applySkill();
  }

  private applySkill() {
    this.transport?.send(`setoption name Skill Level value ${this.skillLevel}`);
  }

  private enqueue<T>(task: () => Promise<T>): Promise<T> {
    const run = this.queue.then(task, task);
    this.queue = run.catch(() => undefined);
    return run;
  }

  // Sends a UCI command and waits for the expected response.
  private request(command: string | null, done: (line: string) => boolean, timeoutMs: number) {
    const transport = this.transport;
    if (!transport) return Promise.resolve(null);
    return new Promise<string | null>((resolve) => {
      const finish = (line: string | null) => {
        clearTimeout(timer);
        off();
        this.pending.delete(finish);
        resolve(line);
      };
      const timer = setTimeout(() => finish(null), timeoutMs);
      const off = transport.onLine((line) => done(line.trim()) && finish(line.trim()));
      this.pending.add(finish);
      if (command) transport.send(command);
    });
  }

  // Gets the best move for the current FEN and identifies which engine produced it.
  async getBestMove(fen: string, depth: number, timeoutMs = 15000): Promise<EngineMove> {
    if (this.ready) return this.enqueue(() => this.searchWithEngine(fen, depth, timeoutMs));
    if (this.restUrl) {
      try {
        const res = await fetch(`${this.restUrl}?fen=${encodeURIComponent(fen)}&depth=${Math.min(depth, 15)}`);
        const move = String((await res.json()).bestmove ?? '').match(UCI_RE)?.[0];
        // The REST answer is untrusted: only use it if it is legal here.
        if (move && isLegal(fen, move)) return { uci: move, source: 'rest' };
      } catch {
        // fall through to the local search
      }
    }
    const uci = fallbackBestMove(fen, depth);
    console.warn(`[Stockfish] ${FALLBACK_LABEL}: no engine connected, built-in search chose ${uci}`);
    return { uci, source: 'fallback' };
  }

  private async searchWithEngine(fen: string, depth: number, timeoutMs: number): Promise<EngineMove> {
    const transport = this.transport;
    if (!transport || !this.ready) throw new StockfishError('DISCONNECTED', 'Stockfish disconnected');
    log(`FEN sent to Stockfish: ${fen}`);
    transport.send(`position fen ${fen}`);
    const started = Date.now();
    const line = await this.request(`go depth ${depth}`, (l) => l.startsWith('bestmove'), timeoutMs);
    if (line === null) {
      if (this.transport !== transport) throw new StockfishError('DISCONNECTED', 'Stockfish disconnected');
      // The stopped search still prints one bestmove: swallow it so the next search cannot read it.
      transport.send('stop');
      await this.request(null, (l) => l.startsWith('bestmove'), 2000);
      throw new StockfishError('TIMEOUT', `Stockfish did not answer within ${timeoutMs / 1000}s`);
    }
    const move = line.split(/\s+/)[1];
    log(`Stockfish bestmove: ${move} (${Date.now() - started} ms, depth ${depth})`);
    if (!move || !UCI_RE.test(move)) throw new StockfishError('NO_MOVE', `Stockfish has no move: ${line}`);
    if (!isLegal(fen, move)) throw new StockfishError('ILLEGAL_MOVE', `Stockfish move ${move} is illegal here`);
    return { uci: move, source: 'stockfish' };
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