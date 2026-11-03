# Project 64 — Companion App

React Native (Expo SDK 58, TypeScript) app that runs the game for the Project 64 robotic chessboard. It enforces the rules with `chess.js`, runs Stockfish locally on the phone, and sends moves to the ESP32 over BLE, Wi-Fi, or a simulated board.

## Setup

```bash
cd app
npm install
npm test              # self-check: sensor bitmask decoding + fallback engine
npm run typecheck
```

### Run on a simulator (no hardware)

The app starts on the **Simulator Mock** transport, so it is playable without an ESP32:

```bash
npx expo run:ios       # or: npx expo run:android
```

`react-native-ble-plx` is a native module, so the app needs a **development build** (`expo run:*` or `eas build --profile development`). In Expo Go the BLE transport fails without crashing, and Mock still works.

### Run against the real board

Open **Settings** (gear icon) and choose:
- **BLE**: enter the advertised device name (default `Project64`) and tap Connect. This needs a physical phone, because simulators have no Bluetooth.
- **Wi-Fi (LAN)**: enter the ESP32's IP address (default `192.168.4.1`, the softAP address) and tap Connect.

## Layout

```
src/
  types/chess.ts                  game modes, connection/gantry status, opcodes
  utils/occupancy.ts              64-bit sensor bitmask <-> chess position, move inference
  services/
    hardware/HardwareBridge.ts    IHardwareBridge interface + shared listener/decoder base
    BleService.ts                 BLE adapter (react-native-ble-plx), singleton
    hardware/WifiWebSocketHardwareAdapter.ts
    hardware/MockHardwareAdapter.ts
    hardware/index.ts             createHardwareBridge('BLE' | 'WIFI' | 'MOCK')
    StockfishService.ts           UCI game engine interface and fallback search
    LocalStockfish.tsx            bundled Stockfish 19 lite engine in a hidden WebView
  components/GlassPanel.tsx       frosted HUD container
  components/MinimalistBoard.tsx  flat SVG board and pieces
  screens/GameScreen.tsx          HUDs, settings sheet, full game loop
scripts/selfcheck.ts              `npm test`
```

## Game loop

1. A move arrives from a **screen tap**, the **physical board** (ASCII move or occupancy bitmask), or the **engine**.
2. `chess.js` validates it. Illegal moves turn the origin square red.
3. Screen and engine moves are sent to the gantry with `sendMove("e7e5")`. Moves made by hand on the board are not, because the piece is already in place.
4. In Stockfish mode on Black's turn, the engine computes a move, waits for the gantry to report `IDLE`, then sends its move.
5. Input stays locked while the gantry is `MOVING`, `HOMING` or in `ERROR`, while the engine is thinking, and after the game ends.

## Firmware contract (the ESP32 must match this)

| | BLE (service `0xCB00`) | Wi-Fi (`ws://<ip>:81/ws`, JSON) |
|---|---|---|
| Board state → app | `0xCB01` notify: 4–5 ASCII bytes (`e2e4`) **or** 8-byte bitmask | `{"type":"board","data":"e2e4"}` or 16 hex chars |
| Move → ESP32 | `0xCB02` write: `e7e5`, `e7e8q` | `{"type":"move","data":"e7e5"}` |
| Gantry status → app | `0xCB03` notify: 1 byte, 0 IDLE · 1 MOVING · 2 HOMING · 3 ERROR | `{"type":"status","data":0}` |
| System cmd → ESP32 | `0xCB04` write: 1 byte, 1 HOME · 2 E-STOP · 3 REBOOT | `{"type":"cmd","data":2}` |

- **Bitmask layout:** byte 0 is rank 1 and byte 7 is rank 8. Within each byte, bit 0 is the a-file. The app works out the move by finding the legal move that produces that occupancy. A lifted piece matches nothing and is ignored until it is put down.
- BLE advertising must include service UUID `0xCB00` (the scan filters on it).
- When the WebSocket is down, Wi-Fi commands fall back to `POST http://<ip>/api` with the same JSON body.
- Board notifications are ignored unless the gantry is `IDLE`, so the gantry's own moves are not read back as player moves.

## Engine

The app bundles Stockfish 19 lite single-threaded WASM and runs it in a hidden WebView on the phone. The engine receives FEN positions locally and returns UCI moves; those moves go through the selected hardware bridge. With **BLE** selected, the app writes the move to the ESP32's `0xCB02` characteristic. No laptop Stockfish bridge or Wi-Fi engine connection is needed.

If the local engine cannot start, the app labels its built-in 2-ply material search as a fallback. The bundled engine is covered by GPL-3.0; see `assets/stockfish/COPYING.txt`. Adding `react-native-webview` requires rebuilding the development app with `npx expo run:ios --device` once.
