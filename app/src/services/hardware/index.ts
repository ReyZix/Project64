import type { TransportKind } from '../../types/chess';
import { BleService } from '../BleService';
import type { IHardwareBridge } from './HardwareBridge';
import { MockHardwareAdapter } from './MockHardwareAdapter';
import { WifiWebSocketHardwareAdapter } from './WifiWebSocketHardwareAdapter';

export type { IHardwareBridge } from './HardwareBridge';
export { BleService as BleHardwareAdapter } from '../BleService';
export { MockHardwareAdapter } from './MockHardwareAdapter';
export { WifiWebSocketHardwareAdapter } from './WifiWebSocketHardwareAdapter';

export function createHardwareBridge(kind: TransportKind): IHardwareBridge {
  switch (kind) {
    case 'BLE':
      return BleService.getInstance();
    case 'WIFI':
      return new WifiWebSocketHardwareAdapter();
    case 'MOCK':
      return new MockHardwareAdapter();
  }
}
