# Two-Square Sensor Test

This test uses two Hall effect sensors and one magnet to detect a piece moving between two locations, labeled e2 and e3. A full chessboard and gantry are not needed.

## Parts

- ESP32-WROOM-32 development board and USB cable
- Two A3144 Hall effect sensors
- Two 10 kΩ resistors
- Breadboard and jumper wires
- One magnet or magnetic chess piece

## Wiring

Unplug the ESP32’s USB before changing wires.

For our sensors, with the printed face toward you and legs pointing down:

| Connection | Sensor A: e2 | Sensor B: e3 |
| Left leg: power | VIN / 5 V | VIN / 5 V |
| Middle leg: ground | GND | GND |
| Right leg: output | D32 | D33 |
| Pull-up resistor | Output → 10 kΩ → 3V3 | Output → 10 kΩ → 3V3 |

Each sensor needs its own resistor. The right leg connects to both its GPIO pin and its resistor. Keep the two outputs separate, and place each sensor leg in a separate breadboard row.

## Running the Test

1. Open `hardware/firmware/Project64_Sensor_Testing/Project64_Sensor_Testing.ino` in Arduino IDE.
2. Set `USE_REAL_SENSORS = true`.
3. Select **ESP32 Dev Module** and the ESP32’s USB serial port.
4. Upload the sketch.
5. Open Serial Monitor at **115200 baud**, with **Newline** selected.

Uploading this sketch replaces the Bluetooth demo running on the ESP32. Both sketches are saved separately in the repository.

## Testing

Start with magnets away from both sensors.

1. Place the magnet near sensor A. Expect `READY: starting piece on e2`.
2. Lift it away from both sensors. Expect `LIFTED: e2`.
3. Place it near sensor B. Expect `MOVE: e2e3`.

Type `status` to check the state and completed move count. Type `reset` to clear tracking and establish a new starting position.

If a sensor does not respond, flip the magnet over. If both sensors detect the same magnet, increase their spacing and type `reset`.

## Current Progress

We tested the wired sensors and successfully detected a magnet moving between the two locations through Serial Monitor.

This sketch does not send sensor moves over Bluetooth or control motors yet. Connecting the sensor detection to the existing Bluetooth communication is the next step.