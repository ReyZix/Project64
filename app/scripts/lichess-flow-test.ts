// Run: npm run test:lichess  (plain Node; starts scripts/fake-lichess-server.mjs)
//
// Drives the same code the app uses (LichessService + LichessGame) against a local fake Lichess:
// sign-in, random seek, friend challenges (accept / decline / cancel), moves both ways, resign, errors.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { Chess } from 'chess.js';

import {
  lichessMovesToLan,
  localMoves,
  normalizeCastling,
  planSync,
  rebuildGame,
  resultText,
} from '../src/game/LichessGame.ts';
import { LichessError, LichessService } from '../src/services/LichessService.ts';

const PORT = 18780;
const BASE = `http://localhost:${PORT}`;
const quiet = console.log;
console.log = () => {}; // silence the service's own [Lichess] logs

const server = spawn(process.execPath, ['scripts/fake-lichess-server.mjs'], {
  env: { ...process.env, PORT: String(PORT), OPPONENT_DELAY_MS: '50', SEEK_DELAY_MS: '100' },
  stdio: ['ignore', 'pipe', 'inherit'],
});
let serverLog = '';
server.stdout.on('data', (d) => (serverLog += d));

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
async function until<T>(check: () => T | undefined | null | false, ms = 5000): Promise<T> {
  for (const end = Date.now() + ms; Date.now() < end; await sleep(20)) {
    const v = check();
    if (v) return v;
  }
  throw new Error('timed out waiting');
}

let passed = 0;
async function test(name: string, body: () => Promise<void>) {
  await body();
  passed++;
  quiet(`  ok  ${name}`);
}

const svc = (token: string) => new LichessService({ token, baseUrl: BASE, fetch: fetch as never });

/** One side of an online game, kept in sync exactly like the app does. */
function player(service: LichessService) {
  const events: Record<string, any>[] = [];
  const events$ = service.streamEvents((e) => events.push(e));
  events$.done.catch((e) => quiet(`      (event stream ended: ${e.message})`));
  const chess = new Chess();
  const side = { events, events$, chess, gameId: '', color: '', status: 'started', winner: undefined as string | undefined, stream: null as any };
  side.open = (gameId: string) => {
    side.gameId = gameId;
    side.stream = service.streamGame(gameId, (e) => {
      if (e.type === 'gameFull') {
        rebuildGame(chess, e.initialFen, lichessMovesToLan(e.initialFen, e.state.moves));
        side.color = e.white.id === 'caroline' || e.white.id === 'teammate' ? (e.white.id === side.me ? 'white' : 'black') : side.color;
        side.status = e.state.status;
      } else if (e.type === 'gameState') {
        const plan = planSync(localMoves(chess), lichessMovesToLan('startpos', e.moves));
        if (plan.kind === 'apply') chess.move(plan.uci);
        else if (plan.kind === 'rebuild') rebuildGame(chess, 'startpos', plan.moves);
        side.status = e.status;
        side.winner = e.winner;
      }
    });
    side.stream.done.catch((e: Error) => quiet(`      (game stream ended: ${e.message})`));
  };
  return side as typeof side & { open(id: string): void; me?: string };
}

/** Plays one move locally (like a tap on the board) and sends it to Lichess. */
async function playMove(service: LichessService, side: ReturnType<typeof player>) {
  const move = side.chess.moves({ verbose: true })[0];
  side.chess.move(move);
  await service.makeMove(side.gameId, move.lan);
  return move.lan;
}

try {
  await until(() => serverLog.includes('listening'));

  await test('wrong token is reported as BAD_TOKEN', async () => {
    await assert.rejects(svc('nope').account(), (e: unknown) => (e as LichessError).code === 'BAD_TOKEN');
  });

  const caroline = svc('test-token');
  const teammate = svc('friend-token');

  await test('sign in returns the Lichess account', async () => {
    assert.deepEqual(await caroline.account(), { id: 'caroline', username: 'caroline' });
  });

  await test('sync rules: identical, one new move, own move pending, mismatch', async () => {
    assert.deepEqual(planSync(['e2e4'], ['e2e4']), { kind: 'none' });
    assert.deepEqual(planSync(['e2e4'], ['e2e4', 'e7e5']), { kind: 'apply', uci: 'e7e5' });
    assert.deepEqual(planSync(['e2e4', 'e7e5'], ['e2e4']), { kind: 'wait' });
    assert.deepEqual(planSync(['e2e4', 'd7d5'], ['e2e4', 'e7e5']), { kind: 'rebuild', moves: ['e2e4', 'e7e5'] });
    assert.deepEqual(planSync([], ['e2e4', 'e7e5', 'g1f3']), { kind: 'rebuild', moves: ['e2e4', 'e7e5', 'g1f3'] });
  });

  await test('castling sent as king-takes-rook (e1h1) is converted to e1g1', async () => {
    const pos = new Chess('r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1');
    assert.equal(normalizeCastling(pos, 'e1h1'), 'e1g1');
    assert.equal(normalizeCastling(pos, 'e1a1'), 'e1c1');
    assert.equal(normalizeCastling(pos, 'e1g1'), 'e1g1');
    const lan = lichessMovesToLan('startpos', 'e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 e1h1');
    assert.equal(lan.at(-1), 'e1g1');
  });

  await test('result text from the player point of view', async () => {
    assert.equal(resultText('mate', 'white', 'white'), 'You won · Checkmate');
    assert.equal(resultText('resign', 'white', 'black'), 'You lost · Resignation');
    assert.equal(resultText('draw', undefined, 'white'), 'Draw');
  });

  await test('random seek pairs with an opponent and a 6-turn game stays in sync', async () => {
    const me = player(caroline);
    me.me = 'caroline';
    await sleep(100);
    const seek = caroline.seek({ minutes: 10, increment: 0 });
    const start = await until(() => me.events.find((e) => e.type === 'gameStart'));
    await seek.done; // server closes the seek once paired
    me.color = start.game.color;
    me.open(start.game.gameId);
    await until(() => me.stream);
    await sleep(100);
    for (let turn = 0; turn < 6; turn++) {
      const myTurn = (me.chess.turn() === 'w') === (me.color === 'white');
      if (!myTurn) await until(() => (me.chess.turn() === 'w') === (me.color === 'white'));
      const before = me.chess.history().length;
      await playMove(caroline, me);
      // the opponent (random_human) answers; our board must pick it up
      await until(() => me.chess.history().length >= before + 2 || me.status !== 'started');
      if (me.status !== 'started') break;
    }
    // Ask Lichess for the game again (first line = gameFull) and compare with our board.
    let full: Record<string, any> | null = null;
    const check = caroline.streamGame(me.gameId, (e) => (full ??= e));
    await until(() => full);
    check.close();
    assert.deepEqual(localMoves(me.chess), lichessMovesToLan('startpos', full!.state.moves));
    assert.ok(me.chess.history().length >= 12 || me.status !== 'started');
    quiet(`      played ${me.chess.history().length} half-moves as ${me.color}, board matches Lichess`);
    me.events$.close();
    me.stream.close();
  });

  await test('friend challenge: accept, play both sides, resign ends the game for both', async () => {
    const a = player(caroline);
    a.me = 'caroline';
    const b = player(teammate);
    b.me = 'teammate';
    await sleep(150);
    let challengeId = '';
    const ch = caroline.challenge('teammate', { minutes: 10, increment: 5 }, (id) => (challengeId = id));
    const incoming = await until(() => b.events.find((e) => e.type === 'challenge'));
    assert.equal(incoming.challenge.challenger.name, 'caroline');
    assert.equal(incoming.challenge.timeControl.show, '10+5');
    await until(() => challengeId);
    await teammate.acceptChallenge(incoming.challenge.id);
    // Lichess also re-sends games still in progress when a stream opens, so match the new game by id.
    const startB = await until(() => b.events.find((e) => e.type === 'gameStart'));
    const startA = await until(() => a.events.find((e) => e.type === 'gameStart' && e.game.gameId === startB.game.gameId));
    assert.equal(startA.game.gameId, startB.game.gameId);
    assert.notEqual(startA.game.color, startB.game.color);
    await ch.done; // the keep-alive challenge stream ends once accepted
    a.color = startA.game.color;
    b.color = startB.game.color;
    a.open(startA.game.gameId);
    b.open(startB.game.gameId);
    await sleep(150);
    const white = a.color === 'white' ? { s: caroline, p: a } : { s: teammate, p: b };
    const black = a.color === 'white' ? { s: teammate, p: b } : { s: caroline, p: a };
    for (let i = 0; i < 4; i++) {
      for (const side of [white, black]) {
        const n = side.p.chess.history().length;
        await playMove(side.s, side.p);
        const other = side === white ? black : white;
        await until(() => other.p.chess.history().length === n + 1);
      }
    }
    assert.deepEqual(localMoves(a.chess), localMoves(b.chess));
    await teammate.resign(startB.game.gameId);
    await until(() => a.status === 'resign' && b.status === 'resign');
    const winner = b.color === 'white' ? 'black' : 'white';
    assert.equal(a.winner, winner);
    quiet(`      8 moves each side in sync, teammate resigned -> ${resultText('resign', a.winner as never, a.color as never)} for caroline`);
    for (const p of [a, b]) p.events$.close();
  });

  await test('declined challenge is reported to the challenger', async () => {
    const a = player(caroline);
    const b = player(teammate);
    await sleep(150);
    const ch = caroline.challenge('teammate', { minutes: 15, increment: 10 }, () => {});
    const incoming = await until(() => b.events.find((e) => e.type === 'challenge'));
    await teammate.declineChallenge(incoming.challenge.id);
    await until(() => a.events.find((e) => e.type === 'challengeDeclined'));
    await ch.done;
    for (const p of [a, b]) p.events$.close();
  });

  await test('cancelling a challenge removes it for the friend', async () => {
    const b = player(teammate);
    await sleep(150);
    let id = '';
    const ch = caroline.challenge('teammate', { minutes: 10, increment: 0 }, (x) => (id = x));
    await until(() => b.events.find((e) => e.type === 'challenge'));
    await until(() => id);
    ch.close();
    await caroline.cancelChallenge(id).catch(() => undefined);
    await until(() => b.events.find((e) => e.type === 'challengeCanceled'));
    b.events$.close();
  });

  await test('cancelling a seek closes it before pairing', async () => {
    const before = serverLog.split('created:').length;
    const seeksBefore = serverLog.split('POST /api/board/seek').length;
    const seek = caroline.seek({ minutes: 30, increment: 0 });
    await until(() => serverLog.split('POST /api/board/seek').length > seeksBefore);
    seek.close();
    await seek.done;
    await sleep(300);
    assert.equal(serverLog.split('created:').length, before, 'no game was created');
    assert.match(serverLog, /seek by caroline cancelled/);
  });

  await test('Lichess refusing a move is reported as REJECTED', async () => {
    const me = player(caroline);
    await sleep(100);
    const seek = caroline.seek({ minutes: 10, increment: 0 });
    const start = await until(() => me.events.find((e) => e.type === 'gameStart'));
    await seek.done;
    await assert.rejects(caroline.makeMove(start.game.gameId, 'e2e5'), (e: unknown) => (e as LichessError).code === 'REJECTED');
    me.events$.close();
  });

  await test('seek with a too-fast time control is rejected by Lichess', async () => {
    await assert.rejects(caroline.seek({ minutes: 3, increment: 0 }).done, (e: unknown) => (e as LichessError).code === 'REJECTED');
  });

  await test('HTTP 429 sets a one-minute pause and later requests wait instead of hammering Lichess', async () => {
    const limited = svc('test-token');
    await fetch(`${BASE}/__test/rate-limit-next`);
    await assert.rejects(limited.account(), (e: unknown) => (e as LichessError).code === 'RATE_LIMITED');
    const requestsBefore = serverLog.split('GET /api/account').length;
    await assert.rejects(limited.account(), (e: unknown) => (e as LichessError).code === 'RATE_LIMITED');
    assert.equal(serverLog.split('GET /api/account').length, requestsBefore, 'second call never reached the server');
  });

  quiet(`lichess-flow-test: ${passed} passed`);
} catch (e) {
  quiet('FAILED:', e);
  quiet('--- server log ---\n' + serverLog.split('\n').slice(-40).join('\n'));
  process.exitCode = 1;
} finally {
  server.kill();
  setTimeout(() => process.exit(), 100);
}
