import type { SystemOpcode } from '../../types/chess';
import { UCI_MOVE_RE } from '../../utils/occupancy';
import { BaseHardwareBridge, decodeBoardText, decodeGantryStatus } from './HardwareBridge';

/**
 * Wi-Fi (LAN) transport. Same payloads as BLE, wrapped in a JSON envelope because
 * one socket replaces four characteristics:
 *   App -> ESP32: {"type":"move","data":"e7e5"}   {"type":"cmd","data":2}
 *   ESP32 -> App: {"type":"board","data":"e2e4" | "<16 hex bitmask>"}   {"type":"status","data":0}
 * When the socket is down, commands fall back to HTTP POST http://<ip>/api with the same envelope.
 */
export class WifiWebSocketHardwareAdapter extends BaseHardwareBridge {
  private ws: WebSocket | null = null;
  private host = '';

  async connect(ip: string, timeoutMs = 5000): Promise<boolean> {
    await this.disconnect();
    this.host = ip.trim();
    if (!this.host) return false;
    this.setConnectionStatus('CONNECTING');

    return new Promise((resolve) => {
      const ws = new WebSocket(`ws://${this.host}:81/ws`);
      this.ws = ws;
      const fail = () => {
        clearTimeout(timer);
        if (this.ws === ws) {
          // only the current socket may change status; stale sockets just die
          this.ws = null;
          this.setConnectionStatus('DISCONNECTED');
        }
        resolve(false);
      };
      const timer = setTimeout(() => {
        ws.close();
        fail();
      }, timeoutMs);
      ws.onopen = () => {
        clearTimeout(timer);
        this.setConnectionStatus('CONNECTED');
        resolve(true);
      };
      ws.onmessage = (e) => this.handleMessage(String(e.data));
      ws.onclose = fail; // resolve() is a no-op once settled
      ws.onerror = () => undefined; // onclose follows
    });
  }

  async disconnect() {
    const ws = this.ws;
    this.ws = null;
    ws?.close();
    this.setConnectionStatus('DISCONNECTED');
  }

  private handleMessage(raw: string) {
    let msg: { type?: unknown; data?: unknown };
    try {
      msg = JSON.parse(raw);
    } catch {
      return;
    }
    if (msg.type === 'board' && typeof msg.data === 'string') {
      const payload = decodeBoardText(msg.data);
      if (payload) this.emitBoard(payload);
    } else if (msg.type === 'status' && typeof msg.data === 'number') {
      this.emitGantry(decodeGantryStatus(msg.data));
    }
  }

  private async send(type: 'move' | 'cmd', data: string | number): Promise<boolean> {
    const body = JSON.stringify({ type, data });
    if (this.ws?.readyState === WebSocket.OPEN) {
      this.ws.send(body);
      return true;
    }
    if (!this.host) return false;
    try {
      const res = await fetch(`http://${this.host}/api`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body,
      });
      return res.ok;
    } catch {
      return false;
    }
  }

  sendMove(move: string) {
    return UCI_MOVE_RE.test(move) ? this.send('move', move) : Promise.resolve(false);
  }

  protected sendSystemCommand(opcode: SystemOpcode) {
    return this.send('cmd', opcode);
  }
}
