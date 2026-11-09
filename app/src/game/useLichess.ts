import type { Chess } from 'chess.js';
import { fetch as expoFetch } from 'expo/fetch';
import { useCallback, useEffect, useRef, useState, type MutableRefObject } from 'react';

import { LICHESS_URL, LichessError, LichessService, type StreamHandle, type TimeControl } from '../services/LichessService';
import { clearLichessToken, loadLichessToken, saveLichessToken } from '../services/lichessToken';
import {
  type LichessColor,
  isLive,
  lichessMovesToLan,
  localMoves,
  planSync,
  rebuildGame,
  resultText,
} from './LichessGame';

/*
 * Online play against people on Lichess:
 *   your move -> chess.js -> sendMove -> Lichess
 *   Lichess game stream -> opponent's move -> ESP32 first -> chess.js -> board on screen
 */

export type LichessPhase = 'SIGNED_OUT' | 'CHECKING' | 'LOBBY' | 'SEEKING' | 'CHALLENGING' | 'PLAYING' | 'FINISHED';

export interface IncomingChallenge {
  id: string;
  from: string;
  rating?: number;
  timeControl: string;
}

export interface LichessGameInfo {
  id: string;
  myColor: LichessColor;
  opponent: string;
  opponentRating?: number;
  clocks: { w: number; b: number } | null;
  status: string;
  result: string | null;
  opponentGone: boolean;
}

interface Options {
  enabled: boolean;
  chessRef: MutableRefObject<Chess>;
  /**
   * Delivers the opponent's move from Lichess. The screen sends it to the board first and shows it
   * only once the board accepted it, the same way as Stockfish's moves.
   */
  onOpponentMove: (uci: string) => void;
  /** Called after the whole game was reloaded from Lichess, so the screen can redraw. */
  onRebuilt: () => void;
}

const BASE_URL = process.env.EXPO_PUBLIC_LICHESS_URL ?? LICHESS_URL;
const RECONNECT_MS = 5000;
const log = (message: string) => console.log(`[Lichess] ${message}`);

export function useLichess(options: Options) {
  const opts = useRef(options);
  opts.current = options;

  const [phase, setPhaseState] = useState<LichessPhase>('SIGNED_OUT');
  const [account, setAccount] = useState<{ id: string; username: string } | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [incoming, setIncoming] = useState<IncomingChallenge[]>([]);
  const [game, setGame] = useState<LichessGameInfo | null>(null);

  const phaseRef = useRef(phase);
  const setPhase = useCallback((p: LichessPhase) => {
    phaseRef.current = p;
    setPhaseState(p);
  }, []);

  const service = useRef<LichessService | null>(null);
  const accountRef = useRef<{ id: string; username: string } | null>(null);
  const searchHandle = useRef<StreamHandle | null>(null);
  const outgoingId = useRef<string | null>(null);
  const gameStream = useRef<StreamHandle | null>(null);
  const gameIdRef = useRef<string | null>(null);
  const initialFenRef = useRef<string | undefined>(undefined);
  const myColorRef = useRef<LichessColor>('white');
  const confirmedMoves = useRef<string[]>([]);

  const report = useCallback((e: unknown) => {
    const message = e instanceof Error ? e.message : String(e);
    console.warn(`[Lichess] ${message}`);
    setError(message);
  }, []);

  // --- sign in --------------------------------------------------------------------------

  const signIn = useCallback(
    async (token: string, save = true) => {
      const trimmed = token.trim();
      if (!trimmed) return;
      setPhase('CHECKING');
      setError(null);
      const candidate = new LichessService({ token: trimmed, baseUrl: BASE_URL, fetch: expoFetch as never });
      try {
        const me = await candidate.account();
        if (save) await saveLichessToken(trimmed);
        service.current = candidate;
        accountRef.current = me;
        setAccount(me);
        setPhase('LOBBY');
        log(`signed in as ${me.username}`);
      } catch (e) {
        setPhase('SIGNED_OUT');
        report(e);
      }
    },
    [report, setPhase],
  );

  const signOut = useCallback(async () => {
    searchHandle.current?.close();
    gameStream.current?.close();
    service.current = null;
    accountRef.current = null;
    setAccount(null);
    setGame(null);
    setIncoming([]);
    setPhase('SIGNED_OUT');
    await clearLichessToken();
  }, [setPhase]);

  // Use a token saved earlier on this phone.
  useEffect(() => {
    if (!options.enabled || service.current) return;
    void loadLichessToken().then((saved) => {
      if (saved) void signIn(saved, false);
    });
  }, [options.enabled, signIn]);

  // --- the live game ---------------------------------------------------------------------

  /** `full` is set for gameFull (game opened or stream reconnected): always reload from Lichess then. */
  const syncWithLichess = useCallback(
    (state: Record<string, any>, full = false) => {
      const chess = opts.current.chessRef.current;
      let remote: string[];
      try {
        remote = lichessMovesToLan(initialFenRef.current, String(state.moves ?? ''));
      } catch {
        report(new Error('Lichess sent a move list the app could not read.'));
        return;
      }
      confirmedMoves.current = remote;
      const plan = full ? ({ kind: 'rebuild', moves: remote } as const) : planSync(localMoves(chess), remote);
      if (plan.kind === 'apply') {
        log(`move received from Lichess: ${plan.uci}`);
        opts.current.onOpponentMove(plan.uci);
      } else if (plan.kind === 'rebuild') {
        log(`board reloaded from Lichess (${remote.length} moves)`);
        rebuildGame(chess, initialFenRef.current, remote);
        opts.current.onRebuilt();
      }

      const status = String(state.status ?? 'started');
      const finished = !isLive(status);
      setGame((g) =>
        g && {
          ...g,
          clocks: typeof state.wtime === 'number' ? { w: state.wtime, b: state.btime } : g.clocks,
          status,
          result: finished ? resultText(status, state.winner, g.myColor) : null,
        },
      );
      if (finished && phaseRef.current === 'PLAYING') {
        log(`game over: ${status}${state.winner ? `, ${state.winner} won` : ''}`);
        setPhase('FINISHED');
      }
    },
    [report, setPhase],
  );

  const openGame = useCallback(
    (gameId: string, color: LichessColor | undefined, opponent: string | undefined) => {
      const svc = service.current;
      if (!svc || gameIdRef.current === gameId) return;
      searchHandle.current?.close();
      searchHandle.current = null;
      outgoingId.current = null;
      gameStream.current?.close();
      gameIdRef.current = gameId;
      myColorRef.current = color ?? 'white';
      confirmedMoves.current = [];
      initialFenRef.current = undefined;
      setIncoming([]);
      setNotice(null);
      setError(null);
      setGame({
        id: gameId,
        myColor: myColorRef.current,
        opponent: opponent ?? 'Opponent',
        clocks: null,
        status: 'started',
        result: null,
        opponentGone: false,
      });
      setPhase('PLAYING');
      log(`game ${gameId} started, you play ${myColorRef.current}`);

      const connect = () => {
        if (gameIdRef.current !== gameId) return;
        const handle = svc.streamGame(gameId, (event) => {
          if (event.type === 'gameFull') {
            initialFenRef.current = event.initialFen;
            const me = accountRef.current?.id;
            const color: LichessColor = event.black?.id === me ? 'black' : event.white?.id === me ? 'white' : myColorRef.current;
            myColorRef.current = color;
            const them = color === 'white' ? event.black : event.white;
            setGame((g) => g && { ...g, myColor: color, opponent: them?.name ?? g.opponent, opponentRating: them?.rating });
            syncWithLichess(event.state ?? {}, true);
          } else if (event.type === 'gameState') {
            syncWithLichess(event);
          } else if (event.type === 'opponentGone') {
            setGame((g) => g && { ...g, opponentGone: !!event.gone });
          }
        });
        gameStream.current = handle;
        handle.done
          .catch(report)
          .finally(() => {
            // Reopen a dropped stream while the game is still on; gameFull resyncs everything.
            if (gameStream.current === handle && phaseRef.current === 'PLAYING') {
              log('game stream closed early, reconnecting');
              setTimeout(connect, RECONNECT_MS);
            }
          });
      };
      connect();
    },
    [report, setPhase, syncWithLichess],
  );

  // --- incoming events (games starting, challenges) -----------------------------------

  useEffect(() => {
    if (!options.enabled || !account) return;
    let stopped = false;
    let handle: StreamHandle | null = null;
    const connect = () => {
      const svc = service.current;
      if (stopped || !svc) return;
      handle = svc.streamEvents((event) => {
        const me = accountRef.current?.id;
        if (event.type === 'gameStart') {
          const info = event.game ?? {};
          const live = !info.status?.name || isLive(info.status.name);
          if (live) openGame(info.gameId ?? info.id, info.color, info.opponent?.username);
        } else if (event.type === 'challenge') {
          const c = event.challenge ?? {};
          if (c.challenger?.id === me) return;
          const tc = c.timeControl?.type === 'clock' ? c.timeControl.show ?? `${c.timeControl.limit / 60}+${c.timeControl.increment}` : 'Correspondence';
          setIncoming((list) =>
            list.some((x) => x.id === c.id) ? list : [...list, { id: c.id, from: c.challenger?.name ?? '?', rating: c.challenger?.rating, timeControl: tc }],
          );
        } else if (event.type === 'challengeCanceled') {
          setIncoming((list) => list.filter((x) => x.id !== event.challenge?.id));
        } else if (event.type === 'challengeDeclined') {
          const c = event.challenge ?? {};
          setIncoming((list) => list.filter((x) => x.id !== c.id));
          if (c.id && c.id === outgoingId.current) {
            searchHandle.current?.close();
            outgoingId.current = null;
            setNotice(`${c.destUser?.name ?? 'Your friend'} declined the challenge.`);
            setPhase('LOBBY');
          }
        }
      });
      const current = handle;
      current.done.catch(report).finally(() => {
        if (!stopped && handle === current) setTimeout(connect, RECONNECT_MS);
      });
    };
    connect();
    return () => {
      stopped = true;
      handle?.close();
    };
  }, [options.enabled, account, openGame, report, setPhase]);

  // Leaving online mode closes every stream.
  useEffect(() => {
    if (options.enabled) return;
    searchHandle.current?.close();
    gameStream.current?.close();
    gameStream.current = null;
    gameIdRef.current = null;
  }, [options.enabled]);

  // --- lobby actions ----------------------------------------------------------------------

  const findOpponent = useCallback(
    (tc: TimeControl) => {
      const svc = service.current;
      if (!svc || phaseRef.current !== 'LOBBY') return;
      setError(null);
      setNotice(null);
      setPhase('SEEKING');
      const handle = svc.seek(tc);
      searchHandle.current = handle;
      handle.done
        .catch(report)
        .finally(() => {
          if (searchHandle.current === handle && phaseRef.current === 'SEEKING') {
            searchHandle.current = null;
            setNotice('No opponent found. Try again.');
            setPhase('LOBBY');
          }
        });
    },
    [report, setPhase],
  );

  const challengeFriend = useCallback(
    (username: string, tc: TimeControl) => {
      const svc = service.current;
      const name = username.trim();
      if (!svc || !name || phaseRef.current !== 'LOBBY') return;
      setError(null);
      setNotice(null);
      setPhase('CHALLENGING');
      const handle = svc.challenge(name, tc, (id) => {
        outgoingId.current = id;
        log(`challenge ${id} sent to ${name}`);
      });
      searchHandle.current = handle;
      handle.done
        .catch(report)
        .finally(() => {
          if (searchHandle.current === handle && phaseRef.current === 'CHALLENGING') {
            searchHandle.current = null;
            outgoingId.current = null;
            setNotice((n) => n ?? `${name} did not accept the challenge.`);
            setPhase('LOBBY');
          }
        });
    },
    [report, setPhase],
  );

  const cancelSearch = useCallback(() => {
    const id = outgoingId.current;
    searchHandle.current?.close();
    searchHandle.current = null;
    outgoingId.current = null;
    if (id) service.current?.cancelChallenge(id).catch(() => undefined);
    if (phaseRef.current === 'SEEKING' || phaseRef.current === 'CHALLENGING') setPhase('LOBBY');
  }, [setPhase]);

  const answerChallenge = useCallback(
    (id: string, accept: boolean) => {
      setIncoming((list) => list.filter((x) => x.id !== id));
      const svc = service.current;
      if (!svc) return;
      // Accepting starts the game; the event stream then sends gameStart.
      (accept ? svc.acceptChallenge(id) : svc.declineChallenge(id)).catch(report);
    },
    [report],
  );

  // --- in-game actions --------------------------------------------------------------------

  /**
   * Sends a move the player just made. Resolves true once Lichess accepted it; if Lichess refuses it,
   * the board goes back to Lichess's version and this resolves false.
   */
  const sendMove = useCallback(
    async (uci: string): Promise<boolean> => {
      const svc = service.current;
      const id = gameIdRef.current;
      if (!svc || !id) return false;
      try {
        await svc.makeMove(id, uci);
        return true;
      } catch (e) {
        report(e instanceof LichessError && e.code === 'REJECTED' ? new Error(`Lichess refused ${uci}: ${e.message}`) : e);
        rebuildGame(opts.current.chessRef.current, initialFenRef.current, confirmedMoves.current);
        opts.current.onRebuilt();
        return false;
      }
    },
    [report],
  );

  /** Resigns, or aborts if the game has barely started (Lichess only allows abort before both sides moved). */
  const resign = useCallback(() => {
    const svc = service.current;
    const id = gameIdRef.current;
    if (!svc || !id) return;
    const request = confirmedMoves.current.length < 2 ? svc.abort(id) : svc.resign(id);
    request.catch(report);
  }, [report]);

  /** Back to the lobby after a game has finished. */
  const leaveGame = useCallback(() => {
    gameStream.current?.close();
    gameStream.current = null;
    gameIdRef.current = null;
    confirmedMoves.current = [];
    setGame(null);
    if (service.current) setPhase('LOBBY');
  }, [setPhase]);

  return {
    phase,
    account,
    error,
    notice,
    incoming,
    game,
    isMyTurn: (turn: 'w' | 'b') => phase === 'PLAYING' && (turn === 'w') === (myColorRef.current === 'white'),
    signIn,
    signOut,
    findOpponent,
    challengeFriend,
    cancelSearch,
    answerChallenge,
    sendMove,
    resign,
    leaveGame,
    clearError: () => setError(null),
  };
}
