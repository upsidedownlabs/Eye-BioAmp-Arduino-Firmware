# EOG Blink & Gaze Morse Keyboard – Type with Your Eyes 👁️⌨️

Type letters and numbers using nothing but your **blinks and eye movements**! This project detects blinks and left/right gaze via **Electrooculography (EOG)** signals using the **BioAmp EXG Pill**, encodes them as **Morse code**, and outputs the result as real keystrokes over **USB HID** - turning your eyes into a hands-free keyboard.

> ⚠️ **Experimental:** This is a two-channel, threshold-based EOG detector with guided per-session calibration. It's a solid starting point for eye-controlled text entry but will need calibration and tuning per person/setup.

- **Blink** → Morse dot / dash entry
- **Look left** → commit buffered Morse pattern as a typed character
- **Look right** → typed space
- **Triple blink** → clear the buffer (undo)

---

## Table of Contents
- [How It Works](#how-it-works)
- [Hardware Required](#hardware-required)
- [Software Required](#software-required)
- [Hardware Setup](#hardware-setup)
- [Skin Preparation & Electrode Placement](#skin-preparation--electrode-placement)
- [Uploading the Arduino Code](#uploading-the-arduino-code)
- [Calibrating & Using the Keyboard](#calibrating--using-the-keyboard)
- [Controls](#controls)

---

## How It Works

This project builds a **two-channel EOG acquisition system**:

- **Channel 1 (A0)** records vertical EOG near the eye (used to detect blinks)
- **Channel 2 (A1)** records horizontal EOG across both eyes (used to detect left/right gaze)

The Arduino samples both channels at **512 Hz**. Channel 1 runs through a **notch filter** and an **EOG-specific filter**, then a 100 ms moving-average **envelope**; a blink is flagged whenever the envelope crosses a hysteresis threshold (rises above 50.0, must fall back below 30.0 to re-arm). Channel 2 runs through a **10 Hz low-pass filter**, a continuously adapting **baseline tracker**, and a **direction state machine** that confirms a sustained left/right gaze before firing.

Blinks accumulate into a small Morse buffer:

| Blink Pattern | Meaning |
|---|---|
| 1 blink, pause | Dot (`.`) |
| 2 blinks, pause | Dash (`-`) |
| 3 blinks quickly | Clear buffer (undo) |

Gaze events act on that buffer:

| Gaze | Meaning |
|---|---|
| Look left | Commit buffer as a typed character (via Morse lookup) |
| Look right | Type a space |

Unlike a serial-plus-Python setup, this board **types directly** - it enumerates as a native USB HID keyboard, so whatever key it "presses" lands right in whatever app or text field has focus on the host computer. No companion script is required.

---

## Hardware Required

- **2× BioAmp EXG Pill** (Assembled) - one per channel, since the standard Pill is single-channel
- Arduino board with **native USB HID support** (e.g. Arduino Leonardo, Micro, Pro Micro, or Uno R4 Minima) - a plain Uno R3 cannot run the `Keyboard` library
- BioAmp Cable(s) to connect each Pill to its electrodes
- Gel Electrodes (6 total: 2 positive, 2 negative, and 1-2 shared reference)
- NuPrep Skin Preparation Gel
- Alcohol/Wet Wipes
- USB Cable (for Arduino)

> 🛒 BioAmp EXG Pill and accessories available from: [Upside Down Labs Store](https://store.upsidedownlabs.tech/), [Amazon India](https://www.amazon.in/), [Robu India](https://robu.in/), [Tindie](https://www.tindie.com/stores/upsidedownlabs/), or [DigiKey](https://www.digikey.in/).

---

## Software Required

- [Arduino IDE](https://www.arduino.cc/en/software)
- Board package matching your Arduino (e.g. for Uno R4 Minima: `Tools → Board → Boards Manager → Search "Arduino UNO R4 Boards" → Install`)
- Built-in `Keyboard` library (ships with the Arduino IDE - no extra install needed)
- No Python or companion script required - all logic runs on-device and outputs HID keystrokes directly

### Project Files
- `Blink_Morse.ino` – Arduino firmware (dual-channel EOG acquisition, blink/gaze detection, Morse-to-HID output)

---

## Hardware Setup

### Step 1: Wire the Vertical (Blink) Channel

Connect one **BioAmp EXG Pill** to the Arduino's **A0** pin, with its own electrode leads (IN+, IN−, REF).

### Step 2: Wire the Horizontal (Gaze) Channel

Connect the second **BioAmp EXG Pill** to the Arduino's **A1** pin, with its own electrode leads (IN+, IN−, REF).

### Step 3: Tie the References Together

Connect both Pills' **REF/GND** leads and the Arduino's **GND** to the same shared reference electrode on the body. A mismatched reference between the two boards is the most common cause of a drifting or uncalibratable gaze channel.

---

## Skin Preparation & Electrode Placement

This is a **two-channel EOG system** placed around both eyes.

### Skin Prep
1. Apply **NuPrep Skin Preparation Gel** to each site where electrodes will be placed (removes dead skin and dirt).
2. Rub thoroughly, then wipe clean with an alcohol wipe or wet wipe.
3. Let the skin dry fully before pressing an electrode down, and seat each one from the center outward to avoid trapped air.

### Electrode Placement

| Channel | Electrode | Placement |
|---|---|---|
| Vertical (A0, blinks) | Positive (+) | Center of the forehead |
| Vertical (A0, blinks) | Reference/Ground | One electrode behind each ear (behind left ear and behind right ear) |
| Horizontal (A1, gaze) | Negative (−) | Beside the left eye |
| Horizontal (A1, gaze) | Positive (+) | Beside the right eye |
| Horizontal (A1, gaze) | Reference | A bony, electrically neutral area (e.g., behind the ear/mastoid) |

For an exact placement tutorial visit [Upside Down Labs documentation](https://docs.upsidedownlabs.tech/guides/usage-guides/using-gel-electrodes/index.html#using-gel-electrodes)

---

## Uploading the Arduino Code

1. Copy the `12_Blink_Morse.ino` code into the Arduino IDE.
2. Install the board package matching your Arduino:
   `Tools → Board → Boards Manager → search for your board → Install latest`
   - Accept any installation/permission prompts.
3. Connect the Arduino to your laptop via USB.
4. Select the board: `Tools → Board → [your board]`
5. Select the correct port: `Tools → Port` (disconnect/reconnect the Arduino if the port isn't listed).
6. Click **Upload** (→) and wait for it to complete.

> ⚠️ **Important:** For best signal quality, **unplug your laptop charger** and sit **at least 5 meters away from AC appliances** during use - this minimizes electrical interference (50/60 Hz noise) on both EOG channels.

> 🔴 **Critical step:** As soon as upload finishes, the board **automatically starts its guided calibration sequence** - no button press needed. Open the Serial Monitor at 115200 baud right away so you don't miss the countdown and step prompts.

---

## Calibrating & Using the Keyboard

1. Make sure the Arduino is uploaded with the firmware and connected via USB.
2. Open the **Serial Monitor at 115200 baud** immediately after upload/reset.
3. Follow the guided calibration prompts, holding each gaze steady for its full window:
   - Look **straight ahead** (4 s)
   - Look **RIGHT** and hold (3 s)
   - Look **LEFT** and hold (3 s)
4. Wait for `CALIBRATION COMPLETE - SYSTEM READY` in the Serial Monitor - this also prints your derived thresholds for reference.
5. Click into whatever app or text field you want to type into (any text editor, browser field, etc.) so it has focus.
6. Blink and gaze to type - each committed character or space is sent as a real keystroke.

> 💡 You can adjust `HID_COOLDOWN_MS` (default `1000` ms) in the firmware to control the minimum time between emitted keystrokes - lower values feel snappier but may allow rapid double-triggers; higher values are safer but may feel sluggish during fast typing.

---

## Controls

| Event | Detection | Action | Result |
|---|---|---|---|
| 1 blink, then pause | Vertical envelope crosses threshold once | Buffer dot | `.` added to buffer |
| 2 blinks, then pause | Vertical envelope crosses threshold twice | Buffer dash | `-` added to buffer |
| 3 blinks quickly | Vertical envelope crosses threshold three times | Clear buffer | Buffer reset (undo) |
| Look left | Horizontal signal past positive threshold | Commit character | Typed keystroke from Morse lookup |
| Look right | Horizontal signal past negative threshold | Space | `Spacebar` keystroke |

