# Lichess online play

The app can play real people online through [Lichess](https://lichess.org): a random opponent or a friend you challenge by username. It uses the Lichess [Board API](https://lichess.org/api#tag/board), which is made for physical boards and third-party apps and works with normal Lichess accounts.

You do not need the ESP32 to play online. Without it, the app uses the Simulator Mock board. With the ESP32 connected over BLE, your opponent's moves are also sent to the physical board.

## What you need

- A Lichess account (free).
- A Lichess personal access token (steps below).
- The app running on a Mac (web version) or on an iPhone (development build, see [ESP32_COMMUNICATION_SETUP.md](ESP32_COMMUNICATION_SETUP.md)).
- Internet access on the device running the app.

## 1. Create a Lichess token (one time)

1. Sign in on lichess.org.
2. Go to **Preferences → API access tokens** (or open <https://lichess.org/account/oauth/token>).
3. Click **New personal API access token**. Give it a name, for example `Project 64`.
4. Tick these two boxes and leave everything else unticked:
   - **Send, accept and reject challenges** (`challenge:write`), under Play Games
   - **Play games using the Board API** (`board:play`), under External Play
5. Scroll down, click **Create**, and copy the token. It starts with `lip_`. Lichess only shows it once.

Shortcut: this link opens the same page with the name and both boxes already filled in:
<https://lichess.org/account/oauth/token/create?scopes[]=board:play&scopes[]=challenge:write&description=Project+64>

Keep the token private. Paste it only into the app's sign-in box. Do not put it in the code, the repository, or a chat. If it ever gets shared by accident, delete it on the same Lichess page and create a new one.

## 2. Start the app

From the repo's main folder:

```bash
cd app
npm install
```

**On a Mac (web version):**

```bash
npm run web
```

Then open <http://localhost:8081> in Chrome.

**On an iPhone:** use the team's development build and start the app server with:

```bash
npx expo start --dev-client --lan
```

Scan the QR code with the iPhone Camera. The development build needs to be rebuilt once after this change because the token is stored with `expo-secure-store`, which is a new native package.

## 3. Sign in

1. Tap **Play Game**.
2. Tap the Settings gear, choose **Online** under Game mode, and tap **Done**.
3. Paste your token into the **Lichess token** box and tap **Sign in to Lichess**.
4. The panel should say **Signed in as** followed by your Lichess username.

On the iPhone the token is saved securely in the Keychain, so you only sign in once. In the web version it is kept in memory only, so you sign in again after a page refresh. To remove it, tap **Sign out**.

## 4. Start a game

Pick a time control first (minutes + seconds added per move): **10+0**, **10+5**, **15+10** or **30+0**. Lichess only allows Rapid or slower games through the Board API.

**Random opponent**

1. Tap **Find random opponent**.
2. The app shows "Looking for an opponent…". Lichess usually pairs you within a minute.
3. Tap **Cancel** to stop searching.

**Challenge a friend**

1. Type your friend's Lichess username in the box and tap **Challenge**.
2. Your friend accepts on lichess.org or in the Lichess app. The game then starts in Project 64.
3. Tap **Cancel** to withdraw the challenge.

**Accept a challenge**

When someone challenges you, it shows in the panel with their name, rating and time control. Tap **Accept** or **Decline**.

Games are casual (unrated) by default, so your rating is not affected.

## 5. During the game

- The top of the screen shows your opponent's name and rating, and the bottom shows you. If you play Black, the board flips so your pieces are at the bottom.
- When it says **Your move**, tap a piece and then the square you want. chess.js checks the move is legal, then it is sent to Lichess.
- When it says **Waiting for …**, the board is locked until your opponent moves. Their move appears on the board automatically.
- The clocks come from Lichess.
- **Resign** ends the game. Before both players have moved, it aborts the game instead.
- When the game ends, the result shows (for example "You won · Checkmate"). Tap **Back to lobby** to play again.

Stockfish is never used in online games. Lichess does not allow engine help in Board API games.

## With the physical board (ESP32)

Connect the board first: Settings → **BLE** → enter `Project64` → **Connect**, and wait for Idle. Then:

- A move made on the physical board (or typed in Serial Monitor as `board e2e4`) is checked and sent to Lichess. It is not sent back to the gantry, because the piece is already in place.
- A move you tap on the screen is sent to the gantry only after Lichess accepts it, so a refused move never moves a piece.
- Your opponent's move works like Stockfish's: it is sent to the gantry once the gantry is Idle, and it only appears on the screen after the board accepted it, so the screen and the physical board never disagree.
- If the board does not accept the opponent's move (for example the Bluetooth link dropped), the status shows **Could not send … to the board · tap Retry**. Reconnect the board if needed, then tap **Retry**.

## Checking that it really is Lichess

- Open the game on lichess.org in another tab or on your phone. Moves from the app appear there, and your opponent's moves appear in the app.
- The logs (browser console, or the terminal running Expo) show each step:
  `[Lichess] move sent to Lichess: e2e4`, `[Lichess] move received from Lichess: e7e5`, `[Game] Opponent move applied: e7e5`.

## If something goes wrong

| Message or problem | What to do |
|---|---|
| "Lichess rejected the token" | The token is wrong, expired or deleted. Create a new one with the two permissions above. |
| "Lichess refused …" | Lichess did not accept the move (for example the game already ended). The board goes back to Lichess's position. |
| "Lichess asked us to slow down" | Lichess's rate limit. Wait one minute; the app pauses on its own. |
| An error right after **Find random opponent** | Lichess refused the seek, usually because of the time control. Random seeks must be Rapid or slower, so use one of the listed time controls. |
| "Could not reach Lichess" | Check the device's internet connection. |
| Your friend never sees the challenge | Check the spelling of their username, and that they are signed in on lichess.org. |
| "No opponent found" | No one took the seek in time. Try again or try a different time control. |

If the connection drops during a game, the app reconnects on its own and reloads the game from Lichess.

## Testing without real accounts

A small fake Lichess server is included, so the online flow can be tested on a laptop without Lichess accounts or internet.

Automated tests (sign-in, random seek, friend challenge, accept, decline, cancel, resign, refused moves, rate limits):

```bash
npm run test:lichess
```

Manual test in the browser, with two terminals:

```bash
npm run lichess-fake
```

```bash
EXPO_PUBLIC_LICHESS_URL=http://localhost:8780 npm run web
```

Sign in with `test-token`. **Find random opponent** pairs you with `random_human`, a simulated player who answers each move. The fake server's second account uses `friend-token` (username `teammate`), which you can use with `curl` to send and answer challenges.

## Files

| File | What it does |
|---|---|
| `app/src/services/LichessService.ts` | All requests and live streams to Lichess |
| `app/src/services/lichessToken.ts` | Saves the token securely on the phone |
| `app/src/game/LichessGame.ts` | Keeps chess.js in sync with Lichess's move list |
| `app/src/game/useLichess.ts` | Online game state: lobby, streams, moves |
| `app/src/components/LichessPanel.tsx` | Online lobby and in-game controls |
| `app/src/screens/GameScreen.tsx` | Online mode, opponent info, board flip |
| `app/scripts/fake-lichess-server.mjs` | Local fake Lichess for testing |
| `app/scripts/lichess-flow-test.ts` | Automated tests (`npm run test:lichess`) |
