// Connects the app to a real Stockfish engine through WebSocket and UCI.
//
// Run with:
// npm run stockfish-bridge
//
// The app connects to ws://<this machine>:8765.
// Each connection starts its own Stockfish engine process.

import { spawn } from 'node:child_process';
import { createRequire } from 'node:module';
import { networkInterfaces } from 'node:os';
import { createInterface } from 'node:readline';
import { WebSocketServer } from 'ws';

const require = createRequire(import.meta.url);
const PORT = Number(process.env.PORT ?? 8765);
const VERBOSE = process.env.VERBOSE === '1'; // also print every `info` line

const time = () => new Date().toISOString().slice(11, 23);
const log = (message) => console.log(`${time()} [bridge] ${message}`);

function engineCommand() {
  if (process.env.STOCKFISH_PATH) return [process.env.STOCKFISH_PATH, []];

  const findEngine = require('stockfish/scripts/findEngine.js');
  return [process.execPath, [findEngine('lite-single')]];
}

const wss = new WebSocketServer({ port: PORT });
let clients = 0;

wss.on('listening', () => {
  const [cmd, args] = engineCommand();

  log(`listening on ws://localhost:${PORT}`);

  for (const nets of Object.values(networkInterfaces())) {
    for (const net of nets ?? []) {
      if (net.family === 'IPv4' && !net.internal) {
        log(`  phone on the same Wi-Fi: ws://${net.address}:${PORT}`);
      }
    }
  }

  log(`engine: ${[cmd, ...args].join(' ')}`);
});

wss.on('connection', (ws, req) => {
  const who = `#${++clients} ${req.socket.remoteAddress}`;
  const [cmd, args] = engineCommand();

  const engine = spawn(cmd, args, {
    stdio: ['pipe', 'pipe', 'inherit'],
  });

  engine.stdin.on('error', () => {}); // engine already gone

  log(`${who} app connected, started engine pid ${engine.pid}`);

  let lastInfo = '';

  createInterface({ input: engine.stdout }).on('line', (line) => {
    if (!line.trim()) return;

    if (line.startsWith('info') || line.startsWith('option')) {
      if (VERBOSE) {
        log(`${who} stockfish -> app: ${line}`);
      }

      if (line.includes(' pv ')) {
        lastInfo = line;
      }
    } else {
      if (line.startsWith('bestmove') && lastInfo) {
        log(`${who} stockfish search: ${lastInfo}`);
      }

      log(`${who} stockfish -> app: ${line}`);
    }

    if (ws.readyState === ws.OPEN) {
      ws.send(line);
    }
  });

  ws.on('message', (data) => {
    for (const line of String(data).split(/\r?\n/)) {
      if (!line.trim()) continue;

      log(`${who} app -> stockfish: ${line}`);
      engine.stdin.write(`${line}\n`);
    }
  });

  ws.on('close', () => {
    log(`${who} app disconnected, stopping engine`);
    engine.stdin.end('quit\n');

    setTimeout(() => {
      engine.kill();
    }, 500);
  });

  engine.on('exit', (code) => {
    log(`${who} engine exited (${code})`);
    ws.close();
  });
});