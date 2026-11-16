# Native migration phases 3 and 4 implementation plan

## Scope and architecture

Finish the independent native app using Objective-C++ and C++ only. Existing `Core/ChessGame` and `Core/StockfishEngine` remain the rules and engine authorities; UIKit owns presentation, Foundation networking owns HTTP/WebSocket/NDJSON, CoreBluetooth owns BLE, and Security owns token storage. Preserve the Expo app as a migration reference. Do not introduce JavaScript, Swift, WebViews, remote engine services, or new generated bitmap dependencies.

Reference behavior comes from `src/screens/GameScreen.tsx`, `HomeScreen.tsx`, `components/MinimalistBoard.tsx`, `LichessPanel.tsx`, `services/{BleService,LichessService}.ts`, `services/hardware/*`, `game/useLichess.ts`, and `game/LichessGame.ts`. Port intended behavior while fixing stale asynchronous completions and occupancy ambiguity rather than reproducing those weaknesses.

## Ownership and integration order

1. Coordinator establishes `App/P64GameSession.h` and confirms the online service header with the services implementer before parallel work begins.
2. Hardware implementer owns `Services/Hardware/` and `Interfaces/P64HardwareBridge.h`.
3. Online implementer owns `Services/Online/`, including the Lichess client, NDJSON framing, and Keychain store.
4. UI implementer owns new UIKit files in `App/`, excluding `main.mm`, the app delegate, and `P64GameSession.*`.
5. Coordinator owns the game session implementation, app launch wiring, CMake, Info.plist, tests, README, integration, and verification. Implementers must not edit each other's files.

All service callbacks and session state notifications occur on the main queue. Blocking engine search runs on a worker queue. Service implementations may use private serial delegate queues but marshal published callbacks to main. No controller directly changes the C++ game or calls the engine/hardware/client.

## Shared session contract

Use a Foundation Objective-C header that UIKit can consume without including C++ types. Define game mode (pass-and-play, Stockfish, online) and transport (mock, BLE, Wi-Fi) enums. One `onChange` block tells the attached controller to reread presentation state; attach weakly and detach when the controller/session relationship ends.

Expose FEN; selected square; legal target squares; last move origin/destination; checked/rejected square; board orientation; input lock; status/error text; player names; white/black clock seconds; mode; transport; target; engine skill; connection and gantry status; username; lobby phase; incoming challenge dictionaries. Optional additional engine status and last SAN/UCI help match the HUD.

Expose `tapSquare:`, `playUCI:`, `newGame`, mode/transport/target/skill setters, connect/disconnect, home/emergency-stop/reboot, and retry. Expose `promotionChoices` plus `choosePromotion:`; never silently select a queen. Expose `inferredMoveChoices` ambiguous inferred UCI candidates plus `chooseInferredMove:` or use the same explicit move-choice presentation. Candidate selection must revalidate against the current position revision.

Online session methods cover token sign-in/sign-out, seek by minutes/increment, challenge username, accept/decline challenge ID, cancel search, resign/abort, and return to lobby. Token is supplied only through sign-in and never returned in UI snapshots or logs. Challenge dictionaries should consistently use `id`, `username`, optional `rating`, and `timeControl`; agree exact keys with UI before coding. The frozen `App/P64GameSession.h` is authoritative: use `setGameMode:`, `setHardwareTransport:`, `loginWithToken:`, `cancelMatchmaking`, `answerChallenge:accept:`, `resignOrAbort`, and `leaveOnlineGame` exactly as declared; do not define competing APIs. `Services/Online/P64LichessService.h` is likewise frozen.

## Phase 3: hardware

Implement `P64BLEHardwareBridge`, `P64WiFiHardwareBridge`, and `P64MockHardwareBridge` under `Services/Hardware/`, conforming to the existing bridge protocol. A small factory may map the session transport enum to concrete implementations. Keep wire values unchanged: CB00 service, CB01 board notifications, CB02 move writes, CB03 gantry notifications/read, CB04 command writes; UUID expansion uses Bluetooth's standard base UUID. Home=1, emergency stop=2, reboot=3; Idle=0, Moving=1, Homing=2, Error=3, and unknown status means Error.

BLE: lazily initialize CBCentralManager; handle powered-off/unauthorized/unsupported states, bounded scan and connection timeouts, name/identifier matching, service and characteristic discovery, notification subscription success, and disconnection. Declare connected only when required characteristics and subscriptions are usable. Write ASCII UCI and single-byte commands with response, completing requests from the delegate acknowledgement. Serialize writes or track each acknowledgement safely. Disconnect fails every pending connect/write exactly once and invalidates timers and callbacks.

Wi-Fi: preserve `ws://<host>:81/ws`, outgoing JSON `{type:move,data:e2e4}` / `{type:cmd,data:2}`, incoming board/status envelopes, and HTTP POST `http://<host>/api` fallback when the socket is unavailable. Use NSURLSessionWebSocketTask with an opening timeout, receive loop, delegate close/error handling, and send completion. Accept a carefully validated host, including a deliberate loopback test form if needed; reject credentials, fragments, arbitrary paths, and malformed ports. HTTP fallback requires a retained current target and a 2xx response; disconnect or transport replacement must cancel/invalidate it. WebSocket send completion indicates transport acceptance, never completed physical movement. Avoid automatically retrying an uncertain write because that could execute a move twice.

Mock: simulate connection delay, move duration, homing duration, emergency stop, reboot, and faults; provide explicit board-payload injection for deterministic tests. Cancel all timers on disconnect and reject stale timer completions.

Keep board data lossless at the bridge boundary. BLE emits 4–5 ASCII UCI bytes or exactly eight occupancy bytes. Wi-Fi text occupancy is sixteen hex characters encoding byte 0=rank 1 through byte 7=rank 8; convert each byte explicitly to `uint64_t(byte) << (8*index)`, never parse the whole string as a big-endian integer. Reject malformed input. Only the session interprets legal moves.

## Phase 3: Lichess and credentials

Create a configurable `P64LichessService` initialized with token and base URL (production default HTTPS lichess.org; controlled local server for tests). Publish a cancellable stream protocol and JSON event callbacks plus stream completion errors. Methods: account, make move, resign, abort, accept/decline/cancel challenge, event stream, game stream, seek stream, and challenge keep-alive stream with challenge-ID callback. These map to the same Board API paths and form fields in `src/services/LichessService.ts`.

Use NSURLSessionDataDelegate incremental data delivery for real streaming. Buffer bytes across chunks, split at newline, decode UTF-8 only after framing, ignore blank keepalives and malformed individual JSON lines, handle final unterminated lines, and bound pending line memory. Each stream has its own identity and buffer. Completion and cancellation are exactly once; canceled stream events cannot reach the active session. Close seek/challenge tasks to cancel keep-alive searches.

Serialize short HTTP requests. Validate status before accepting stream data. Map 401 to rejected token, 429 to shared 60-second cooldown, other non-2xx responses to readable server errors, and transport failures to network errors. The cooldown applies to streams and requests; never log tokens or Authorization. Percent-encode usernames, IDs, UCI, and form fields using the correct URL-component rules. Do not let a held streaming connection block the short request queue.

Provide `P64TokenStore` backed by generic-password Keychain items, a stable app-specific service/account, explicit load/save/delete error handling, and device-local accessibility. Save after account validation. Sign-out cancels streams and reconnect timers and removes the token. Do not copy tokens to NSUserDefaults, files, fixtures, screenshots, or logs.

## Session orchestration and safety invariants

Maintain main-queue game revision, hardware generation, and online generation/game ID. Capture these plus expected FEN/ply for asynchronous operations; compare before any state mutation. Reset, mode change, transport replacement, disconnect, sign-out, game replacement, and authoritative rebuild invalidate the relevant outstanding work. Increment generations before canceling tasks because cancellation can invoke callbacks immediately. Pending request/stream identity also matters when two operations share a generation.

Input is locked during gantry Moving/Homing/Error, a pending hardware delivery, engine thinking/turn, game over or offline clock flag, online opponent turn, lobby/checking, and uncertain online reconciliation. Ignore physical sensor notifications while the gantry is active or delivery is pending; its own replay must not become a new human move. A physical BOARD move is already on the board and must not be echoed back to the gantry. Invalid input leaves the position unchanged and never triggers engine/hardware work.

Occupancy inference uses `ChessGame::inferMoves`. Zero matches means transient/unrecognized sensor state, one means a possible move, and multiple means explicit choice. Do not select the first capture/promotion candidate. Ignore identical current occupancy. Store ambiguity with revision; clear it on any intervening move/reset/disconnect.

For offline screen and engine delivery, validate against a copy first and define a consistent hardware acceptance boundary; hardware failure must remain visible with retry and must not silently diverge. Preserve the strict remote boundary: queue the next remote move, wait for connected/Idle hardware, send once, and apply it to the displayed C++ game only after successful hardware write and current revision checks. Failure keeps the move pending with Retry. A write acknowledgement is distinct from subsequent Idle and input stays locked through motion.

Online mode never starts Stockfish search, fallback evaluation, hints, or automatic engine moves. Cancel engine work on entry and discard any late result. Maintain initial FEN, normalized authoritative moves, locally accepted moves, and any pending local/remote delivery separately. Convert standard king-to-rook castling notation when needed. Reject unsupported variants instead of interpreting them as standard chess.

On gameFull/gameState, validate the complete remote move list using a temporary ChessGame before changing state. Identical prefix is a no-op; a one-ply own-move lag waits for confirmation; a single new opponent move uses the hardware-first queue. Deduplicate repeated stream updates while that move is pending. Multiple new moves need ordered catch-up or an explicit physical resynchronization requirement; never replay a backlog simultaneously. Use the frozen `alignmentRequired` and `confirmBoardAlignment` session API for real-board reconciliation. A divergent/full reconnect snapshot can rebuild logical state only with a visible physical reconciliation boundary; do not imply the actual board was repositioned. Store a validated authoritative snapshot for rejection recovery. Server-rejected local moves restore that snapshot and flag any physical discrepancy for a board move already made by hand. Online screen moves must not reach hardware before Lichess accepts them.

Reconnect event/game streams with bounded delayed retries guarded by generation/identity. Stop retries after sign-out/mode exit/finished game or authentication failure; respect 429 cooldown. Seek/challenge cancellation must not revive the lobby operation through a stale completion. Incoming challenge deduplication and cancel/decline events update the same ID-indexed collection.

Offline clocks start at 10 minutes and decrement using monotonic elapsed time, not assumed timer intervals; freeze correctly for game end, reset, and app lifecycle. Online clocks derive from server millisecond values and locally interpolate for display without adjudicating a remote result. Flip board/player clock placement for a black online player.

## Phase 4: UIKit presentation

Build a dark home screen with Project 64 title, Autonomous chessboard subtitle, and Play Game. Use a native navigation/controller flow retaining the session as intended when returning home. Game layout is safe-area-aware, scrollable on compact displays, and usable on iPad. Port the theme values from `src/theme.ts`: background #0D1117, squares #38404B/#E2DCD2, light/dark pieces #FAFAF7/#14171C, green legal/connection, red danger, muted secondary text.

Draw an 8x8 native board with clear contrasting pieces (Core Graphics paths or native text symbols), square labels, selection/last-move/check/rejection highlights, legal-target dots, orientation-aware hit testing, and accessibility labels containing square and piece. Use session FEN and immutable presentation data. Taps select then move; promotion shows q/r/b/n choices. Ambiguous physical moves show explicit candidates. Board interaction reflects session input lock.

Show player/opponent names and clocks, hardware link and gantry badge, engine/thinking/error/result status, Settings/Home, connect/disconnect, home gantry, emergency stop, new game, and relevant Retry. Emergency stop remains reachable while ordinary input is locked. Settings select the three modes, three transports, BLE name/Wi-Fi target, and engine skill 0–20. Online panel supports secure token entry, sign-in/out, time control, random seek, username challenge, incoming accept/decline, cancel, active-game opponent/status, resign or early abort, result, and back to lobby. Wire every visible control to the session rather than placeholder actions.

## Build integration and validation

Coordinator adds all `.mm` sources to the iOS CMake target and links UIKit, Foundation, CoreBluetooth, and Security with ARC. Add Bluetooth usage and local network usage descriptions. Permit deliberate LAN HTTP/WebSocket access with the narrowest functional ATS configuration and document it; production Lichess uses HTTPS. Keep vendored engine NNUE resources and license/provenance packaging intact. Host C++ tests must continue to build without UIKit dependencies.

Run existing host ChessGame and Stockfish tests, generate/build the iOS simulator target, and run meaningful new native integration tests where Foundation is available. Use mock bridges and a controlled local HTTP/WebSocket/NDJSON server; do not sign in to a real account, seek real opponents, submit real moves, or accept challenges during verification.

Required regression cases: malformed board payloads/unknown status; byte-order occupancy; ambiguous promotions/captures; illegal moves; input locks; own gantry sensor echo; remote move withheld on failed hardware write and accepted once on retry; repeated remote snapshots during pending delivery; reset/disconnect/mode switch while search/write/HTTP/stream/reconnect is pending; stale connect and timer completions; ordered remote catch-up; divergent reconnect requiring physical reconciliation; online local rejection; no engine search in online mode; NDJSON chunk boundaries splitting UTF-8/newline; empty/malformed/final lines; stream cancellation; 401/429/500; Keychain lifecycle; challenge cancel race. UI smoke checks cover home/play/back, all mode/transport controls, board flips/taps, promotion, online lobby states, emergency stop while locked, compact layout, and VoiceOver labels.

Document actual executed checks separately from manual/device follow-ups. Real BLE discovery/notifications/write acknowledgement, LAN permissions and physical gantry behavior require hardware validation; simulator success is not evidence of these. Finish with native README instructions, current architecture, test commands, known platform limits, and reproducible simulator build paths.
