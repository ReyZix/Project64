import type { BoardPayload, GantryStatus, SystemOpcode } from '../types/chess';
import { UCI_MOVE_RE } from '../utils/occupancy';
import { BaseHardwareBridge, decodeBoardBytes, decodeGantryStatus } from './hardware/HardwareBridge';

/*
 * Web version of BleService.ts (Expo uses this file only in the browser).
 * react-native-ble-plx does not run in a browser, so this talks to the same ESP32
 * through Web Bluetooth instead. It works in Chrome or Edge on a Mac (not Safari or Firefox).
 * Same GATT layout, same bytes and the same IHardwareBridge methods as the phone version,
 * so Stockfish and Lichess moves reach the board the same way.
 */

const uuid16 = (short: string) => `0000${short}-0000-1000-8000-00805f9b34fb`;

/** GATT layout; must match the ESP32 firmware. */
export const P64_GATT = {
  SERVICE: uuid16('cb00'),
  BOARD_STATE: uuid16('cb01'), // notify: 4–5 byte ASCII move or 8-byte occupancy bitmask
  MOVE_COMMAND: uuid16('cb02'), // write: 4–5 byte ASCII move
  GANTRY_STATUS: uuid16('cb03'), // notify/read: 1-byte status enum
  SYSTEM_COMMAND: uuid16('cb04'), // write: 1-byte opcode
} as const;

export const DEFAULT_BLE_NAME = 'Project64';

// Minimal Web Bluetooth types (the browser has them, TypeScript's default libs do not).
type Characteristic = EventTarget & {
  value?: DataView;
  startNotifications(): Promise<Characteristic>;
  writeValueWithResponse?(data: BufferSource): Promise<void>;
  writeValue(data: BufferSource): Promise<void>;
};
type GattServer = {
  connected: boolean;
  connect(): Promise<GattServer>;
  disconnect(): void;
  getPrimaryService(uuid: string): Promise<{ getCharacteristic(uuid: string): Promise<Characteristic> }>;
};
type WebBleDevice = EventTarget & { id: string; name?: string; gatt?: GattServer };
type WebBluetooth = {
  requestDevice(options: { filters: { services?: string[]; name?: string }[] }): Promise<WebBleDevice>;
};

const webBluetooth = () => (globalThis.navigator as Navigator & { bluetooth?: WebBluetooth })?.bluetooth;
const bytesOf = (c: Characteristic) =>
  c.value ? new Uint8Array(c.value.buffer, c.value.byteOffset, c.value.byteLength) : new Uint8Array();

/** BLE link to the ESP32 from a desktop browser. Singleton like the phone version. */
export class BleService extends BaseHardwareBridge {
  private static instance: BleService | null = null;
  static getInstance() {
    return (this.instance ??= new BleService());
  }

  private device: WebBleDevice | null = null;
  private chars: { move: Characteristic; command: Characteristic } | null = null;
  private stopListening: (() => void)[] = [];

  private constructor() {
    super();
  }

  /**
   * Opens the browser's Bluetooth picker (filtered to the Project 64 service and name),
   * then connects and subscribes to board and gantry notifications.
   * Must start from a tap, because browsers only show the picker after a user gesture.
   */
  async scanAndConnect(nameOrId = DEFAULT_BLE_NAME): Promise<boolean> {
    const bluetooth = webBluetooth();
    if (!bluetooth) {
      console.warn('[BLE] Web Bluetooth is not available in this browser. Use Chrome or Edge.');
      return false;
    }
    await this.disconnect();

    this.setConnectionStatus('SCANNING');
    let device: WebBleDevice;
    try {
      const filter = nameOrId ? { services: [P64_GATT.SERVICE], name: nameOrId } : { services: [P64_GATT.SERVICE] };
      device = await bluetooth.requestDevice({ filters: [filter] });
    } catch (e) {
      console.warn('[BLE] no board chosen', e); // picker closed or nothing found
      this.setConnectionStatus('DISCONNECTED');
      return false;
    }

    this.setConnectionStatus('CONNECTING');
    try {
      const server = await device.gatt!.connect();
      const service = await server.getPrimaryService(P64_GATT.SERVICE);
      const [board, status, move, command] = await Promise.all(
        [P64_GATT.BOARD_STATE, P64_GATT.GANTRY_STATUS, P64_GATT.MOVE_COMMAND, P64_GATT.SYSTEM_COMMAND].map((uuid) =>
          service.getCharacteristic(uuid),
        ),
      );

      const onBoard = () => {
        const payload = decodeBoardBytes(bytesOf(board));
        if (payload) this.emitBoard(payload);
      };
      const onStatus = () => {
        const bytes = bytesOf(status);
        if (bytes.length) this.emitGantry(decodeGantryStatus(bytes[0]));
      };
      const onDisconnect = () => this.cleanup();
      board.addEventListener('characteristicvaluechanged', onBoard);
      status.addEventListener('characteristicvaluechanged', onStatus);
      device.addEventListener('gattserverdisconnected', onDisconnect);
      this.stopListening = [
        () => board.removeEventListener('characteristicvaluechanged', onBoard),
        () => status.removeEventListener('characteristicvaluechanged', onStatus),
        () => device.removeEventListener('gattserverdisconnected', onDisconnect),
      ];
      await board.startNotifications();
      await status.startNotifications();

      this.device = device;
      this.chars = { move, command };
      console.log(`[BLE] connected to ${device.name ?? device.id} from the browser`);
      this.setConnectionStatus('CONNECTED');
      return true;
    } catch (e) {
      console.warn('[BLE] connect failed', e);
      this.stopListening.forEach((stop) => stop());
      this.stopListening = [];
      device.gatt?.disconnect();
      this.setConnectionStatus('DISCONNECTED');
      return false;
    }
  }

  connect(targetIdOrIp: string) {
    return this.scanAndConnect(targetIdOrIp);
  }

  async disconnect() {
    const device = this.device;
    this.cleanup();
    if (device?.gatt?.connected) device.gatt.disconnect();
  }

  private cleanup() {
    this.stopListening.forEach((stop) => stop());
    this.stopListening = [];
    this.device = null;
    this.chars = null;
    this.setConnectionStatus('DISCONNECTED');
  }

  private async write(characteristic: Characteristic | undefined, raw: string): Promise<boolean> {
    if (!characteristic) return false;
    const data = Uint8Array.from(raw, (c) => c.charCodeAt(0));
    try {
      await (characteristic.writeValueWithResponse
        ? characteristic.writeValueWithResponse(data)
        : characteristic.writeValue(data));
      return true;
    } catch (e) {
      console.warn('[BLE] write failed', e);
      return false;
    }
  }

  sendMove(move: string) {
    return UCI_MOVE_RE.test(move) ? this.write(this.chars?.move, move) : Promise.resolve(false);
  }

  protected sendSystemCommand(opcode: SystemOpcode) {
    return this.write(this.chars?.command, String.fromCharCode(opcode));
  }

  // Names from the original BleService spec; same behaviour as the IHardwareBridge methods.
  subscribeToBoardState(cb: (p: BoardPayload) => void) {
    return this.onBoardStateChange(cb);
  }
  subscribeToGantryStatus(cb: (s: GantryStatus) => void) {
    return this.onGantryStatusChange(cb);
  }
  sendMoveToGantry(move: string) {
    return this.sendMove(move);
  }
  emergencyStop() {
    return this.sendEmergencyStop();
  }
}
