import Slider from '@react-native-community/slider';
import { Chess, type Move, type Square } from 'chess.js';
import { ArrowLeft, Bluetooth, Cpu, House, OctagonX, RotateCcw, Settings, Wifi } from 'lucide-react-native';
import { useCallback, useEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import {
  Alert,
  Animated,
  Modal,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  TextInput,
  View,
  useWindowDimensions,
} from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import { GlassPanel } from '../components/GlassPanel';
import { LichessPanel } from '../components/LichessPanel';
import { MinimalistBoard } from '../components/MinimalistBoard';
import { toAppliedMove } from '../game/ChessGame';
import { requestComputerMove } from '../game/ComputerTurn';
import type { AppliedMove, EngineSource } from '../game/Types';
import { useLichess } from '../game/useLichess';
import { DEFAULT_BLE_NAME } from '../services/BleService';
import { LocalStockfishHost, LocalStockfishTransport } from '../services/LocalStockfish';
import { FALLBACK_LABEL, StockfishService } from '../services/StockfishService';
import { createHardwareBridge } from '../services/hardware';
import { colors, fonts } from '../theme';
import type {
  ConnectionStatus,
  GameMode,
  GantryStatus,
  MoveSource,
  TransportKind,
} from '../types/chess';
import { UCI_MOVE_RE, inferMoveFromOccupancy } from '../utils/occupancy';

const ENGINE_COLOR = 'b';
const START_CLOCK_MS = 10 * 60 * 1000;
const DEFAULT_TARGET: Record<TransportKind, string> = { BLE: DEFAULT_BLE_NAME, WIFI: '192.168.4.1', MOCK: '' };
const wait = (ms: number) => new Promise((r) => setTimeout(r, ms));

type EngineLink = { state: 'CONNECTING' | 'CONNECTED' | 'FALLBACK'; name: string | null };

export function GameScreen({ onExit }: { onExit: () => void }) {
  const { width } = useWindowDimensions();
  const boardSize = Math.min(width - 32, 520);

  // --- game state -------------------------------------------------------------
  const chessRef = useRef(new Chess());
  const chess = chessRef.current;
  const [fen, setFen] = useState(chess.fen()); // re-render trigger; chess.js instance is the source of truth
  const [lastMove, setLastMove] = useState<{ from: Square; to: Square } | null>(null);
  const [selected, setSelected] = useState<Square | null>(null);
  const [rejected, setRejected] = useState<Square | null>(null);
  const [mode, setMode] = useState<GameMode>('OFFLINE_STOCKFISH');
  const [skill, setSkill] = useState(8);
  const [thinking, setThinking] = useState(false);
  const [clocks, setClocks] = useState({ w: START_CLOCK_MS, b: START_CLOCK_MS });

  // --- hardware state ---------------------------------------------------------
  const [transport, setTransport] = useState<TransportKind>('MOCK');
  const [target, setTarget] = useState(DEFAULT_TARGET.MOCK);
  const bridge = useMemo(() => createHardwareBridge(transport), [transport]);
  const [connection, setConnection] = useState<ConnectionStatus>('DISCONNECTED');
  const [gantry, setGantry] = useState<GantryStatus>('IDLE');
  const gantryRef = useRef(gantry);
  gantryRef.current = gantry;
  const [settingsOpen, setSettingsOpen] = useState(false);

  const engine = useMemo(() => new StockfishService(), []);
  useEffect(() => engine.setSkillLevel(skill), [engine, skill]);
  const [localEngine, setLocalEngine] = useState<LocalStockfishTransport | null>(null);
  const [engineLink, setEngineLink] = useState<EngineLink>({ state: 'CONNECTING', name: null });
  const [engineError, setEngineError] = useState<string | null>(null);
  // Last computer move in the shape the ESP32 layer needs: { from: 'e7', to: 'e5', ... }.
  const [computerMove, setComputerMove] = useState<{ move: AppliedMove; source: EngineSource } | null>(null);
  const engineConnectSeq = useRef(0);

  // --- online (Lichess) -----------------------------------------------------------
  const online = mode === 'ONLINE_LICHESS';
  const deliverOpponentRef = useRef<(uci: string) => void>(() => {});
  // The opponent's move from Lichess waits here until the board has accepted it.
  const pendingOpponent = useRef<string | null>(null);
  const sendingOpponent = useRef(false);
  const [opponentError, setOpponentError] = useState<string | null>(null);
  const lichess = useLichess({
    enabled: online,
    chessRef,
    onOpponentMove: (uci) => deliverOpponentRef.current(uci),
    onRebuilt: () => {
      pendingOpponent.current = null;
      setOpponentError(null);
      const game = chessRef.current;
      const last = game.history({ verbose: true }).at(-1);
      setLastMove(last ? { from: last.from, to: last.to } : null);
      setSelected(null);
      setFen(game.fen());
    },
  });
  const lichessRef = useRef(lichess);
  lichessRef.current = lichess;
  const myColor = lichess.game?.myColor === 'black' ? 'b' : 'w';

  // --- derived ----------------------------------------------------------------
  const turn = chess.turn();
  // Online, Lichess decides when time runs out and when the game ends.
  const flagged = online ? null : clocks.w <= 0 ? 'w' : clocks.b <= 0 ? 'b' : null;
  const gameOver = online ? lichess.phase === 'FINISHED' || chess.isGameOver() : chess.isGameOver() || flagged !== null;
  const engineTurn = mode === 'OFFLINE_STOCKFISH' && turn === ENGINE_COLOR; // never online: Lichess forbids engine help
  const onlineLocked = online && !lichess.isMyTurn(turn);
  const inputLocked = gantry !== 'IDLE' || thinking || engineTurn || gameOver || onlineLocked;
  const clockRunning = online
    ? lichess.phase === 'PLAYING' && chess.history().length >= 2
    : chess.history().length > 0 && !gameOver;
  const checkSquare = chess.inCheck()
    ? chess.board().flat().find((p) => p?.type === 'k' && p.color === turn)?.square
    : null;
  const legalTargets = useMemo(
    () => (selected ? chessRef.current.moves({ square: selected, verbose: true }).map((m) => m.to) : []),
    [selected, fen],
  );

  // --- move pipeline: validate -> update board -> replay on gantry --------------
  const applyMove = useCallback(
    (uci: string, source: MoveSource, sendToHardware = true): Move | null => {
      const game = chessRef.current;
      const fromHuman = source === 'SCREEN' || source === 'BOARD';
      if (fromHuman && mode === 'OFFLINE_STOCKFISH' && game.turn() === ENGINE_COLOR) return null;
      if (fromHuman && mode === 'ONLINE_LICHESS' && !lichessRef.current.isMyTurn(game.turn())) return null;
      let move: Move;
      try {
        move = game.move({ from: uci.slice(0, 2), to: uci.slice(2, 4), promotion: uci[4] ?? 'q' });
      } catch {
        console.log(`[Game] Illegal move rejected by chess.js: ${uci} (position unchanged, Stockfish not called)`);
        setRejected(uci.slice(0, 2) as Square);
        return null;
      }
      if (fromHuman) console.log(`[Game] Player move: ${move.lan}. New FEN: ${move.after}`);
      if (source === 'REMOTE') console.log(`[Game] Opponent move applied: ${move.lan}. New FEN: ${move.after}`);
      setRejected(null);
      setSelected(null);
      setLastMove({ from: move.from, to: move.to });
      setFen(game.fen());
      // A move made by hand on the physical board is already there; everything else needs the gantry.
      // Online, our move only goes to the gantry once Lichess accepts it, so a refused move never moves a piece.
      if (fromHuman && mode === 'ONLINE_LICHESS') {
        void lichessRef.current.sendMove(move.lan).then((ok) => ok && source === 'SCREEN' && bridge.sendMove(move.lan));
      } else if (sendToHardware && source !== 'BOARD') void bridge.sendMove(move.lan);
      return move;
    },
    [bridge, mode],
  );

  /** Resolves once the gantry is free (or the link is gone, so nothing will ever report IDLE). */
  const waitForGantryIdle = useCallback(
    () =>
      new Promise<void>((resolve) => {
        if (gantryRef.current === 'IDLE' || bridge.getConnectionStatus() !== 'CONNECTED') return resolve();
        const offs = [
          bridge.onGantryStatusChange((s) => s === 'IDLE' && done()),
          bridge.onConnectionStatusChange((s) => s !== 'CONNECTED' && done()),
        ];
        function done() {
          offs.forEach((off) => off());
          resolve();
        }
      }),
    [bridge],
  );

  // --- online opponent's move: board first, then the screen -----------------------
  // Like Stockfish's moves: the move is sent to the board once it is idle, and the screen only shows it
  // after the board accepted it. If sending fails it stays pending and Retry sends it again.
  const deliverOpponentMove = useCallback(
    async (uci: string) => {
      if (sendingOpponent.current && pendingOpponent.current === uci) return;
      pendingOpponent.current = uci;
      sendingOpponent.current = true;
      setOpponentError(null);
      try {
        await waitForGantryIdle();
        if (pendingOpponent.current !== uci) return; // the game was reloaded from Lichess meanwhile
        const sent = await bridge.sendMove(uci);
        if (pendingOpponent.current !== uci) return;
        if (!sent) {
          console.warn(`[Game] Could not send opponent move ${uci} to the board`);
          setOpponentError(`Could not send ${uci} to the board`);
          return;
        }
        pendingOpponent.current = null;
        if (!applyMove(uci, 'REMOTE', false)) setOpponentError(`Could not apply opponent move ${uci}`);
      } finally {
        sendingOpponent.current = false;
      }
    },
    [bridge, applyMove, waitForGantryIdle],
  );
  deliverOpponentRef.current = (uci) => void deliverOpponentMove(uci);

  // --- hardware subscriptions ---------------------------------------------------
  useEffect(() => {
    const offs = [
      bridge.onConnectionStatusChange((s) => {
        setConnection(s);
        if (s !== 'CONNECTED') setGantry('IDLE');
      }),
      bridge.onGantryStatusChange(setGantry),
    ];
    setConnection(bridge.getConnectionStatus());
    if (transport === 'MOCK') void bridge.connect('');
    return () => {
      offs.forEach((off) => off());
      void bridge.disconnect();
    };
  }, [bridge, transport]);

  useEffect(
    () =>
      bridge.onBoardStateChange((payload) => {
        // The gantry's own moves trip the sensors too; only trust them while it is idle.
        if (gantryRef.current !== 'IDLE') return;
        const uci = UCI_MOVE_RE.test(payload)
          ? payload
          : inferMoveFromOccupancy(chessRef.current.fen(), payload);
        if (uci) applyMove(uci, 'BOARD');
      }),
    [bridge, applyMove],
  );

  // --- engine connection -----------------------------------------------------------
  const connectEngine = useCallback(
    async () => {
      const seq = ++engineConnectSeq.current;
      setEngineLink({ state: 'CONNECTING', name: null });
      engine.detachEngine();
      const transport = new LocalStockfishTransport();
      setLocalEngine(transport);
      const ok = (await transport.connect()) && (await engine.attachEngine(transport));
      if (seq !== engineConnectSeq.current) return transport.close(); // a newer attempt took over
      if (!ok) {
        transport.close();
        setEngineLink({ state: 'FALLBACK', name: null });
        console.warn(`[Stockfish] local engine unavailable: ${FALLBACK_LABEL} will make the computer moves`);
        return;
      }
      transport.onClose(() => {
        engine.detachEngine(transport);
        if (seq === engineConnectSeq.current) setEngineLink({ state: 'FALLBACK', name: null });
        console.warn(`[Stockfish] local engine stopped: ${FALLBACK_LABEL} will make the computer moves`);
      });
      setEngineLink({ state: 'CONNECTED', name: engine.getEngineName() });
    },
    [engine],
  );

  useEffect(() => {
    void connectEngine();
    return () => { ++engineConnectSeq.current; engine.detachEngine(); };
  }, [engine, connectEngine]);

  // --- engine turn: FEN -> Stockfish -> bestmove -> chess.js -> board ----------------
  useEffect(() => {
    // wait for the first connection attempt so a Stockfish game never starts on the fallback by accident
    if (!engineTurn || gameOver || engineError || engineLink.state === 'CONNECTING') return;
    let cancelled = false;
    let searching = true;
    const fenAtStart = fen;
    setThinking(true);
    console.log(`[Game] Computer to move. Current FEN: ${fenAtStart}`);
    (async () => {
      // the floor gives the ESP32 time to report MOVING for the player's move before we check it
      const [reply] = await Promise.all([requestComputerMove(engine, fenAtStart, skill), wait(400)]);
      searching = false;
      await waitForGantryIdle();
      if (cancelled || chessRef.current.fen() !== fenAtStart) {
        console.log(`[Game] dropped stale computer move ${reply.uci}: the position changed`);
        return;
      }
      // Do not advance the displayed game if the BLE write failed or the board disconnected.
      if (!(await bridge.sendMove(reply.uci))) throw new Error('Could not send the computer move to the board');
      if (cancelled || chessRef.current.fen() !== fenAtStart) return;
      const played = applyMove(reply.uci, 'ENGINE', false);
      if (!played) throw new Error(`Could not apply computer move ${reply.uci}`);
      const move = toAppliedMove(played);
      setComputerMove({ move, source: reply.source });
      const by = reply.source === 'fallback' ? FALLBACK_LABEL : 'REAL STOCKFISH';
      console.log(`[Game] Computer move applied: ${move.uci} (${by})`, { from: move.from, to: move.to });
    })()
      .catch((e) => {
        if (cancelled) return;
        console.warn('[engine]', e);
        setEngineError(e instanceof Error ? e.message : String(e));
      })
      .finally(() => !cancelled && setThinking(false));
    return () => {
      cancelled = true;
      if (searching) engine.stop(); // its late bestmove is dropped by the check above
      setThinking(false);
    };
  }, [fen, engineTurn, gameOver, engineError, engineLink.state, engine, skill, bridge, applyMove, waitForGantryIdle]);

  // Back in the online lobby (or just signed in), start from a clean board.
  useEffect(() => {
    if (!online || (lichess.phase !== 'LOBBY' && lichess.phase !== 'SIGNED_OUT')) return;
    pendingOpponent.current = null;
    setOpponentError(null);
    const game = chessRef.current;
    if (game.history().length === 0) return;
    game.reset();
    setFen(game.fen());
    setLastMove(null);
    setSelected(null);
    setRejected(null);
    setClocks({ w: START_CLOCK_MS, b: START_CLOCK_MS });
  }, [online, lichess.phase]);

  // --- clocks ----------------------------------------------------------------------
  const lichessClocks = lichess.game?.clocks;
  useEffect(() => {
    if (online && lichessClocks) setClocks(lichessClocks);
  }, [online, lichessClocks]);

  useEffect(() => {
    if (!clockRunning) return;
    let last = Date.now();
    const id = setInterval(() => {
      const now = Date.now();
      const elapsed = now - last;
      last = now;
      setClocks((c) => ({ ...c, [turn]: Math.max(0, c[turn] - elapsed) }));
    }, 200);
    return () => clearInterval(id);
  }, [clockRunning, turn]);

  // --- actions ---------------------------------------------------------------------
  const onSquarePress = (sq: Square) => {
    setRejected(null);
    if (selected && legalTargets.includes(sq)) {
      applyMove(selected + sq, 'SCREEN'); // shortcut: on-screen promotion is always a queen; add a picker if needed
      return;
    }
    setSelected(chess.get(sq)?.color === turn ? sq : null);
  };

  const newGame = () => {
    chess.reset();
    setFen(chess.fen());
    setLastMove(null);
    setSelected(null);
    setRejected(null);
    setClocks({ w: START_CLOCK_MS, b: START_CLOCK_MS });
    setEngineError(null);
    setComputerMove(null);
    void engine.newGame(); // stops a search still running and sends ucinewgame
  };

  const chooseTransport = (kind: TransportKind) => {
    setTransport(kind);
    setTarget(DEFAULT_TARGET[kind]);
  };

  const connect = async () => {
    if (await bridge.connect(target)) return;
    Alert.alert(
      'Connection failed',
      transport === 'BLE'
        ? 'No Project 64 board found. Check Bluetooth is on and the board is advertising. BLE needs a development build, not Expo Go.'
        : `Could not reach ws://${target}:81/ws`,
    );
  };

  const hardwareCommand = async (send: () => Promise<boolean>, label: string) => {
    if (!(await send())) Alert.alert(`${label} failed`, 'The board is not connected.');
  };

  // --- render ------------------------------------------------------------------------
  const opponentName = online
    ? `${lichess.game?.opponent ?? 'Online opponent'}${lichess.game?.opponentRating ? ` (${lichess.game.opponentRating})` : ''}`
    : mode === 'OFFLINE_STOCKFISH'
      ? `Stockfish Level ${skill}`
      : 'Player 2';
  // The player is always at the bottom; online as Black that means the board and clocks flip.
  const bottomColor = online ? myColor : 'w';
  const topColor = bottomColor === 'w' ? 'b' : 'w';
  const colorName = (c: 'w' | 'b') => (c === 'w' ? 'White' : 'Black');
  const myName = online && lichess.account ? lichess.account.username : 'You';
  const engineLabel =
    engineLink.state === 'CONNECTED'
      ? `Real Stockfish connected · ${engineLink.name ?? 'UCI engine'}`
      : engineLink.state === 'CONNECTING'
        ? 'Connecting to Stockfish…'
        : FALLBACK_LABEL;
  const usingFallback = mode === 'OFFLINE_STOCKFISH' && engineLink.state === 'FALLBACK';
  const status = online
    ? onlineStatusText({ chess, gantry, lichess, myTurn: lichess.isMyTurn(turn), opponentError })
    : statusText({ chess, flagged, thinking, gantry, turn, engineError, computerMove, usingFallback });
  const statusAlarm =
    gantry === 'ERROR' || !!engineError || usingFallback || computerMove?.source === 'fallback' || (online && !!opponentError);

  return (
    <SafeAreaView style={styles.screen}>
      {/* Scrolls only when the content is taller than the phone (e.g. the online lobby with challenges). */}
      <ScrollView contentContainerStyle={styles.content} keyboardShouldPersistTaps="handled">
        <GlassPanel glow={connection === 'CONNECTED'}>
          <View style={styles.row}>
            <PlayerTag
              name={opponentName}
              side={mode === 'OFFLINE_STOCKFISH' ? `Black · ${engineLabel}` : online ? `${colorName(topColor)} · Lichess` : 'Black'}
              alarm={usingFallback}
            />
            <Clock ms={clocks[topColor]} active={clockRunning && turn === topColor} />
          </View>
          <View style={[styles.row, styles.gapTop]}>
            <LinkBadge transport={transport} connection={connection} gantry={gantry} />
            <View style={styles.row}>
              <IconButton label="Settings" onPress={() => setSettingsOpen(true)}>
                <Settings size={18} color={colors.text} />
              </IconButton>
              <IconButton label="Back to home" onPress={onExit}>
                <ArrowLeft size={18} color={colors.text} />
              </IconButton>
            </View>
          </View>
        </GlassPanel>

        <View style={styles.boardWrap}>
          <MinimalistBoard
            fen={fen}
            size={boardSize}
            selected={selected}
            legalTargets={legalTargets}
            lastMove={lastMove}
            dangerSquare={rejected ?? checkSquare}
            disabled={inputLocked}
            onSquarePress={onSquarePress}
            flipped={bottomColor === 'b'}
          />
        </View>

        <GlassPanel>
          <View style={styles.row}>
            <PlayerTag name={myName} side={colorName(bottomColor)} />
            <Clock ms={clocks[bottomColor]} active={clockRunning && turn === bottomColor} />
          </View>
          <Text style={[styles.status, statusAlarm && { color: colors.danger }]}>{status}</Text>
          <View style={[styles.row, styles.gapTop]}>
            <ActionButton
              label="E-STOP"
              danger
              icon={<OctagonX size={18} color="#fff" />}
              onPress={() => hardwareCommand(() => bridge.sendEmergencyStop(), 'Emergency stop')}
            />
            <ActionButton
              label="Re-home"
              icon={<House size={18} color={colors.text} />}
              disabled={gantry === 'MOVING' || gantry === 'HOMING'}
              onPress={() => hardwareCommand(() => bridge.sendRehome(), 'Re-home')}
            />
            {!online && <ActionButton label="New" icon={<RotateCcw size={18} color={colors.text} />} onPress={newGame} />}
            {!online && engineError && <ActionButton label="Retry" onPress={() => setEngineError(null)} />}
            {online && opponentError && pendingOpponent.current && (
              <ActionButton label="Retry" onPress={() => void deliverOpponentMove(pendingOpponent.current ?? '')} />
            )}
          </View>
          {online && <LichessPanel lichess={lichess} />}
          {mode === 'OFFLINE_STOCKFISH' && (
            <View style={styles.gapTop}>
              <Text style={styles.label}>Engine difficulty · {skill}</Text>
              <Slider
                minimumValue={1}
                maximumValue={20}
                step={1}
                value={skill}
                onSlidingComplete={setSkill}
                minimumTrackTintColor={colors.ok}
                maximumTrackTintColor={colors.glassBorder}
                thumbTintColor={colors.text}
                accessibilityLabel="Engine difficulty"
              />
            </View>
          )}
        </GlassPanel>

        <Modal visible={settingsOpen} transparent animationType="fade" onRequestClose={() => setSettingsOpen(false)}>
          <View style={styles.backdrop}>
            <GlassPanel style={styles.sheet} glow={connection === 'CONNECTED'}>
              <Text style={styles.title}>Settings</Text>

              <Text style={styles.label}>Game mode</Text>
              <Segmented
                value={mode}
                onChange={setMode}
                options={[
                  ['OFFLINE_PASS_AND_PLAY', 'Pass & Play'],
                  ['OFFLINE_STOCKFISH', 'Stockfish'],
                  ['ONLINE_LICHESS', 'Online'],
                ]}
              />

              <Text style={[styles.label, styles.gapTop]}>Hardware link</Text>
              <Segmented
                value={transport}
                onChange={chooseTransport}
                options={[
                  ['BLE', 'BLE'],
                  ['WIFI', 'Wi-Fi (LAN)'],
                  ['MOCK', 'Simulator Mock'],
                ]}
              />
              {transport !== 'MOCK' && (
                <TextInput
                  style={styles.input}
                  value={target}
                  onChangeText={setTarget}
                  placeholder={transport === 'BLE' ? 'Device name or id' : 'ESP32 IP address'}
                  placeholderTextColor={colors.textMuted}
                  autoCapitalize="none"
                  autoCorrect={false}
                  keyboardType={transport === 'WIFI' ? 'numbers-and-punctuation' : 'default'}
                  accessibilityLabel={transport === 'BLE' ? 'BLE device name' : 'ESP32 IP address'}
                />
              )}
              <View style={[styles.row, styles.gapTop]}>
                {connection === 'CONNECTED' ? (
                  <ActionButton label="Disconnect" onPress={() => bridge.disconnect()} />
                ) : (
                  <ActionButton
                    label={connection === 'DISCONNECTED' ? 'Connect' : `${titleCase(connection)}…`}
                    disabled={connection !== 'DISCONNECTED'}
                    onPress={connect}
                  />
                )}
                <ActionButton label="Done" onPress={() => setSettingsOpen(false)} />
              </View>

              <Text style={[styles.label, styles.gapTop]}>On-device Stockfish · {engineLabel}</Text>
              <View style={[styles.row, styles.gapTop]}>
                <ActionButton
                  label={engineLink.state === 'CONNECTED' ? 'Reconnect engine' : 'Connect engine'}
                  disabled={engineLink.state === 'CONNECTING'}
                  onPress={() => { void connectEngine(); }}
                />
              </View>
            </GlassPanel>
          </View>
        </Modal>
        {localEngine && <LocalStockfishHost key={engineConnectSeq.current} transport={localEngine} />}
      </ScrollView>
    </SafeAreaView>
  );
}

function statusText({
  chess,
  flagged,
  thinking,
  gantry,
  turn,
  engineError,
  computerMove,
  usingFallback,
}: {
  chess: Chess;
  flagged: 'w' | 'b' | null;
  thinking: boolean;
  gantry: GantryStatus;
  turn: 'w' | 'b';
  engineError: string | null;
  computerMove: { move: AppliedMove; source: EngineSource } | null;
  usingFallback: boolean;
}) {
  const side = (c: 'w' | 'b') => (c === 'w' ? 'White' : 'Black');
  const other = turn === 'w' ? 'b' : 'w';
  if (flagged) return `${side(flagged === 'w' ? 'b' : 'w')} wins on time`;
  if (chess.isCheckmate()) return `Checkmate · ${side(other)} wins`;
  if (chess.isDraw()) return 'Draw';
  if (gantry === 'ERROR') return 'Gantry fault: clear the jam, then Re-home';
  if (gantry === 'HOMING') return 'Homing gantry…';
  if (gantry === 'MOVING') return 'Gantry moving piece…';
  if (engineError) return `Engine error: ${engineError} · tap Retry`;
  if (thinking) return usingFallback ? `${FALLBACK_LABEL} is thinking…` : 'Stockfish is thinking…';
  const engineName = computerMove?.source === 'fallback' ? FALLBACK_LABEL : 'Stockfish';
  const reply = computerMove ? ` · ${engineName} played ${computerMove.move.from}→${computerMove.move.to}` : '';
  return `${side(turn)} to move${chess.inCheck() ? ' · Check' : ''}${reply}`;
}

function onlineStatusText({
  chess,
  gantry,
  lichess,
  myTurn,
  opponentError,
}: {
  chess: Chess;
  gantry: GantryStatus;
  lichess: ReturnType<typeof useLichess>;
  myTurn: boolean;
  opponentError: string | null;
}) {
  switch (lichess.phase) {
    case 'SIGNED_OUT':
    case 'CHECKING':
      return 'Sign in to Lichess to play people online';
    case 'LOBBY':
      return 'Find a random opponent or challenge a friend';
    case 'SEEKING':
      return 'Looking for an opponent on Lichess…';
    case 'CHALLENGING':
      return 'Waiting for your friend to accept…';
    case 'FINISHED':
      return lichess.game?.result ?? 'Game over';
  }
  if (gantry === 'ERROR') return 'Gantry fault: clear the jam, then Re-home';
  if (opponentError) return `${opponentError} · tap Retry`;
  if (gantry === 'MOVING') return 'Gantry moving piece…';
  const check = chess.inCheck() ? ' · Check' : '';
  return myTurn ? `Your move${check}` : `Waiting for ${lichess.game?.opponent ?? 'opponent'}…${check}`;
}

const titleCase = (s: string) => s[0] + s.slice(1).toLowerCase();

function formatClock(ms: number) {
  const total = Math.ceil(ms / 1000);
  return `${String(Math.floor(total / 60)).padStart(2, '0')}:${String(total % 60).padStart(2, '0')}`;
}

function PlayerTag({ name, side, alarm }: { name: string; side: string; alarm?: boolean }) {
  return (
    <View style={styles.tag}>
      <Text style={styles.name}>{name}</Text>
      <Text style={[styles.label, alarm && { color: colors.danger, fontWeight: '700' }]}>{side}</Text>
    </View>
  );
}

function Clock({ ms, active }: { ms: number; active: boolean }) {
  return (
    <View style={[styles.clock, active && styles.clockActive]}>
      <Text style={[styles.clockText, ms < 30000 && { color: colors.danger }]}>{formatClock(ms)}</Text>
    </View>
  );
}

function LinkBadge({
  transport,
  connection,
  gantry,
}: {
  transport: TransportKind;
  connection: ConnectionStatus;
  gantry: GantryStatus;
}) {
  const pulse = useRef(new Animated.Value(1)).current;
  const live = connection !== 'DISCONNECTED';
  useEffect(() => {
    if (!live) return;
    const loop = Animated.loop(
      Animated.sequence([
        Animated.timing(pulse, { toValue: 0.25, duration: 700, useNativeDriver: true }),
        Animated.timing(pulse, { toValue: 1, duration: 700, useNativeDriver: true }),
      ]),
    );
    loop.start();
    return () => {
      loop.stop();
      pulse.setValue(1);
    };
  }, [live, pulse]);

  const dotColor =
    connection === 'CONNECTED' ? (gantry === 'ERROR' ? colors.danger : colors.ok) : live ? colors.warn : colors.textMuted;
  const Icon = transport === 'BLE' ? Bluetooth : transport === 'WIFI' ? Wifi : Cpu;
  const label = connection === 'CONNECTED' ? `${titleCase(gantry)}` : titleCase(connection);

  return (
    <View style={styles.badge} accessibilityLabel={`${transport} ${connection}, gantry ${gantry}`}>
      <Animated.View style={[styles.dot, { backgroundColor: dotColor, opacity: pulse }]} />
      <Icon size={14} color={colors.textMuted} />
      <Text style={styles.badgeText}>{label}</Text>
    </View>
  );
}

function IconButton({ label, onPress, children }: { label: string; onPress: () => void; children: ReactNode }) {
  return (
    <Pressable onPress={onPress} accessibilityRole="button" accessibilityLabel={label} hitSlop={8} style={styles.iconBtn}>
      {children}
    </Pressable>
  );
}

function ActionButton({
  label,
  icon,
  danger,
  disabled,
  onPress,
}: {
  label: string;
  icon?: ReactNode;
  danger?: boolean;
  disabled?: boolean;
  onPress: () => void;
}) {
  return (
    <Pressable
      onPress={onPress}
      disabled={disabled}
      accessibilityRole="button"
      accessibilityLabel={label}
      accessibilityState={{ disabled }}
      style={({ pressed }) => [
        styles.btn,
        danger && styles.btnDanger,
        (pressed || disabled) && { opacity: disabled ? 0.4 : 0.7 },
      ]}
    >
      {icon}
      <Text style={styles.btnText}>{label}</Text>
    </Pressable>
  );
}

function Segmented<T extends string>({
  value,
  options,
  onChange,
}: {
  value: T;
  options: [T, string][];
  onChange: (v: T) => void;
}) {
  return (
    <View style={styles.segmented} accessibilityRole="radiogroup">
      {options.map(([v, label]) => (
        <Pressable
          key={v}
          onPress={() => onChange(v)}
          accessibilityRole="radio"
          accessibilityState={{ selected: v === value }}
          style={[styles.segment, v === value && styles.segmentOn]}
        >
          <Text style={styles.btnText}>{label}</Text>
        </Pressable>
      ))}
    </View>
  );
}

const styles = StyleSheet.create({
  screen: { flex: 1, backgroundColor: colors.background },
  content: { flexGrow: 1, paddingHorizontal: 16, paddingVertical: 12, gap: 12, justifyContent: 'center' },
  row: { flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', gap: 8 },
  gapTop: { marginTop: 12 },
  boardWrap: { alignItems: 'center' },
  tag: { flexShrink: 1 },
  name: { color: colors.text, fontFamily: fonts.sans, fontSize: 16, fontWeight: '600' },
  label: { color: colors.textMuted, fontFamily: fonts.sans, fontSize: 12, marginBottom: 4 },
  title: { color: colors.text, fontFamily: fonts.sans, fontSize: 20, fontWeight: '700', marginBottom: 12 },
  status: { color: colors.text, fontFamily: fonts.sans, fontSize: 13, marginTop: 10 },
  clock: {
    paddingHorizontal: 12,
    paddingVertical: 6,
    borderRadius: 10,
    borderWidth: 1,
    borderColor: colors.glassBorder,
  },
  clockActive: { borderColor: colors.glowConnected, backgroundColor: 'rgba(255, 255, 255, 0.08)' },
  clockText: { color: colors.text, fontFamily: fonts.mono, fontSize: 22, fontVariant: ['tabular-nums'] },
  badge: { flexDirection: 'row', alignItems: 'center', gap: 6 },
  dot: { width: 8, height: 8, borderRadius: 4 },
  badgeText: { color: colors.textMuted, fontFamily: fonts.mono, fontSize: 12 },
  iconBtn: { padding: 6, borderRadius: 10, borderWidth: 1, borderColor: colors.glassBorder },
  btn: {
    flex: 1,
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
    gap: 6,
    minHeight: 44,
    borderRadius: 12,
    borderWidth: 1,
    borderColor: colors.glassBorder,
    backgroundColor: 'rgba(255, 255, 255, 0.08)',
  },
  btnDanger: { backgroundColor: colors.danger, borderColor: 'rgba(255, 255, 255, 0.25)' },
  btnText: { color: colors.text, fontFamily: fonts.sans, fontSize: 13, fontWeight: '600' },
  backdrop: { flex: 1, justifyContent: 'center', padding: 16, backgroundColor: 'rgba(0, 0, 0, 0.45)' },
  sheet: { alignSelf: 'stretch' },
  segmented: { flexDirection: 'row', gap: 6 },
  segment: {
    flex: 1,
    minHeight: 40,
    alignItems: 'center',
    justifyContent: 'center',
    borderRadius: 10,
    borderWidth: 1,
    borderColor: colors.glassBorder,
  },
  segmentOn: { backgroundColor: 'rgba(76, 175, 80, 0.3)', borderColor: colors.glowConnected },
  input: {
    marginTop: 10,
    minHeight: 44,
    paddingHorizontal: 12,
    borderRadius: 10,
    borderWidth: 1,
    borderColor: colors.glassBorder,
    color: colors.text,
    fontFamily: fonts.mono,
    backgroundColor: 'rgba(255, 255, 255, 0.08)',
  },
});
