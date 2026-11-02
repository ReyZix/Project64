# Project64

Project 64 Senior Design.

The combined movement planner and simulator are in [movement_algorithm](./movement_algorithm/README.md).

This project was written with the assistance of Google, Anthropic and OpenAI models.

## M1 Pre-Alpha Build

The M1 Pre-Alpha Build establishes the main architecture for Project64 and demonstrates how the mobile application, chess logic, computer opponent, communication system and physical movement system will work together. The complete autonomous chessboard is still in development, so some physical hardware functionality is currently simulated or still being integrated.

## Completed Work

The current Pre-Alpha build includes:

* A React Native mobile chess application.
* Chess rules and game-state management using `chess.js`.
* Illegal move detection and validation.
* Stockfish 19 Lite WASM integration for the computer opponent.
* A local WebSocket bridge for communication between the application and Stockfish.
* A clearly labeled fallback engine when real Stockfish is unavailable.
* Bluetooth Low Energy (BLE) communication between the mobile application and ESP32.
* UCI-formatted chess move communication such as `e2e4`.
* ESP32 status communication for Idle, Moving, Homing and Error states.
* A C++ movement planner for converting chess moves into gantry and electromagnet commands.
* Movement-planning support for normal moves, captures, castling, en passant and promotion.
* A movement simulator for testing and visualizing planned piece movement.
* Error handling for illegal moves, invalid move information, stale Stockfish responses, connection loss and hardware error states.
* Automated Stockfish testing and BLE communication testing.

Together, these components provide the architectural foundation needed to receive a player's move, update the game state, generate a computer response and eventually move the responding piece on the physical chessboard.

## Project Architecture

The mobile application and `chess.js` provide the central game state for Project64. The `chess.js` game object keeps track of the current board position, turn, move history, castling rights and other information required to continue the game.

When a player enters a move through the application, `chess.js` checks that the move is legal before changing the game state. When it is the computer's turn, the current position is converted into FEN and sent to Stockfish. Stockfish returns a UCI move such as `e7e5`, which is checked before being applied to the game.

A computer-generated move can then be sent from the application to the ESP32 through BLE. The movement planner converts the chess move into the lower-level commands needed to control the X/Y gantry and electromagnet.

**The main system flow is:**

```text
Player / Physical Chessboard
            ↓
       Mobile Application
            ↓
         chess.js
       ↙         ↘
 Stockfish       BLE
                    ↓
                  ESP32
                    ↓
             Movement Planner
                    ↓
       X/Y Gantry + Electromagnet
                    ↓
            Physical Chessboard
```

The final physical board will also use Hall-effect sensors to send board-state information from the ESP32 back to the application. This will allow a move made on the physical board to update the same `chess.js` game state used by the interface and computer opponent.

### Game State and Information Handling

The current game is maintained using a `chess.js` game object. The interface and Stockfish use information derived from this same state, helping keep the displayed board and computer opponent synchronized.

FEN is used to represent the complete chess position when communicating with Stockfish. Individual chess moves are primarily represented using UCI notation such as `e2e4`.

Information is validated before it is allowed to change the game state. `chess.js` rejects illegal player moves and Stockfish responses are parsed and checked before being applied. BLE move commands must also match the expected UCI move format.

The current game state is maintained while the application is running. Local games are not currently saved and restored between application launches.

### Stockfish Computer Opponent

The computer-opponent system follows this general flow:

```text
chess.js
   ↓
Current Position / FEN
   ↓
Stockfish 19 Lite WASM
   ↓
UCI bestmove
   ↓
Parse and Validate Move
   ↓
chess.js
   ↓
Application / Hardware Communication
```

For the Pre-Alpha build, Stockfish runs on a laptop through `stockfish-bridge.mjs`. The mobile application connects to the bridge over the local network using a WebSocket.

The application sends the current position to Stockfish and starts a search. Stockfish returns its selected `bestmove`. The response is parsed and checked against the current `chess.js` position before being applied.

If real Stockfish becomes unavailable, the application can use a fallback computer opponent. The interface clearly identifies when the fallback is being used so it is not confused with the real Stockfish engine.

### BLE and ESP32 Communication

The mobile application communicates with the ESP32 using Bluetooth Low Energy.

The application can send chess moves to the ESP32 using UCI notation. The ESP32 can also communicate its current gantry status back to the application:

* **0 - Idle**
* **1 - Moving**
* **2 - Homing**
* **3 - Error**

BLE communication has been tested in both directions using an iPhone and the ESP32 demonstration firmware. The application can send a move to the ESP32 and a simulated player move can be sent from the ESP32 back to the application. A computer-generated response from the application has also been received by the ESP32.

Physical movement is currently simulated in the ESP32 demonstration firmware. Simulated physical-board moves can be entered through the Arduino Serial Monitor for testing.

### Movement Planner

The project includes a C++ movement planner for the physical chessboard. The movement planner converts a chess move into the gantry and electromagnet commands that will eventually be used to physically move a chess piece.

The planner includes support for normal moves, captures, castling, en passant and promotion. It can also calculate movement routes around other pieces and account for additional piece clearance when necessary.

The movement simulator is currently used to visualize and test these planned movements before the planner is fully connected to the physical motors and electromagnet.

## Integrity and Error Handling

The Pre-Alpha architecture includes several protections to prevent incorrect, outdated or conflicting information from changing the game.

`chess.js` rejects illegal moves before they change the game state. Stockfish responses are checked for valid format and legality before being applied. The Stockfish system also prevents an old response from being applied after the game position has changed.

Starting a new game invalidates the previous computer search. Engine timeouts and connection loss are also handled so the application does not continue waiting indefinitely for a response.

Before a move is sent through BLE, it must match the expected UCI move format. The ESP32 reports whether the gantry is Idle, Moving, Homing or in an Error state so the application can track whether the physical system is ready.

Board notifications generated while the gantry is moving can be ignored so the system's own movement is not mistaken for a new player move. A move originating from the physical board is also not sent back to the gantry as another movement command.

The ESP32 demonstration firmware includes additional handling for invalid protocol messages, command queue overflow and emergency-stop conditions. The movement planner also attempts to reduce collisions by checking piece clearance and routing around occupied areas.

## Testing

Testing has been completed across the major Pre-Alpha software and communication components.

### Stockfish Testing

* Real Stockfish startup and UCI communication tested.
* FEN positions successfully sent to Stockfish.
* Stockfish `bestmove` responses parsed and applied through `chess.js`.
* 12 full turns tested with every Stockfish response legal and successfully applied.
* Promotion handling tested.
* New Game behavior during an active search tested.
* Engine timeout and connection-loss handling tested.
* Fallback behavior tested when Stockfish is unavailable.

### BLE / ESP32 Testing

* BLE connection tested between an iPhone and ESP32.
* App-to-ESP32 move communication tested.
* ESP32-to-app simulated player move communication tested.
* Computer-generated moves successfully received by the ESP32.
* Idle, Moving, Homing and Error status communication tested.

### Movement Planner Testing

* Chess moves can be converted into planned gantry and electromagnet commands.
* Planned movement can be visualized using the movement simulator.
* Routing logic includes support for occupied areas and special chess moves.

## Known Bugs and Limitations

No unresolved reproducible software bugs are currently documented following the latest Pre-Alpha software testing. Any newly discovered bugs will be documented as development continues.

Current limitations include:

* Local games are not saved between application launches.
* The complete physical X/Y gantry and electromagnet are not yet fully integrated with the application and movement planner.
* Hall-effect sensor detection across the complete 64-square board is not yet integrated.
* Physical gantry movement is currently simulated in the ESP32 demonstration firmware.
* Simulated physical-board moves are currently entered through the Arduino Serial Monitor during BLE testing.
* The movement planner still requires calibration and testing with the final motors, gantry, electromagnet, board dimensions and chess pieces.
* Physical captures and special moves still require testing on the completed board.
* Hardware error detection and recovery require additional validation after the complete physical system is assembled.

## Next Steps

The next stage of Project64 will focus primarily on building and integrating the physical hardware for the autonomous chessboard. The software, BLE communication and movement-planning architecture developed during the Pre-Alpha build will be used to control and test the physical system as it is constructed.

The main hardware development tasks include:

* Designing and constructing the custom X/Y gantry system that will move underneath the chessboard.
* Installing and testing the stepper motors, motor drivers, belts, pulleys and linear movement components.
* Building the carriage that will hold and move the electromagnet underneath the board.
* Integrating the electromagnet and its power-control components so chess pieces can be picked up, moved and released.
* Installing magnets in the chess pieces and integrating Hall-effect sensors for detecting the location of pieces on the board.
* Designing and testing the sensor layout for all 64 chessboard squares.
* Developing the physical chessboard surface and determining the required spacing between the board, sensors, electromagnet and chess pieces.
* Adding homing and position calibration so the gantry can accurately locate each chessboard square.
* Creating and testing the physical capture area for pieces removed from the board.
* Connecting the movement planner to the actual motors and electromagnet instead of the current simulator.
* Testing physical movement for normal moves, captures, castling, en passant and promotion.
* Integrating sensor feedback so the system can verify that a physical move was completed correctly.
* Testing communication and error handling with the completed physical hardware.

The main goal of the next stage is to connect the existing software architecture to the custom-built hardware and complete an end-to-end physical turn:

```text
Player physically moves a piece
            ↓
Hall-effect sensors detect the change
            ↓
ESP32 sends the move through BLE
            ↓
Application validates and processes the move
            ↓
Computer opponent selects a response
            ↓
Application sends the response through BLE
            ↓
ESP32 and movement planner control the hardware
            ↓
Custom X/Y gantry moves the electromagnet
            ↓
Computer's piece is physically moved
            ↓
Sensors verify the new board position
```

The next phase will bring these systems together by turning the current software and communication foundation into a fully integrated physical prototype of Project64.