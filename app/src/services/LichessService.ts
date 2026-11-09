/*
 * Talks to the Lichess Board API (https://lichess.org/api#tag/board) for online games against people.
 * Long-lived ndjson streams deliver events and moves; short requests send moves and answer challenges.
 * Lichess asks clients to send one request at a time and to wait a minute after an HTTP 429.
 */

export const LICHESS_URL = 'https://lichess.org';

export type LichessErrorCode = 'BAD_TOKEN' | 'RATE_LIMITED' | 'REJECTED' | 'NETWORK';

export class LichessError extends Error {
  code: LichessErrorCode;
  status: number;
  constructor(code: LichessErrorCode, message: string, status = 0) {
    super(message);
    this.name = 'LichessError';
    this.code = code;
    this.status = status;
  }
}

type FetchLike = (url: string, init?: Record<string, unknown>) => Promise<{
  ok: boolean;
  status: number;
  body?: unknown;
  text(): Promise<string>;
  json(): Promise<unknown>;
}>;

export interface TimeControl {
  minutes: number;
  increment: number;
}

/** A running stream; close() ends it, done resolves when the server or close() ends it. */
export interface StreamHandle {
  close(): void;
  done: Promise<void>;
}

const RATE_LIMIT_MS = 60_000;
const log = (message: string) => console.log(`[Lichess] ${message}`);

export class LichessService {
  private token: string;
  private baseUrl: string;
  private fetchImpl: FetchLike;
  private queue: Promise<unknown> = Promise.resolve();
  private rateLimitedUntil = 0;

  constructor(options: { token: string; baseUrl?: string; fetch: FetchLike }) {
    this.token = options.token;
    this.baseUrl = (options.baseUrl ?? LICHESS_URL).replace(/\/$/, '');
    // Call fetch unbound: browsers throw "Illegal invocation" if it runs as a method of another object.
    const fetchFn = options.fetch;
    this.fetchImpl = (url, init) => fetchFn(url, init);
  }

  // --- short requests (one at a time) ------------------------------------------------

  /** Checks the token and returns the signed-in account. */
  account() {
    return this.request<{ id: string; username: string }>('GET', '/api/account');
  }

  makeMove(gameId: string, uci: string) {
    log(`move sent to Lichess: ${uci}`);
    return this.request('POST', `/api/board/game/${gameId}/move/${uci}`);
  }

  resign(gameId: string) {
    return this.request('POST', `/api/board/game/${gameId}/resign`);
  }

  abort(gameId: string) {
    return this.request('POST', `/api/board/game/${gameId}/abort`);
  }

  acceptChallenge(id: string) {
    return this.request('POST', `/api/challenge/${id}/accept`);
  }

  declineChallenge(id: string) {
    return this.request('POST', `/api/challenge/${id}/decline`);
  }

  cancelChallenge(id: string) {
    return this.request('POST', `/api/challenge/${id}/cancel`);
  }

  // --- streams ---------------------------------------------------------------------------

  /** Incoming events: gameStart, gameFinish, challenge, challengeCanceled, challengeDeclined. */
  streamEvents(onEvent: (event: Record<string, any>) => void): StreamHandle {
    return this.stream('GET', '/api/stream/event', undefined, onEvent);
  }

  /** The live game: gameFull first, then gameState after every move, chatLine, opponentGone. */
  streamGame(gameId: string, onEvent: (event: Record<string, any>) => void): StreamHandle {
    return this.stream('GET', `/api/board/game/stream/${gameId}`, undefined, onEvent);
  }

  /** Looks for a random opponent. The seek stays active only while this stream is open. */
  seek(tc: TimeControl, rated = false): StreamHandle {
    log(`seeking a random opponent (${tc.minutes}+${tc.increment}, ${rated ? 'rated' : 'casual'})`);
    return this.stream('POST', '/api/board/seek', form({ rated, time: tc.minutes, increment: tc.increment }), () => {});
  }

  /**
   * Challenges a specific user. `keepAliveStream` keeps the challenge open while we wait,
   * otherwise Lichess expires it after 20 seconds. The first line is the challenge itself.
   */
  challenge(username: string, tc: TimeControl, onCreated: (id: string) => void, rated = false): StreamHandle {
    log(`challenging ${username} (${tc.minutes}+${tc.increment})`);
    const body = form({
      rated,
      'clock.limit': tc.minutes * 60,
      'clock.increment': tc.increment,
      keepAliveStream: true,
    });
    return this.stream('POST', `/api/challenge/${encodeURIComponent(username)}`, body, (line) => {
      const id = line.id ?? line.challenge?.id;
      if (typeof id === 'string') onCreated(id);
    });
  }

  // --- plumbing --------------------------------------------------------------------------

  private headers(body?: string) {
    return {
      Authorization: `Bearer ${this.token}`,
      Accept: 'application/x-ndjson, application/json',
      ...(body !== undefined ? { 'Content-Type': 'application/x-www-form-urlencoded' } : {}),
    };
  }

  private checkRateLimit() {
    const wait = this.rateLimitedUntil - Date.now();
    if (wait > 0) {
      throw new LichessError('RATE_LIMITED', `Lichess asked us to slow down. Try again in ${Math.ceil(wait / 1000)}s.`, 429);
    }
  }

  private async fail(res: { status: number; text(): Promise<string> }): Promise<never> {
    const detail = await res.text().catch(() => '');
    if (res.status === 401) throw new LichessError('BAD_TOKEN', 'Lichess rejected the token. Check it in Settings.', 401);
    if (res.status === 429) {
      this.rateLimitedUntil = Date.now() + RATE_LIMIT_MS;
      throw new LichessError('RATE_LIMITED', 'Lichess asked us to slow down. Try again in a minute.', 429);
    }
    throw new LichessError('REJECTED', errorMessage(detail) ?? `Lichess refused the request (${res.status}).`, res.status);
  }

  private request<T = unknown>(method: string, path: string, body?: string): Promise<T> {
    const run = async () => {
      this.checkRateLimit();
      let res;
      try {
        res = await this.fetchImpl(this.baseUrl + path, { method, headers: this.headers(body), body });
      } catch (e) {
        throw new LichessError('NETWORK', `Could not reach Lichess: ${e instanceof Error ? e.message : e}`);
      }
      if (!res.ok) return this.fail(res);
      return (await res.json().catch(() => ({}))) as T;
    };
    const next = this.queue.then(run, run);
    this.queue = next.catch(() => undefined);
    return next;
  }

  private stream(
    method: string,
    path: string,
    body: string | undefined,
    onLine: (line: Record<string, any>) => void,
  ): StreamHandle {
    const controller = new AbortController();
    const done = (async () => {
      this.checkRateLimit();
      let res;
      try {
        res = await this.fetchImpl(this.baseUrl + path, {
          method,
          headers: this.headers(body),
          body,
          signal: controller.signal,
        });
      } catch (e) {
        if (controller.signal.aborted) return;
        throw new LichessError('NETWORK', `Could not reach Lichess: ${e instanceof Error ? e.message : e}`);
      }
      if (!res.ok) return this.fail(res);
      try {
        await readNdjson(res, onLine);
      } catch (e) {
        if (!controller.signal.aborted) throw new LichessError('NETWORK', 'The connection to Lichess dropped.');
      }
    })();
    return { close: () => controller.abort(), done };
  }
}

/** Reads an ndjson body line by line; empty keep-alive lines are skipped. */
async function readNdjson(res: { body?: unknown; text(): Promise<string> }, onLine: (line: any) => void) {
  const emit = (text: string) => {
    if (!text.trim()) return;
    try {
      onLine(JSON.parse(text));
    } catch {
      // ignore a malformed line rather than killing the whole stream
    }
  };
  const reader = (res.body as { getReader?: () => any } | undefined)?.getReader?.();
  if (!reader) {
    (await res.text()).split('\n').forEach(emit);
    return;
  }
  let pending = new Uint8Array(0);
  for (;;) {
    const { value, done } = await reader.read();
    if (done) break;
    pending = concat(pending, value);
    let nl;
    while ((nl = pending.indexOf(10)) >= 0) {
      emit(utf8(pending.subarray(0, nl)));
      pending = pending.slice(nl + 1);
    }
  }
  emit(utf8(pending));
}

function concat(a: Uint8Array, b: Uint8Array) {
  const out = new Uint8Array(a.length + b.length);
  out.set(a);
  out.set(b, a.length);
  return out;
}

const decoder = typeof TextDecoder !== 'undefined' ? new TextDecoder() : null;

/** UTF-8 bytes to string; the manual path covers runtimes without TextDecoder. */
function utf8(bytes: Uint8Array) {
  if (decoder) return decoder.decode(bytes);
  let out = '';
  for (let i = 0; i < bytes.length; ) {
    const b = bytes[i++];
    let cp = b;
    if (b >= 0xf0) cp = ((b & 7) << 18) | ((bytes[i++] & 63) << 12) | ((bytes[i++] & 63) << 6) | (bytes[i++] & 63);
    else if (b >= 0xe0) cp = ((b & 15) << 12) | ((bytes[i++] & 63) << 6) | (bytes[i++] & 63);
    else if (b >= 0xc0) cp = ((b & 31) << 6) | (bytes[i++] & 63);
    out += String.fromCodePoint(cp);
  }
  return out;
}

function form(fields: Record<string, string | number | boolean>) {
  return Object.entries(fields)
    .map(([k, v]) => `${encodeURIComponent(k)}=${encodeURIComponent(String(v))}`)
    .join('&');
}

function errorMessage(body: string) {
  try {
    const parsed = JSON.parse(body);
    return typeof parsed.error === 'string' ? parsed.error : null;
  } catch {
    return body.trim() || null;
  }
}
