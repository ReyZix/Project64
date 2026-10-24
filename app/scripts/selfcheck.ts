// Run: npm test  (plain Node, no RN runtime needed)
import assert from 'node:assert/strict';
import { Chess } from 'chess.js';

import { fallbackBestMove } from '../src/services/StockfishService.ts';
import { inferMoveFromOccupancy, occupancyHex } from '../src/utils/occupancy.ts';

const START = new Chess().fen();

// Occupancy layout: byte 0 = rank 1 (all set), bytes 2..5 empty, byte 7 = rank 8.
assert.equal(occupancyHex(START), 'ffff00000000ffff');

// e2e4 clears e2 (rank 2, bit 4) and fills e4 (rank 4, bit 4).
assert.equal(inferMoveFromOccupancy(START, 'ffef0010' + '0000ffff'), 'e2e4');
// A lifted piece (e2 empty, nothing placed yet) is not a move.
assert.equal(inferMoveFromOccupancy(START, 'ffef0000' + '0000ffff'), null);

// Castling is recognised from the king + rook squares together.
const castle = new Chess('r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1');
const afterOO = new Chess(castle.fen());
afterOO.move('O-O');
assert.equal(inferMoveFromOccupancy(castle.fen(), occupancyHex(afterOO.fen())), 'e1g1');

// Promotion defaults to a queen.
const promo = new Chess('8/4P3/8/8/8/8/k7/4K3 w - - 0 1');
const afterPromo = new Chess(promo.fen());
afterPromo.move('e8=Q');
assert.equal(inferMoveFromOccupancy(promo.fen(), occupancyHex(afterPromo.fen())), 'e7e8q');

// Fallback engine finds mate in one and grabs a hanging queen.
assert.equal(fallbackBestMove('6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1', 2), 'a1a8');
assert.equal(fallbackBestMove('4k3/8/8/3q4/4P3/8/8/4K3 w - - 0 1', 1), 'e4d5');

console.log('selfcheck: all assertions passed');
