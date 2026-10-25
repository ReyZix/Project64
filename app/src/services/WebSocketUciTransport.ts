import type { UciTransport } from './StockfishService';

// Connects the React Native app to the Stockfish bridge over WebSocket.
export class WebSocketUciTransport implements UciTransport {
  private url: string;
  private socket: WebSocket | null = null;
  private lineListeners = new Set<(line: string) => void>();
  private closeListeners = new Set<() => void>();

  constructor(url: string) {
    this.url = url;
  }

  // Connects to the Stockfish bridge.
  connect(timeoutMs = 5000): Promise<boolean> {
    return new Promise((resolve) => {
      let settled = false;

      const settle = (ok: boolean) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        resolve(ok);
      };

      const socket = new WebSocket(this.url);
      this.socket = socket;

      const timer = setTimeout(() => {
        socket.close();
        settle(false);
      }, timeoutMs);

      socket.onopen = () => settle(true);
      socket.onerror = () => settle(false);

      socket.onclose = () => {
        settle(false);

        if (this.socket !== socket) return;

        this.socket = null;
        [...this.closeListeners].forEach((listener) => listener());
      };

      socket.onmessage = (event) => {
        for (const line of String(event.data).split(/\r?\n/)) {
          if (line.trim()) {
            [...this.lineListeners].forEach((listener) => listener(line));
          }
        }
      };
    });
  }

  send(command: string) {
    if (this.socket?.readyState === WebSocket.OPEN) {
      this.socket.send(command);
    }
  }

  onLine(callback: (line: string) => void) {
    this.lineListeners.add(callback);

    return () => {
      this.lineListeners.delete(callback);
    };
  }

  // Runs when the Stockfish bridge disconnects unexpectedly.
  onClose(callback: () => void) {
    this.closeListeners.add(callback);

    return () => {
      this.closeListeners.delete(callback);
    };
  }

  close() {
    const socket = this.socket;
    this.socket = null;
    socket?.close();
  }
}