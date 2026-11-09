// A small stand-in for lichess.org's Board API, for testing online play without real accounts.
//
//   npm run lichess-fake          # http://localhost:8780
//
// Tokens: "test-token" signs in as caroline, "friend-token" as teammate.
// A random seek is paired with "random_human", a simulated person who answers each move after a short pause.
// Every request is printed, so this terminal shows what the app sent and what "Lichess" answered.
// Set FAKE_CASTLE_960=1 to send castling as king-takes-rook (e1h1), like Lichess may do.
import http from 'node:http';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { Chess } = require('chess.js');

const PORT = Number(process.env.PORT ?? 8780);
const OPPONENT_DELAY_MS = Number(process.env.OPPONENT_DELAY_MS ?? 800);
const SEEK_DELAY_MS = Number(process.env.SEEK_DELAY_MS ?? 1200);
const CASTLE_960 = process.env.FAKE_CASTLE_960 === '1';

const USERS = {
  'test-token': { id: 'caroline', username: 'caroline' },
  'friend-token': { id: 'teammate', username: 'teammate' },
};
const BOT = { id: 'random_human', username: 'random_human' };

const eventStreams = new Map(); // userId -> Set(res)
const games = new Map(); // gameId -> game
const challenges = new Map(); // id -> { id, from, to, tc, res }
let nextId = 1;
let rateLimitNext = false;

const time = () => new Date().toISOString().slice(11, 23);
const log = (msg) => console.log(`${time()} [fake-lichess] ${msg}`);
const newId = () => `g${String(nextId++).padStart(7, '0')}`;

function send(res, status, body) {
  res.writeHead(status, { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*' });
  res.end(JSON.stringify(body));
}

function openStream(res) {
  res.writeHead(200, { 'Content-Type': 'application/x-ndjson', 'Access-Control-Allow-Origin': '*' });
  const keepAlive = setInterval(() => res.write('\n'), 7000);
  res.on('close', () => clearInterval(keepAlive));
}

const line = (res, obj) => res.write(JSON.stringify(obj) + '\n');

function pushEvent(userId, event) {
  for (const res of eventStreams.get(userId) ?? []) line(res, event);
}

function readBody(req) {
  return new Promise((resolve) => {
    let data = '';
    req.on('data', (c) => (data += c));
    req.on('end', () => resolve(Object.fromEntries(new URLSearchParams(data))));
  });
}

// --- games ---------------------------------------------------------------------------------

function lichessUci(chess, move) {
  // Optionally report castling as king-takes-rook, the Chess960-compatible form.
  if (CASTLE_960 && move.isKingsideCastle()) return `${move.from}h${move.from[1]}`;
  if (CASTLE_960 && move.isQueensideCastle()) return `${move.from}a${move.from[1]}`;
  return move.lan;
}

function stateOf(game) {
  return {
    type: 'gameState',
    moves: game.moves.join(' '),
    wtime: game.clock.w,
    btime: game.clock.b,
    winc: game.tc.increment * 1000,
    binc: game.tc.increment * 1000,
    status: game.status,
    ...(game.winner ? { winner: game.winner } : {}),
  };
}

function broadcast(game) {
  const state = stateOf(game);
  for (const res of game.streams) line(res, state);
  if (game.status !== 'started') {
    for (const res of game.streams) res.end();
    game.streams.clear();
    for (const p of [game.white, game.black]) {
      pushEvent(p.id, { type: 'gameFinish', game: { gameId: game.id, id: game.id, status: { name: game.status } } });
    }
  }
}

function createGame(a, b, tc) {
  const [white, black] = Math.random() < 0.5 ? [a, b] : [b, a];
  const forced = process.env.FAKE_COLOR; // "white" or "black": force the color of the first player
  const ordered = forced === 'white' ? [a, b] : forced === 'black' ? [b, a] : [white, black];
  const game = {
    id: newId(),
    white: ordered[0],
    black: ordered[1],
    tc,
    chess: new Chess(),
    moves: [],
    status: 'started',
    winner: null,
    clock: { w: tc.minutes * 60000, b: tc.minutes * 60000 },
    lastMoveAt: Date.now(),
    streams: new Set(),
  };
  games.set(game.id, game);
  log(`game ${game.id} created: ${game.white.username} (white) vs ${game.black.username} (black), ${tc.minutes}+${tc.increment}`);
  for (const [me, them, color] of [
    [game.white, game.black, 'white'],
    [game.black, game.white, 'black'],
  ]) {
    pushEvent(me.id, {
      type: 'gameStart',
      game: { gameId: game.id, id: game.id, fullId: game.id + 'xxxx', color, isMyTurn: color === 'white', opponent: { id: them.id, username: them.username }, status: { name: 'started' } },
    });
  }
  botMoveIfNeeded(game);
  return game;
}

function play(game, uci) {
  const fixed = uci.length >= 4 ? uci : '';
  const move = game.chess.move({ from: fixed.slice(0, 2), to: fixed.slice(2, 4), promotion: fixed[4] });
  const side = move.color;
  const now = Date.now();
  if (game.moves.length >= 2) game.clock[side] = Math.max(0, game.clock[side] - (now - game.lastMoveAt) + game.tc.increment * 1000);
  game.lastMoveAt = now;
  game.moves.push(lichessUci(game.chess, move));
  if (game.chess.isCheckmate()) {
    game.status = 'mate';
    game.winner = side === 'w' ? 'white' : 'black';
  } else if (game.chess.isDraw()) {
    game.status = game.chess.isStalemate() ? 'stalemate' : 'draw';
  }
  broadcast(game);
  botMoveIfNeeded(game);
}

function botMoveIfNeeded(game) {
  if (game.status !== 'started') return;
  const toMove = game.chess.turn() === 'w' ? game.white : game.black;
  if (toMove.id !== BOT.id) return;
  setTimeout(() => {
    if (game.status !== 'started') return;
    const moves = game.chess.moves({ verbose: true });
    const pick = moves[Math.floor(Math.random() * moves.length)];
    log(`random_human plays ${pick.lan} in ${game.id}`);
    play(game, pick.lan);
  }, OPPONENT_DELAY_MS);
}

function gameFull(game) {
  const player = (p) => ({ id: p.id, name: p.username, rating: p.id === BOT.id ? 1500 : 1650 });
  return {
    type: 'gameFull',
    id: game.id,
    variant: { key: 'standard', name: 'Standard', short: 'Std' },
    clock: { initial: game.tc.minutes * 60000, increment: game.tc.increment * 1000 },
    speed: 'rapid',
    perf: { name: 'Rapid' },
    rated: false,
    createdAt: Date.now(),
    white: player(game.white),
    black: player(game.black),
    initialFen: 'startpos',
    state: stateOf(game),
  };
}

// --- server --------------------------------------------------------------------------------

const server = http.createServer(async (req, res) => {
  if (req.method === 'OPTIONS') {
    res.writeHead(204, {
      'Access-Control-Allow-Origin': '*',
      'Access-Control-Allow-Headers': 'Authorization, Content-Type, Accept',
      'Access-Control-Allow-Methods': 'GET, POST',
    });
    return res.end();
  }
  const url = new URL(req.url, `http://localhost:${PORT}`);
  const path = url.pathname;

  if (path === '/__test/rate-limit-next') {
    rateLimitNext = true;
    return send(res, 200, { ok: true });
  }

  const token = (req.headers.authorization ?? '').replace(/^Bearer /, '');
  const user = USERS[token];
  log(`${req.method} ${path}${user ? ` (${user.username})` : ''}`);
  if (rateLimitNext) {
    rateLimitNext = false;
    log('  -> 429 Too Many Requests');
    return send(res, 429, { error: 'Too many requests. Try again later.' });
  }
  if (!user) {
    log('  -> 401 bad token');
    return send(res, 401, { error: 'No such token' });
  }

  let m;
  if (req.method === 'GET' && path === '/api/account') return send(res, 200, user);

  if (req.method === 'GET' && path === '/api/stream/event') {
    openStream(res);
    // Only one event stream per account: a new one replaces the old.
    for (const old of eventStreams.get(user.id) ?? []) old.end();
    eventStreams.set(user.id, new Set([res]));
    res.on('close', () => eventStreams.get(user.id)?.delete(res));
    for (const g of games.values()) {
      if (g.status !== 'started') continue;
      const color = g.white.id === user.id ? 'white' : g.black.id === user.id ? 'black' : null;
      if (color) line(res, { type: 'gameStart', game: { gameId: g.id, id: g.id, color, status: { name: 'started' } } });
    }
    for (const c of challenges.values()) {
      if (c.to.id === user.id) line(res, challengeEvent(c));
    }
    return;
  }

  if (req.method === 'POST' && path === '/api/board/seek') {
    const body = await readBody(req);
    const tc = { minutes: Number(body.time), increment: Number(body.increment) };
    if (!(tc.minutes * 60 + 40 * tc.increment >= 480)) {
      log('  -> 400 Board API seeks must be Rapid or slower');
      return send(res, 400, { error: 'Invalid time control: Board API seeks must be Rapid or slower.' });
    }
    openStream(res);
    const timer = setTimeout(() => {
      createGame(user, BOT, tc);
      res.end(); // Lichess closes the seek connection once paired
    }, SEEK_DELAY_MS);
    res.on('close', () => {
      if (!res.writableEnded) log(`seek by ${user.username} cancelled`);
      clearTimeout(timer);
    });
    return;
  }

  if (req.method === 'POST' && (m = path.match(/^\/api\/challenge\/([^/]+)$/))) {
    const dest = Object.values(USERS).find((u) => u.username.toLowerCase() === decodeURIComponent(m[1]).toLowerCase());
    if (!dest) return send(res, 404, { error: 'No such user' });
    const body = await readBody(req);
    const c = {
      id: newId(),
      from: user,
      to: dest,
      tc: { minutes: Number(body['clock.limit']) / 60, increment: Number(body['clock.increment']) },
      res,
    };
    challenges.set(c.id, c);
    openStream(res);
    line(res, challengeJson(c));
    pushEvent(dest.id, challengeEvent(c));
    res.on('close', () => {
      if (challenges.get(c.id) === c) {
        challenges.delete(c.id);
        pushEvent(dest.id, { type: 'challengeCanceled', challenge: challengeJson(c) });
      }
    });
    return;
  }

  if (req.method === 'POST' && (m = path.match(/^\/api\/challenge\/([^/]+)\/(accept|decline|cancel)$/))) {
    const c = challenges.get(m[1]);
    if (!c) return send(res, 404, { error: 'Challenge not found' });
    challenges.delete(c.id);
    if (m[2] === 'accept') {
      createGame(c.from, c.to, c.tc);
    } else if (m[2] === 'decline') {
      pushEvent(c.from.id, { type: 'challengeDeclined', challenge: challengeJson(c) });
    } else {
      pushEvent(c.to.id, { type: 'challengeCanceled', challenge: challengeJson(c) });
    }
    c.res.end();
    return send(res, 200, { ok: true });
  }

  if (req.method === 'GET' && (m = path.match(/^\/api\/board\/game\/stream\/([^/]+)$/))) {
    const game = games.get(m[1]);
    if (!game) return send(res, 404, { error: 'Not found' });
    openStream(res);
    line(res, gameFull(game));
    if (game.status !== 'started') return res.end();
    game.streams.add(res);
    res.on('close', () => game.streams.delete(res));
    return;
  }

  if (req.method === 'POST' && (m = path.match(/^\/api\/board\/game\/([^/]+)\/move\/([a-h][1-8][a-h][1-8][qrbn]?)$/))) {
    const game = games.get(m[1]);
    if (!game) return send(res, 404, { error: 'Not found' });
    const mine = (game.chess.turn() === 'w' ? game.white : game.black).id === user.id;
    if (game.status !== 'started' || !mine) {
      log(`  -> 400 not your turn (${m[2]})`);
      return send(res, 400, { error: 'Not your turn, or game already over' });
    }
    try {
      play(game, m[2]);
    } catch {
      log(`  -> 400 illegal move ${m[2]}`);
      return send(res, 400, { error: `Piece on ${m[2].slice(0, 2)} cannot move to ${m[2].slice(2, 4)}` });
    }
    log(`  -> ok, ${user.username} played ${m[2]}`);
    return send(res, 200, { ok: true });
  }

  if (req.method === 'POST' && (m = path.match(/^\/api\/board\/game\/([^/]+)\/(resign|abort)$/))) {
    const game = games.get(m[1]);
    if (!game || game.status !== 'started') return send(res, 400, { error: 'Game already over' });
    if (m[2] === 'abort' && game.moves.length >= 2) return send(res, 400, { error: 'Game cannot be aborted' });
    game.status = m[2] === 'abort' ? 'aborted' : 'resign';
    if (m[2] === 'resign') game.winner = game.white.id === user.id ? 'black' : 'white';
    log(`  -> ${user.username} ${m[2] === 'abort' ? 'aborted' : 'resigned'} ${game.id}`);
    broadcast(game);
    return send(res, 200, { ok: true });
  }

  send(res, 404, { error: 'Not found' });
});

function challengeJson(c) {
  return {
    id: c.id,
    url: `http://localhost:${PORT}/${c.id}`,
    status: 'created',
    challenger: { id: c.from.id, name: c.from.username, rating: 1650 },
    destUser: { id: c.to.id, name: c.to.username, rating: 1650 },
    variant: { key: 'standard' },
    rated: false,
    speed: 'rapid',
    timeControl: { type: 'clock', limit: c.tc.minutes * 60, increment: c.tc.increment, show: `${c.tc.minutes}+${c.tc.increment}` },
    color: 'random',
    perf: { icon: '', name: 'Rapid' },
  };
}

function challengeEvent(c) {
  return { type: 'challenge', challenge: { ...challengeJson(c), direction: 'in' } };
}

server.listen(PORT, () => {
  log(`fake Lichess listening on http://localhost:${PORT}`);
  log('tokens: test-token = caroline, friend-token = teammate; seeks pair with random_human');
});
