import { Platform } from 'react-native';

/** Project 64 design tokens. */
export const colors = {
  background: '#0D1117',
  squareDark: '#38404B',
  squareLight: '#E2DCD2',
  pieceWhite: '#FAFAF7',
  pieceBlack: '#14171C',
  highlightLegal: 'rgba(76, 175, 80, 0.3)',
  highlightDanger: 'rgba(244, 67, 54, 0.4)',
  glassFill: 'rgba(22, 27, 34, 0.75)',
  glassBorder: 'rgba(255, 255, 255, 0.12)',
  glowConnected: 'rgba(76, 175, 80, 0.55)',
  text: '#E6EDF3',
  textMuted: 'rgba(230, 237, 243, 0.6)',
  ok: '#4CAF50',
  warn: '#F2B84B',
  danger: '#F44336',
};

export const fonts = {
  mono: Platform.select({ ios: 'Menlo', default: 'monospace' }),
  sans: Platform.select({ ios: 'System', default: 'sans-serif' }),
};
