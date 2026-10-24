import { Chess, type Color, type PieceSymbol, type Square } from 'chess.js';
import { memo, useMemo } from 'react';
import { Pressable } from 'react-native';
import Svg, { Circle, G, Path, Rect } from 'react-native-svg';

import { colors } from '../theme';

const S = 45; // one square in SVG units; piece paths are drawn in a 45x45 box
const FILES = 'abcdefgh';

const circle = (cx: number, cy: number, r: number) =>
  `M${cx - r} ${cy}a${r} ${r} 0 1 0 ${2 * r} 0a${r} ${r} 0 1 0 ${-2 * r} 0Z`;

const BASE = 'M11 34h23v4.5H11Z';
const ROYAL_BASE = 'M13 30.5h19v3H13Z' + BASE;

/** Flat silhouettes: [body, contrasting detail]. */
const PIECES: Record<PieceSymbol, [string, string?]> = {
  p: [circle(22.5, 14, 5.5) + 'M18 34l2.3-13.5h4.4L27 34Z' + BASE],
  r: ['M12.5 10h4v3h4v-3h4v3h4v-3h4v7.5h-20Z' + 'M15.5 17.5h14V34h-14Z' + BASE, 'M15.5 17.5h14'],
  n: [
    'M14 34c0-6 3-9.5 5-13l-5.5 2.5-3-3c1.5-5 4.5-9 8.5-11l-1-3.5 4 2.5c6 0 10.5 5 10.5 13 0 5-.5 9.5-.5 12.5Z' + BASE,
    circle(19, 14.5, 1.3),
  ],
  b: [
    circle(22.5, 8.5, 2.5) + 'M22.5 11.5c-6 4.5-7.5 10.5-5.5 16h11c2-5.5.5-11.5-5.5-16Z' + 'M16 27.5h13V31H16Z' + BASE,
    'M22.5 16v6M19.5 19h6',
  ],
  q: [
    circle(10, 15, 2.2) + circle(22.5, 10.5, 2.2) + circle(35, 15, 2.2) +
      'M10.5 17l3.5 13.5h17l3.5-13.5-6.5 7.5-5.5-11.5-5.5 11.5Z' + ROYAL_BASE,
  ],
  k: [
    'M21 4.5h3v3h3v3h-3v3.5h-3v-3.5h-3v-3h3Z' + 'M14 30.5c-2.5-8 1-14.5 8.5-15 7.5.5 11 7 8.5 15Z' + ROYAL_BASE,
    'M22.5 19v8',
  ],
};

interface PieceProps {
  type: PieceSymbol;
  color: Color;
}

const Piece = memo(function Piece({ type, color }: PieceProps) {
  const [body, detail] = PIECES[type];
  const fill = color === 'w' ? colors.pieceWhite : colors.pieceBlack;
  const ink = color === 'w' ? colors.pieceBlack : colors.pieceWhite;
  return (
    <>
      <Path d={body} fill={fill} stroke={ink} strokeWidth={color === 'w' ? 1.5 : 1} strokeLinejoin="round" />
      {detail && <Path d={detail} fill={ink} stroke={ink} strokeWidth={1.5} strokeLinecap="round" />}
    </>
  );
});

/** Board coords with White at the bottom. */
const toXY = (sq: Square) => ({ x: FILES.indexOf(sq[0]) * S, y: (8 - Number(sq[1])) * S });

export interface MinimalistBoardProps {
  fen: string;
  size: number;
  selected?: Square | null;
  legalTargets?: Square[];
  lastMove?: { from: Square; to: Square } | null;
  /** Square to flag red: king in check or a rejected move. */
  dangerSquare?: Square | null;
  disabled?: boolean;
  onSquarePress?: (square: Square) => void;
}

export function MinimalistBoard({
  fen,
  size,
  selected,
  legalTargets = [],
  lastMove,
  dangerSquare,
  disabled,
  onSquarePress,
}: MinimalistBoardProps) {
  const board = useMemo(() => new Chess(fen).board(), [fen]);

  const squares = [];
  for (let rank = 8; rank >= 1; rank--) {
    for (let f = 0; f < 8; f++) {
      const dark = (f + rank) % 2 === 1; // a1 is dark
      squares.push(
        <Rect
          key={`${FILES[f]}${rank}`}
          x={f * S}
          y={(8 - rank) * S}
          width={S}
          height={S}
          fill={dark ? colors.squareDark : colors.squareLight}
        />,
      );
    }
  }

  const overlay = (sq: Square | null | undefined, fill: string) => {
    if (!sq) return null;
    const { x, y } = toXY(sq);
    return <Rect key={`o-${sq}-${fill}`} x={x} y={y} width={S} height={S} fill={fill} />;
  };

  const handlePress = (locationX: number, locationY: number) => {
    const f = Math.floor((locationX / size) * 8);
    const r = 8 - Math.floor((locationY / size) * 8);
    if (f >= 0 && f < 8 && r >= 1 && r <= 8) onSquarePress?.(`${FILES[f]}${r}` as Square);
  };

  return (
    <Pressable
      disabled={disabled}
      onPress={(e) => handlePress(e.nativeEvent.locationX, e.nativeEvent.locationY)}
      accessibilityRole="adjustable"
      accessibilityLabel="Chess board"
      accessibilityState={{ disabled }}
      style={{ width: size, height: size }}
    >
      <Svg width={size} height={size} viewBox={`0 0 ${8 * S} ${8 * S}`} pointerEvents="none">
        {squares}
        {overlay(lastMove?.from, colors.highlightLegal)}
        {overlay(lastMove?.to, colors.highlightLegal)}
        {overlay(selected, colors.highlightLegal)}
        {overlay(dangerSquare, colors.highlightDanger)}
        {board.flat().map(
          (p) =>
            p && (
              <G key={p.square} transform={`translate(${toXY(p.square).x} ${toXY(p.square).y})`}>
                <Piece type={p.type} color={p.color} />
              </G>
            ),
        )}
        {legalTargets.map((sq) => {
          const { x, y } = toXY(sq);
          return <Circle key={`t-${sq}`} cx={x + S / 2} cy={y + S / 2} r={S * 0.16} fill={colors.highlightLegal} />;
        })}
      </Svg>
    </Pressable>
  );
}
