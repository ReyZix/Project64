export type GameMode = 'OFFLINE_PASS_AND_PLAY' | 'OFFLINE_STOCKFISH' | 'ONLINE_LICHESS';

/** Link status of the active hardware transport (BLE, Wi-Fi or Mock). */
export type ConnectionStatus = 'DISCONNECTED' | 'SCANNING' | 'CONNECTING' | 'CONNECTED';

export type GantryStatus = 'IDLE' | 'MOVING' | 'HOMING' | 'ERROR';

export type TransportKind = 'BLE' | 'WIFI' | 'MOCK';

/** Long-algebraic UCI move, sent as 4–5 ASCII bytes: "e7e5", "e7e8q". */
export type UciMove = string;

/** Where a move came from; decides whether the gantry must replay it physically. */
export type MoveSource = 'SCREEN' | 'BOARD' | 'ENGINE' | 'REMOTE';

/** App -> ESP32 gantry move command. */
export interface MoveCommand {
  move: UciMove;
  source: MoveSource;
}

/** ESP32 -> App gantry status byte; the array index is the wire value. */
export const GANTRY_STATUS_BY_CODE: readonly GantryStatus[] = ['IDLE', 'MOVING', 'HOMING', 'ERROR'];

/** App -> ESP32 1-byte system opcodes. */
export const SystemOpcode = {
  HOME_GANTRY: 0x01,
  EMERGENCY_STOP: 0x02,
  REBOOT_MCU: 0x03,
} as const;
export type SystemOpcode = (typeof SystemOpcode)[keyof typeof SystemOpcode];

/**
 * ESP32 -> App board notification after decoding: either a UCI move string ("e2e4")
 * or the 64-bit occupancy bitmask as 16 lowercase hex chars (byte 0 = rank 1, bit 0 = a-file).
 */
export type BoardPayload = string;
