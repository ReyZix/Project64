# Project 64 - Stockfish and Lichess in C++

Caroline's part of the port (Stockfish integration and Lichess online play), added here as its own folder.
It is not wired into `../CMakeLists.txt` or the iOS app yet, so it cannot break that build.

How it fits with the rest of `app/native`:

- `LichessClient`, `LichessSession`, `Json` and `EventQueue` are new; nothing else in `app/native` does Lichess yet.
- This `StockfishEngine` starts Stockfish as a separate program over UCI. That works on a Mac but not on an
  iPhone, which cannot start other programs. On iOS use the in-process engine in `../Core/StockfishEngine`.
- `Game` here only moves pieces and leaves the rules to Stockfish or Lichess. `../Core/ChessGame` has real
  rules, so the app version should use that one.
- `HardwareBridge` here is a small C++ interface with a mock board; `../Interfaces/P64HardwareBridge.h` is the app's.

The Stockfish integration and Lichess online play for Project 64, written in C++17.
There is no TypeScript or JavaScript in this folder.

It builds one terminal program, `project64`, so the logic can be run and tested by itself.
The classes do not depend on the terminal, so they can sit behind the team's app or web app later.

## Build and run (Mac)

    make
    make test

Play the engine (needs Stockfish: `brew install stockfish`):

    ./project64 stockfish

Play a person on Lichess. The token is typed hidden, or read from `LICHESS_TOKEN`. It is never saved or printed.
The token needs "Play games with the board API" and "Send, accept and reject challenges".

    ./project64 lichess

## Files

    src/
      Game.h/.cpp             The one copy of the game: pieces, move list, castling, en passant, promotion,
                              and the board-first rule (playBoardFirst).
      HardwareBridge.h        The link to the physical board. MockBridge is a pretend board for testing.
      StockfishEngine.h/.cpp  Starts the real Stockfish program and talks to it in UCI over two pipes.
      LichessClient.h/.cpp    Lichess Board API with libcurl: short requests and long-lived ndjson streams.
      LichessSession.h/.cpp   Lobby (find opponent, challenges) and the live game, compared with Lichess after every move.
      Json.h/.cpp             Small JSON reader for the lines Lichess sends.
      EventQueue.h            Lets the stream threads and the keyboard hand work to the main thread.
      main.cpp                The terminal program.
    tests/tests.cpp           29 checks; run with `make test`.

## How it works

- **Board first.** A Stockfish move or a Lichess opponent's move is sent to the board with
  `bridge.sendMove`. Only if that works is it played on screen. If it fails it waits, and `retry` sends it again.
  Type `unplug` and `plug` to see this with the mock board.
- **Your own move.** Against Stockfish it must be in Stockfish's list of legal moves. Online it goes to
  Lichess first and is played only after Lichess accepts it. Either way it then goes to the board, and the game
  does not go on until the board has taken it. Moves wait for the board in a queue, so they arrive in order.
- **Who checks the rules.** Stockfish (`go perft 1` lists every legal move) or Lichess. `Game` only moves pieces.
- **If Stockfish crashes or hangs.** The engine is restarted and the question is asked once more.
  If that also fails the program says so. It never treats "no answer" as "no legal moves".
- **If the network goes silent.** Lichess sends a keep-alive line every few seconds. If the event or game stream
  receives nothing for 30 seconds, the connection is closed and reopened, and the game is compared with Lichess again.
- **Threads.** Each Lichess stream reads on its own thread and posts events to `EventQueue`.
  Only the main thread touches the game.

## What has and has not been tested

Tested on a Mac with Stockfish 19 from Homebrew:

- `make test`: 29 of 29 pass, also under the memory checker (ASan and UBSan). This includes killing the
  engine and checking that it is restarted.
- 8 complete Stockfish-against-Stockfish games (829 moves): after every move the board in `Game` matched
  Stockfish's own position. (Run on iteration 9; `Game` has not changed since.)
- A game against Stockfish: an illegal move refused, the engine killed in the middle (the game carried on),
  and your own move held back while the board was unplugged.
- Two copies of the program playing each other through a fake Lichess server: challenge, accept, moves both ways,
  resign, a wrong token refused, a restart in the middle of a game, a move queue with the board unplugged,
  and a server frozen for 38 seconds (the drop was noticed after 30 seconds and play continued after reconnecting).

Not tested yet:

- The real lichess.org (it needs your own token). This is the most important thing still to do.
- A real ESP32. Only `MockBridge` exists so far; a Bluetooth bridge is the next piece to write,
  and it only has to implement `HardwareBridge`.

Known gaps:

- Draws by repetition or the 50-move rule against Stockfish are not detected.
- When the game is reloaded from Lichess, the board is not told. After a restart that is right (the pieces
  are already in place); after missed moves the pieces would be behind.
- No moves from the physical board, no clocks, no draw offers.

The fake Lichess server used for testing is the JavaScript tool in the old app repo
(`app/scripts/fake-lichess-server.mjs`). It is not part of this code and should be replaced by a C++ test.
To use it:

    LICHESS_URL=http://localhost:8780 LICHESS_TOKEN=test-token ./project64 lichess

## Changes since iteration 9

- Stockfish is restarted after a crash or a missing answer (`StockfishEngine`).
- Lichess streams notice a silent connection after 30 seconds (`LichessClient`).
- Your own move is retried on the board like any other move, through a queue (`main.cpp`).
- 5 new tests.
