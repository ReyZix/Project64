import { PermissionsAndroid, Platform } from 'react-native';
import { BleManager, State, type Device, type Subscription } from 'react-native-ble-plx';

import type { BoardPayload, GantryStatus, SystemOpcode } from '../types/chess';
import { UCI_MOVE_RE } from '../utils/occupancy';
import { BaseHardwareBridge, decodeBoardBytes, decodeGantryStatus } from './hardware/HardwareBridge';

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

const toBase64 = (s: string) => btoa(s);
const fromBase64 = (b64: string) => Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));

/**
 * BLE implementation of IHardwareBridge over react-native-ble-plx.
 * Singleton because the OS allows only one BleManager per app.
 * When the native module is missing (Expo Go, iOS simulator) every call resolves false
 * instead of crashing; use the Mock transport there.
 */
export class BleService extends BaseHardwareBridge {
  private static instance: BleService | null = null;
  static getInstance() {
    return (this.instance ??= new BleService());
  }

  private manager: BleManager | null = null;
  private device: Device | null = null;
  private subscriptions: Subscription[] = [];

  private constructor() {
    super();
  }

  private getManager(): BleManager | null {
    if (!this.manager) {
      try {
        this.manager = new BleManager();
      } catch (e) {
        console.warn('[BLE] native module unavailable, use the Mock transport on simulators', e);
      }
    }
    return this.manager;
  }

  async requestPermissions(): Promise<boolean> {
    if (Platform.OS !== 'android') return true; // iOS prompts from NSBluetoothAlwaysUsageDescription
    if (Number(Platform.Version) >= 31) {
      const result = await PermissionsAndroid.requestMultiple([
        PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
        PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
      ]);
      return Object.values(result).every((r) => r === PermissionsAndroid.RESULTS.GRANTED);
    }
    const r = await PermissionsAndroid.request(PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION);
    return r === PermissionsAndroid.RESULTS.GRANTED;
  }

  private waitForPoweredOn(manager: BleManager, timeoutMs = 5000): Promise<boolean> {
    return new Promise((resolve) => {
      const timer = setTimeout(() => {
        sub.remove();
        resolve(false);
      }, timeoutMs);
      const sub = manager.onStateChange((state) => {
        if (state !== State.PoweredOn) return;
        clearTimeout(timer);
        sub.remove();
        resolve(true);
      }, true);
    });
  }

  /** Scans for the Project 64 service and connects to the first device matching name/id (any if empty). */
  async scanAndConnect(nameOrId = DEFAULT_BLE_NAME, timeoutMs = 10000): Promise<boolean> {
    const manager = this.getManager();
    if (!manager || !(await this.requestPermissions())) return false;
    if (!(await this.waitForPoweredOn(manager))) {
      console.warn('[BLE] Bluetooth is off or unauthorized');
      return false;
    }
    await this.disconnect();

    this.setConnectionStatus('SCANNING');
    const found = await new Promise<Device | null>((resolve) => {
      const timer = setTimeout(() => {
        manager.stopDeviceScan();
        resolve(null);
      }, timeoutMs);
      manager.startDeviceScan([P64_GATT.SERVICE], null, (error, d) => {
        const matches = d && (!nameOrId || [d.id, d.name, d.localName].includes(nameOrId));
        if (!error && !matches) return;
        clearTimeout(timer);
        manager.stopDeviceScan();
        resolve(error ? null : d);
      });
    });
    if (!found) {
      this.setConnectionStatus('DISCONNECTED');
      return false;
    }

    this.setConnectionStatus('CONNECTING');
    try {
      const device = await found.connect({ requestMTU: 64 });
      await device.discoverAllServicesAndCharacteristics();
      this.device = device;
      this.subscriptions = [
        device.monitorCharacteristicForService(P64_GATT.SERVICE, P64_GATT.BOARD_STATE, (err, c) => {
          const payload = !err && c?.value ? decodeBoardBytes(fromBase64(c.value)) : null;
          if (payload) this.emitBoard(payload);
        }),
        device.monitorCharacteristicForService(P64_GATT.SERVICE, P64_GATT.GANTRY_STATUS, (err, c) => {
          if (!err && c?.value) this.emitGantry(decodeGantryStatus(fromBase64(c.value)[0]));
        }),
        manager.onDeviceDisconnected(device.id, () => this.cleanup()),
      ];
      this.setConnectionStatus('CONNECTED');
      return true;
    } catch (e) {
      console.warn('[BLE] connect failed', e);
      await this.disconnect();
      return false;
    }
  }

  connect(targetIdOrIp: string) {
    return this.scanAndConnect(targetIdOrIp);
  }

  async disconnect() {
    const device = this.device;
    this.cleanup();
    await device?.cancelConnection().catch(() => undefined);
  }

  private cleanup() {
    this.subscriptions.forEach((s) => s.remove());
    this.subscriptions = [];
    this.device = null;
    this.setConnectionStatus('DISCONNECTED');
  }

  private async write(characteristic: string, raw: string): Promise<boolean> {
    if (!this.device) return false;
    try {
      await this.device.writeCharacteristicWithResponseForService(P64_GATT.SERVICE, characteristic, toBase64(raw));
      return true;
    } catch (e) {
      console.warn('[BLE] write failed', e);
      return false;
    }
  }

  sendMove(move: string) {
    return UCI_MOVE_RE.test(move) ? this.write(P64_GATT.MOVE_COMMAND, move) : Promise.resolve(false);
  }

  protected sendSystemCommand(opcode: SystemOpcode) {
    return this.write(P64_GATT.SYSTEM_COMMAND, String.fromCharCode(opcode));
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
