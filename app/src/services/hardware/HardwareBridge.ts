import {
  GANTRY_STATUS_BY_CODE,
  SystemOpcode,
  type BoardPayload,
  type ConnectionStatus,
  type GantryStatus,
} from '../../types/chess';
import { BITMASK_HEX_RE, UCI_MOVE_RE, bytesToHex } from '../../utils/occupancy';

/** Transport-agnostic link to the ESP32. The game loop only ever talks to this. */
export interface IHardwareBridge {
  connect(targetIdOrIp: string): Promise<boolean>;
  disconnect(): Promise<void>;
  sendMove(move: string): Promise<boolean>;
  sendEmergencyStop(): Promise<boolean>;
  sendRehome(): Promise<boolean>;
  onBoardStateChange(callback: (bitmaskOrMove: BoardPayload) => void): () => void;
  onGantryStatusChange(callback: (status: GantryStatus) => void): () => void;
  onConnectionStatusChange(callback: (status: ConnectionStatus) => void): () => void;
  getConnectionStatus(): ConnectionStatus;
}

/** Board bytes from BLE: 4–5 ASCII bytes = move, 8 bytes = occupancy bitmask. */
export function decodeBoardBytes(bytes: Uint8Array): BoardPayload | null {
  const ascii = String.fromCharCode(...bytes).trim().toLowerCase();
  if (UCI_MOVE_RE.test(ascii)) return ascii;
  return bytes.length === 8 ? bytesToHex(bytes) : null;
}

/** Validates a board payload arriving as text (Wi-Fi). */
export function decodeBoardText(text: string): BoardPayload | null {
  const t = text.trim().toLowerCase();
  return UCI_MOVE_RE.test(t) || BITMASK_HEX_RE.test(t) ? t : null;
}

/** Unknown codes are treated as ERROR so the app locks input rather than trusting garbage. */
export function decodeGantryStatus(code: number): GantryStatus {
  return GANTRY_STATUS_BY_CODE[code] ?? 'ERROR';
}

/** Listener plumbing and the shared opcode commands; adapters only implement the wire. */
export abstract class BaseHardwareBridge implements IHardwareBridge {
  private connectionStatus: ConnectionStatus = 'DISCONNECTED';
  private boardListeners = new Set<(p: BoardPayload) => void>();
  private gantryListeners = new Set<(s: GantryStatus) => void>();
  private connectionListeners = new Set<(s: ConnectionStatus) => void>();

  abstract connect(targetIdOrIp: string): Promise<boolean>;
  abstract disconnect(): Promise<void>;
  abstract sendMove(move: string): Promise<boolean>;
  protected abstract sendSystemCommand(opcode: SystemOpcode): Promise<boolean>;

  sendEmergencyStop() {
    return this.sendSystemCommand(SystemOpcode.EMERGENCY_STOP);
  }

  sendRehome() {
    return this.sendSystemCommand(SystemOpcode.HOME_GANTRY);
  }

  sendReboot() {
    return this.sendSystemCommand(SystemOpcode.REBOOT_MCU);
  }

  getConnectionStatus() {
    return this.connectionStatus;
  }

  onBoardStateChange(cb: (p: BoardPayload) => void) {
    this.boardListeners.add(cb);
    return () => void this.boardListeners.delete(cb);
  }

  onGantryStatusChange(cb: (s: GantryStatus) => void) {
    this.gantryListeners.add(cb);
    return () => void this.gantryListeners.delete(cb);
  }

  onConnectionStatusChange(cb: (s: ConnectionStatus) => void) {
    this.connectionListeners.add(cb);
    return () => void this.connectionListeners.delete(cb);
  }

  protected setConnectionStatus(status: ConnectionStatus) {
    if (status === this.connectionStatus) return;
    this.connectionStatus = status;
    this.connectionListeners.forEach((cb) => cb(status));
  }

  protected emitBoard(payload: BoardPayload) {
    this.boardListeners.forEach((cb) => cb(payload));
  }

  protected emitGantry(status: GantryStatus) {
    this.gantryListeners.forEach((cb) => cb(status));
  }
}
