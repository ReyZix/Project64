// Run: npm run test:stockfish  (plain Node; starts scripts/stockfish-bridge.mjs with real Stockfish)
//
// Drives the same code GameScreen uses: chess.js -> FEN -> WebSocketUciTransport -> bridge -> Stockfish
// -> bestmove -> parseUciMove -> chess.js. Nothing here is mocked except the screen itself.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { Chess } from 'chess.js';

import { toAppliedMove } from '../src/game/ChessGame.ts';
import { parseUciMove, requestComputerMove } from '../src/game/ComputerTurn.ts';
import { StockfishError, StockfishService } from '../src/services/StockfishService.ts';
import { WebSocketUciTransport } from '../src/services/WebSocketUciTransport.ts';

const PORT = 18765;
const URL = `ws://localhost:${PORT}`;
const quiet = console.log;
console.log = () => {}; // silence the service's own [Stockfish] logs; failures still print

const bridge = spawn(process.execPath, ['scripts/stockfish-bridge.mjs'], {
  env: { ...process.env, PORT: String(PORT) },
  stdio: ['ignore', 'pipe', 'inherit'],
});
let bridgeLog = '';
bridge.stdout.on('data', (d) => (bridgeLog += d));

const until = async (check: () => boolean, ms = 10000) => {
  for (
    const end = Date.now() + ms;
    Date.now() < end;
    await new Promise((r) => setTimeout(r, 50))
  ) {
    if (check()) return;
  }

  throw new Error('timed out waiting');
};

let passed = 0;

async function test(name: string, body: () => Promise<void>) {
  await body();
  passed++;
  quiet(`  ok  ${name}`);
}

async function connect() {
  const engine = new StockfishService();
  const transport = new WebSocketUciTransport(URL);

  assert.equal(await transport.connect(), true, 'bridge reachable');
  assert.equal(await engine.attachEngine(transport), true, 'UCI handshake');

  return { engine, transport };
}

try {
  await until(() => bridgeLog.includes('listening'));
  const { engine } = await connect();

  await test('UCI handshake reports a real Stockfish build', async () => {
    assert.match(engine.getEngineName() ?? '', /^Stockfish/);
    quiet(`      engine: ${engine.getEngineName()}`);
  });

  await test('e2e4 -> FEN -> Stockfish -> legal reply applied by chess.js', async () => {
    const chess = new Chess();

    chess.move({ from: 'e2', to: 'e4' });

    const fen = chess.fen();

    assert.equal(
      fen,
      'rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1',
    );

    const reply = await requestComputerMove(engine, fen, 10);

    assert.equal(reply.source, 'stockfish');

    const applied = toAppliedMove(
      chess.move({
        from: reply.from,
        to: reply.to,
        promotion: reply.promotion,
      }),
    );

    assert.equal(applied.by, 'black');
    assert.equal(applied.fenAfter, chess.fen());

    quiet(
      `      Stockfish answered 1.e4 with ${applied.san}: { from: "${applied.from}", to: "${applied.to}" }`,
    );
  });

  await test('bridge log shows the FEN went in and bestmove came out', async () => {
    // the bridge's stdout can arrive after the WebSocket reply
    await until(
      () =>
        /stockfish -> app: bestmove [a-h][1-8][a-h][1-8]/.test(bridgeLog),
      3000,
    );

    assert.match(
      bridgeLog,
      /app -> stockfish: position fen rnbqkbnr\/pppppppp\/8\/8\/4P3/,
    );
  });

  await test('12 full turns: every Stockfish reply is legal and applied', async () => {
    const chess = new Chess();

    for (let i = 0; i < 12 && !chess.isGameOver(); i++) {
      const moves = chess.moves({ verbose: true });

      chess.move(moves[i % moves.length]); // "player"

      if (chess.isGameOver()) break;

      const reply = await requestComputerMove(engine, chess.fen(), 8);

      assert.equal(reply.source, 'stockfish');

      chess.move({
        from: reply.from,
        to: reply.to,
        promotion: reply.promotion,
      });
    }

    assert.ok(chess.history().length >= 12);
  });

  await test('promotion reply is parsed with its piece', async () => {
    assert.deepEqual(parseUciMove('e7e8q'), {
      from: 'e7',
      to: 'e8',
      promotion: 'q',
    });

    assert.deepEqual(parseUciMove('e7e5'), {
      from: 'e7',
      to: 'e5',
    });

    assert.equal(parseUciMove('(none)'), null);

    engine.setSkillLevel(20);

    const reply = await requestComputerMove(
      engine,
      '7k/4P3/6K1/8/8/8/8/8 w - - 0 1',
      8,
    ); // e8=Q is mate

    engine.setSkillLevel(8);

    assert.deepEqual(
      [reply.from, reply.to, reply.promotion],
      ['e7', 'e8', 'q'],
    );

    const chess = new Chess(
      '7k/4P3/6K1/8/8/8/8/8 w - - 0 1',
    );

    const applied = toAppliedMove(
      chess.move({
        from: reply.from,
        to: reply.to,
        promotion: reply.promotion,
      }),
    );

    assert.equal(applied.uci, 'e7e8q');
    assert.equal(applied.isPromotion, true);
    assert.equal(chess.get('e8')?.type, 'q');
    assert.equal(chess.isCheckmate(), true);
  });

  await test(
    'bridge started a real Stockfish process and stops it when the app disconnects',
    async () => {
      const pids = [...bridgeLog.matchAll(/started engine pid (\d+)/g)].map(
        (m) => Number(m[1]),
      );

      assert.ok(pids.length >= 1);

      const temp = await connect();

      await until(
        () =>
          [...bridgeLog.matchAll(/started engine pid (\d+)/g)].length >
          pids.length,
        3000,
      );

      const pid = Number(
        [...bridgeLog.matchAll(/started engine pid (\d+)/g)].at(-1)![1],
      );

      assert.doesNotThrow(
        () => process.kill(pid, 0),
        'engine process is running',
      );

      temp.engine.detachEngine();

      await until(() => {
        try {
          process.kill(pid, 0);
          return false;
        } catch {
          return true;
        }
      }, 5000);
    },
  );

  await test(
    'timeout raises TIMEOUT and the late bestmove does not leak into the next search',
    async () => {
      const start = new Chess().fen();

      await assert.rejects(
        engine.getBestMove(start, 99, 300),
        (e: unknown) => (e as StockfishError).code === 'TIMEOUT',
      );

      const other =
        '4k3/8/8/3q4/4P3/8/8/4K3 w - - 0 1';

      const { uci, source } = await engine.getBestMove(other, 8);

      assert.equal(source, 'stockfish');
      assert.equal(uci, 'e4d5'); // takes the queen; a leaked opening move would fail here
    },
  );

  await test(
    'overlapping requests each get the move for their own position',
    async () => {
      const a = new Chess();
      a.move('e4');

      const b = new Chess();
      b.move('d4');

      const [ra, rb] = await Promise.all([
        engine.getBestMove(a.fen(), 8),
        engine.getBestMove(b.fen(), 8),
      ]);

      assert.doesNotThrow(() =>
        a.move({
          from: ra.uci.slice(0, 2),
          to: ra.uci.slice(2, 4),
        }),
      );

      assert.doesNotThrow(() =>
        b.move({
          from: rb.uci.slice(0, 2),
          to: rb.uci.slice(2, 4),
        }),
      );
    },
  );

  await test(
    'New Game stops a long search and the engine keeps working',
    async () => {
      const t0 = Date.now();

      const long = engine.getBestMove(
        new Chess().fen(),
        40,
        20000,
      );

      await new Promise((r) => setTimeout(r, 200));

      await engine.newGame();
      await long; // returns early because of `stop`; GameScreen drops it as stale

      assert.ok(
        Date.now() - t0 < 5000,
        'stop interrupted the search',
      );

      const { source } = await engine.getBestMove(
        '4k3/8/8/3q4/4P3/8/8/4K3 w - - 0 1',
        6,
      );

      assert.equal(source, 'stockfish');
    },
  );

  await test(
    'bridge going away is reported as DISCONNECTED, then the labelled fallback is used',
    async () => {
      const second = await connect();

      const pending = second.engine.getBestMove(
        new Chess().fen(),
        40,
        20000,
      );

      second.engine.detachEngine();

      await assert.rejects(
        pending,
        (e: unknown) =>
          (e as StockfishError).code === 'DISCONNECTED',
      );

      const { source } = await second.engine.getBestMove(
        new Chess().fen(),
        2,
      );

      assert.equal(source, 'fallback');
    },
  );

  await test('no bridge running -> connect fails cleanly', async () => {
    assert.equal(
      await new WebSocketUciTransport(
        'ws://localhost:1',
      ).connect(2000),
      false,
    );
  });

  engine.detachEngine();

  quiet(`stockfish-flow-test: ${passed} passed`);
} catch (e) {
  quiet('FAILED:', e);
  quiet('--- bridge log ---\n' + bridgeLog);
  process.exitCode = 1;
} finally {
  bridge.kill();
}