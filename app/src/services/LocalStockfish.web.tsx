import { Asset } from 'expo-asset';
import { useEffect } from 'react';

import type { UciTransport } from './StockfishService';

/*
 * Web version of LocalStockfish.tsx (Expo uses this file only in the browser).
 * Browsers can run the same bundled Stockfish 19 lite files in a Web Worker directly,
 * so the web app needs neither the WebView nor expo-file-system that the phone version uses.
 */

const engineJs = require('../../assets/stockfish/stockfish-19-lite-single.jsbundle');
const engineWasm = require('../../assets/stockfish/stockfish-19-lite-single.wasm');

/** A UCI pipe to Stockfish running in a Web Worker in this browser tab. */
export class LocalStockfishTransport implements UciTransport {
  private worker: Worker | null = null;
  private urls: string[] = [];
  private ready = false;
  private closed = false;
  private lineListeners = new Set<(line: string) => void>();
  private closeListeners = new Set<() => void>();
  private waiters = new Set<(ready: boolean) => void>();

  /** Loads the bundled engine files and starts the worker. */
  async start() {
    try {
      const [script, wasm] = await Promise.all([fetchAsset(engineJs, 'text'), fetchAsset(engineWasm, 'arrayBuffer')]);
      if (this.closed) return;
      const wasmUrl = URL.createObjectURL(new Blob([wasm], { type: 'application/wasm' }));
      const workerUrl = URL.createObjectURL(new Blob([script], { type: 'text/javascript' }));
      this.urls = [wasmUrl, workerUrl];
      const worker = new Worker(`${workerUrl}#${encodeURIComponent(wasmUrl)}`);
      worker.onmessage = (event) => [...this.lineListeners].forEach((listener) => listener(String(event.data)));
      worker.onerror = (event) => {
        console.warn('[Stockfish] local engine failed:', event.message);
        this.fail();
      };
      this.worker = worker;
      this.ready = true;
      [...this.waiters].forEach((finish) => finish(true));
    } catch (error) {
      console.warn('[Stockfish] could not load bundled engine', error);
      this.fail();
    }
  }

  connect(timeoutMs = 15000): Promise<boolean> {
    if (this.ready) return Promise.resolve(true);
    if (this.closed) return Promise.resolve(false);
    return new Promise((resolve) => {
      const finish = (ok: boolean) => {
        clearTimeout(timer);
        this.waiters.delete(finish);
        resolve(ok);
      };
      const timer = setTimeout(() => finish(false), timeoutMs);
      this.waiters.add(finish);
    });
  }

  fail() {
    if (this.closed) return;
    this.stopWorker();
    [...this.waiters].forEach((finish) => finish(false));
    [...this.closeListeners].forEach((listener) => listener());
  }

  send(command: string) {
    if (this.ready && !this.closed) this.worker?.postMessage(command);
  }

  onLine(callback: (line: string) => void) {
    this.lineListeners.add(callback);
    return () => void this.lineListeners.delete(callback);
  }

  onClose(callback: () => void) {
    this.closeListeners.add(callback);
    return () => void this.closeListeners.delete(callback);
  }

  close() {
    if (this.closed) return;
    this.stopWorker();
    [...this.waiters].forEach((finish) => finish(false));
  }

  private stopWorker() {
    this.closed = true;
    this.ready = false;
    this.worker?.postMessage('quit');
    this.worker?.terminate();
    this.worker = null;
    this.urls.forEach((url) => URL.revokeObjectURL(url));
    this.urls = [];
  }
}

async function fetchAsset(module: number, as: 'text'): Promise<string>;
async function fetchAsset(module: number, as: 'arrayBuffer'): Promise<ArrayBuffer>;
async function fetchAsset(module: number, as: 'text' | 'arrayBuffer') {
  const asset = Asset.fromModule(module);
  const res = await fetch(asset.uri);
  if (!res.ok) throw new Error(`Bundled Stockfish asset unavailable (${res.status})`);
  return as === 'text' ? res.text() : res.arrayBuffer();
}

/** Starts the engine for `transport`; renders nothing (the phone version renders a hidden WebView). */
export function LocalStockfishHost({ transport }: { transport: LocalStockfishTransport }) {
  useEffect(() => {
    void transport.start();
  }, [transport]);
  return null;
}
