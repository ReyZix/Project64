import { SystemOpcode, type BoardPayload, type GantryStatus } from '../../types/chess';
import { UCI_MOVE_RE } from '../../utils/occupancy';
import { BaseHardwareBridge } from './HardwareBridge';

const wait = (ms: number) => new Promise((r) => setTimeout(r, ms));

/** Simulated ESP32: reports MOVING/HOMING for a realistic duration, then IDLE. */
export class MockHardwareAdapter extends BaseHardwareBridge {
  private timer: ReturnType<typeof setTimeout> | undefined;

  async connect() {
    this.setConnectionStatus('CONNECTING');
    await wait(400);
    this.setConnectionStatus('CONNECTED');
    this.emitGantry('IDLE');
    return true;
  }

  async disconnect() {
    clearTimeout(this.timer);
    this.setConnectionStatus('DISCONNECTED');
  }

  private busy(status: GantryStatus, ms: number) {
    clearTimeout(this.timer);
    this.emitGantry(status);
    this.timer = setTimeout(() => this.emitGantry('IDLE'), ms);
  }

  async sendMove(move: string) {
    if (this.getConnectionStatus() !== 'CONNECTED' || !UCI_MOVE_RE.test(move)) return false;
    const squares = Math.max(
      Math.abs(move.charCodeAt(0) - move.charCodeAt(2)),
      Math.abs(move.charCodeAt(1) - move.charCodeAt(3)),
    );
    this.busy('MOVING', 700 + squares * 250);
    return true;
  }

  protected async sendSystemCommand(opcode: SystemOpcode) {
    if (this.getConnectionStatus() !== 'CONNECTED') return false;
    if (opcode === SystemOpcode.EMERGENCY_STOP) {
      clearTimeout(this.timer);
      this.emitGantry('IDLE');
    } else if (opcode === SystemOpcode.HOME_GANTRY) {
      this.busy('HOMING', 2000);
    } else {
      await this.disconnect();
      void this.connect();
    }
    return true;
  }

  /** Dev hook: pretend the sensor matrix reported something (move string or hex bitmask). */
  simulateBoardPayload(payload: BoardPayload) {
    this.emitBoard(payload);
  }

  /** Dev hook: pretend the gantry stalled. */
  simulateFault() {
    clearTimeout(this.timer);
    this.emitGantry('ERROR');
  }
}
