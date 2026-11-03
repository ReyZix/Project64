import { Asset } from 'expo-asset';
import { File } from 'expo-file-system';
import { useEffect, useState } from 'react';
import { StyleSheet } from 'react-native';
import WebView, { type WebViewMessageEvent } from 'react-native-webview';

import type { UciTransport } from './StockfishService';

type EngineMessage = { type: 'ready' | 'line' | 'error'; value?: string };

/** A UCI pipe to Stockfish running in a hidden WebView on this phone. */
export class LocalStockfishTransport implements UciTransport {
  private view: { injectJavaScript(script: string): void } | null = null;
  private ready = false;
  private closed = false;
  private lineListeners = new Set<(line: string) => void>();
  private closeListeners = new Set<() => void>();
  private waiters = new Set<(ready: boolean) => void>();

  setView(view: { injectJavaScript(script: string): void } | null) {
    this.view = view;
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

  receive(event: WebViewMessageEvent) {
    if (this.closed) return;
    let message: EngineMessage;
    try {
      message = JSON.parse(event.nativeEvent.data) as EngineMessage;
    } catch {
      return;
    }
    if (message.type === 'ready') {
      this.ready = true;
      [...this.waiters].forEach((finish) => finish(true));
    } else if (message.type === 'line') {
      [...this.lineListeners].forEach((listener) => listener(message.value ?? ''));
    } else if (message.type === 'error') {
      console.warn('[Stockfish] local engine failed:', message.value);
      this.fail();
    }
  }

  fail() {
    if (this.closed) return;
    this.ready = false;
    this.closed = true;
    [...this.waiters].forEach((finish) => finish(false));
    [...this.closeListeners].forEach((listener) => listener());
  }

  send(command: string) {
    if (!this.ready || this.closed) return;
    this.view?.injectJavaScript(`window.engineCommand(${JSON.stringify(command)}); true;`);
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
    this.view?.injectJavaScript('window.engineStop?.(); true;');
    this.closed = true;
    this.ready = false;
    [...this.waiters].forEach((finish) => finish(false));
  }
}

// Stockfish 19 lite, single-threaded: its Worker can load this bundled WASM without a server.
const engineJs = require('../../assets/stockfish/stockfish-19-lite-single.jsbundle');
const engineWasm = require('../../assets/stockfish/stockfish-19-lite-single.wasm');

async function engineHtml(): Promise<string> {
  const [script, wasm] = await Promise.all([
    Asset.fromModule(engineJs).downloadAsync(),
    Asset.fromModule(engineWasm).downloadAsync(),
  ]);
  if (!script.localUri || !wasm.localUri) throw new Error('Bundled Stockfish assets unavailable');
  const [js, wasmBase64] = await Promise.all([
    new File(script.localUri).text(),
    new File(wasm.localUri).base64(),
  ]);
  // Escape '<' so engine source cannot close the containing script tag.
  const jsLiteral = JSON.stringify(js).replace(/</g, '\\u003c');
  return `<!doctype html><html><head><meta name="viewport" content="width=device-width"></head><body><script>
    const report = (type, value) => window.ReactNativeWebView.postMessage(JSON.stringify({type, value}));
    window.onerror = (message) => report('error', String(message));
    try {
      const wasm = Uint8Array.from(atob(${JSON.stringify(wasmBase64)}), c => c.charCodeAt(0));
      const wasmUrl = URL.createObjectURL(new Blob([wasm], {type: 'application/wasm'}));
      const workerUrl = URL.createObjectURL(new Blob([${jsLiteral}], {type: 'text/javascript'}));
      const worker = new Worker(workerUrl + '#' + encodeURIComponent(wasmUrl));
      worker.onmessage = event => report('line', String(event.data));
      worker.onerror = event => report('error', event.message || 'Stockfish worker error');
      window.engineCommand = command => worker.postMessage(command);
      window.engineStop = () => {
        worker.postMessage('quit');
        worker.terminate();
        URL.revokeObjectURL(workerUrl);
        URL.revokeObjectURL(wasmUrl);
      };
      report('ready');
    } catch (error) { report('error', String(error)); }
  </script></body></html>`;
}

export function LocalStockfishHost({ transport }: { transport: LocalStockfishTransport }) {
  const [html, setHtml] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    void engineHtml().then(
      (loaded) => { if (!cancelled) setHtml(loaded); },
      (error) => { if (!cancelled) {
        console.warn('[Stockfish] could not load bundled engine', error);
        transport.fail();
      } },
    );
    return () => { cancelled = true; transport.setView(null); };
  }, [transport]);

  if (!html) return null;
  return (
    <WebView
      ref={(view) => transport.setView(view)}
      style={styles.hidden}
      source={{ html }}
      originWhitelist={['*']}
      javaScriptEnabled
      onMessage={(event) => transport.receive(event)}
      onError={() => transport.fail()}
      scrollEnabled={false}
    />
  );
}

const styles = StyleSheet.create({ hidden: { position: 'absolute', width: 1, height: 1, opacity: 0 } });
