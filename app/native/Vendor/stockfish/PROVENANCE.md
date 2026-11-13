Stockfish 17.1, official upstream https://github.com/official-stockfish/Stockfish
Tag sf_17.1, commit 03e27488f3d21d8ff4dbf3065603afa21dbd0ef3.
Source is distributed under GPL version 3 or later; see Copying.txt and
individual source notices. Linking this engine into an app entails GPL
obligations for distribution of the combined work, including corresponding
source and license terms; app-store distribution restrictions may conflict.
Upstream README notes that its networks use data provided by Leela Chess Zero,
which is available under ODbL. See the upstream README for attribution.

Official networks downloaded from https://tests.stockfishchess.org/api/nn/:
1c0000000000a67d629999d932d0c373f7450ce43cd12d0562868f4eaf9ae2ad  nn-1c0000000000.nnue
37f18f62d772f3107e1d6aaca3898c130c3c86f2ab63e6555fbbca20635a899d  nn-37f18f62d772.nnue
Local change: nnue/network.cpp throws on unavailable or incompatible network
instead of terminating the entire host process.
