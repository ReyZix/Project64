# ESP32 + app setup

These are the steps I used to get the app talking to the ESP32. I tested it with an iPhone 15, Xcode 27.1, and Arduino-ESP32 3.3.x.

## Setting it up the first time 

1. Get the branch with these changes. From the repo's main folder:

```bash
cd app
npm ci
```

2. Check your Xcode and Swift versions:

```bash
xcodebuild -version
xcrun swift --version
```

Xcode 27.1 / Swift 6.4 worked for me. One dependency needs Swift 6.2 or newer, so Xcode 16.4 was too old. Your Xcode also needs to support your phone.

3. The app needs a native development build for Bluetooth, so use the installed Project 64 app instead of Expo Go. The shared package files should already include expo-dev-client. If it is missing:

```bash
npx expo install expo-dev-client
```

Make sure "expo-dev-client" is an entry in the existing plugins list in app.json, alongside the BLE plugin. Then apply the setup:

```bash
npx expo prebuild --platform ios
```

1. Connect your phone into your laptop. Unlock it and trust the computer if asked. On the phone, turn on Settings > Privacy & Security > Developer Mode and finish the restart/confirmation.

2. For signing, open the workspace:

```bash
open ios/Project64.xcworkspace
```

Click the Project64 target > Signing & Capabilities. Turn on automatic signing and choose your own Apple account/Personal Team. Under Xcode > Settings > Locations, select the newer Xcode Command Line Tools.

6. Back in the app terminal:

```bash
npx expo run:ios --device
```

Choose your actual phone. If it says Untrusted Developer, go to Settings > General > VPN & Device Management on the phone and trust your Apple Development profile. Allow Bluetooth and Local Network access when asked.

## Setting up the ESP32

1. Open hardware/firmware/Project64_BLE_Demo/Project64_BLE_Demo.ino in Arduino IDE.
2. Use the Espressif ESP32 board package. ESP32 Dev Module worked for my board.
3. Connect the ESP32 with a USB data cable and select its USB serial port.
4. Upload the sketch. Open Serial Monitor at 115200 baud and choose Newline.
5. You should see Project64 BLE ready and V2 ALIVE messages with increasing uptime.

## Running it again 

1. Plug in the ESP32. Keep it connected to a computer if you want serial logs or want to type simulated board moves.
2. Put the laptop and phone on the same Wi-Fi network. From app, run:

```bash
npx expo start --dev-client --lan
```

3. Leave the terminal running and scan its current QR code with the iPhone Camera.
4. In Project 64, tap Play Game, then the app's Settings gear. The floating Tools button is Expo's menu.
5. Select BLE, enter Project64, and tap Connect. Wait for Idle.

To show ESP32 -> app: start a new game, then type this in ARDUINO SERIAL MONITOR, not the laptop terminal:

```text
board e2e4
```

White's pawn should move to e4 in the app. If the computer opponent is on, it should reply. Serial Monitor then prints App move received followed by that reply, status 1, and later status 0. The computer's move can be different each time.

You can also make a legal move by tapping the app's board and look for it in Serial Monitor.

Status values: 0 = Idle, 1 = Moving, 2 = Homing, 3 = Error.
