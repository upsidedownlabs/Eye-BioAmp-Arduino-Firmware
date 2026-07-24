// Blink_Morse - BioAmp EXG Pill
// https://github.com/upsidedownlabs/BioAmp-EXG-Pill

// Upside Down Labs invests time and resources providing this open source code,
// please support Upside Down Labs and open-source hardware by purchasing
// products from Upside Down Labs!

// Copyright (c) 2021 Upside Down Labs - contact@upsidedownlabs.tech

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// At Upside Down Labs, we create open-source DIY neuroscience hardware and software.
// Our mission is to make neuroscience affordable and accessible for everyone.
// By supporting us with your purchase, you help spread innovation and open science.
// Thank you for being part of this journey with us!

#include <Arduino.h>
#include <Keyboard.h>  // HID keyboard library for native USB / R4 boards
#include <math.h>

// ----------------- USER CONFIGURATION -----------------
#define SAMPLE_RATE         512          // Samples per second
#define BAUD_RATE           115200
#define INPUT_PIN           A0           // Vertical EOG Channel (Blinks)
#define HORIZ_PIN           A1           // Horizontal EOG Channel (Left/Right)
#define LED_PIN             LED_BUILTIN

// Envelope Configuration for EOG Blink detection
#define ENVELOPE_WINDOW_MS  100
#define ENVELOPE_WINDOW_SIZE ((ENVELOPE_WINDOW_MS * SAMPLE_RATE) / 1000)

// Blink Detection Thresholds (Hysteresis)
const float BlinkLowerThreshold = 30.0;
const float BlinkUpperThreshold = 50.0;

// Timing Buffers for A0
#define BUFFER_SIZE 64
float eogCircBuffer[BUFFER_SIZE];
int writeIndex = 0;
int readIndex = 0;
int samplesAvailable = 0;

// Blink Detection Timing & State Variables
const unsigned long BLINK_DEBOUNCE_MS   = 200;
const unsigned long DOUBLE_BLINK_MS     = 600;
const unsigned long TRIPLE_WINDOW_MS   = 900;
const bool TYPE_SYMBOLS                 = false; // Set to true to directly HID-type '.' and '-'

unsigned long lastBlinkTime     = 0;
unsigned long firstBlinkTime    = 0;
unsigned long secondBlinkTime   = 0;
int           blinkCount         = 0;
bool          blinkArmed         = true;
float         currentEOGEnvelope = 0;
unsigned long ledPulseUntil      = 0;

// HID Command Cooldown
const unsigned long HID_COOLDOWN_MS = 1000;
unsigned long lastHIDCommandTime    = 0;

// Suppression Timers
unsigned long blink_suppress_until_ms = 0;

// ----------------- HORIZONTAL ACTIVE CALIBRATION & DETECTION -----------------
// Directional Debounce / Recoil Lockout Timer
const unsigned long DIRECTIONAL_COOLDOWN_MS = 600; // Ignores recoil/overshoot for 600ms after a gaze trigger
unsigned long directionLockoutUntil         = 0;

// Continuous DSP Filters for Horizontal EOG (A1)
float hBaseline = 0.0;
const float BASELINE_ALPHA = 0.002;
float hSmoothed = 0.0;
const float SMOOTH_ALPHA = 0.3;
bool baselineInitialized = false;

// Low-Pass Filter Alpha for Horizontal channel (Filters out sharp blink transients)
const float A1_LPF_CUTOFF_HZ = 10.0f;
float A1_LPF_alpha           = 0.0f;

// Calibration States & Timers
enum CalibPhase { CALIB_SETTLING, CALIB_RIGHT, CALIB_LEFT, CALIB_COMPLETE };
CalibPhase calibPhase = CALIB_SETTLING;
unsigned long calibPhaseStartTime = 0;

// Dynamic Threshold Values
float POSITIVE_THRESHOLD = 0.0;
float NEGATIVE_THRESHOLD = 0.0;
float EXIT_THRESHOLD     = 0.0;

// Dynamic Peak Capture
float maxPositivePeak = 0.0;
float maxNegativePeak = 0.0;

// Detection Parameters (Tuned for 512Hz)
const int CONFIRM_SAMPLES = 20; // ~40ms sustained gaze at 512Hz
int aboveCount = 0;
int belowCount = 0;

const int REARM_SAMPLES  = 128; // ~250ms near baseline at 512Hz
int rearmCount = 0;

const unsigned long REFRACTORY_MS = 1000;
unsigned long lastEventTime       = 0;

enum EOGState { NEUTRAL, LEFT_ACTIVE, RIGHT_ACTIVE };
EOGState hState = NEUTRAL;

// ----------------- MORSE BUFFER & HELPER FUNCTIONS -----------------
const uint8_t MAX_MORSE_LEN = 8;
char morseBuf[MAX_MORSE_LEN + 1] = {0};
uint8_t morseLen = 0;

float eogEnvelopeBuffer[ENVELOPE_WINDOW_SIZE] = {0};
int eogEnvelopeIndex = 0;
float eogEnvelopeSum = 0;

inline void appendMorseChar(char c) {
  if (morseLen < MAX_MORSE_LEN) {
    morseBuf[morseLen++] = c;
    morseBuf[morseLen] = '\0';
  }
}

inline void clearMorseBuf() {
  morseLen = 0;
  morseBuf[0] = '\0';
}

char morseToChar(const char* m) {
  static const struct { const char* morse; char letter; } morseTable[] = {
    {".-", 'A'}, {"-...", 'B'}, {"-.-.", 'C'}, {"-..", 'D'}, {".", 'E'},
    {"..-.", 'F'}, {"--.", 'G'}, {"....", 'H'}, {"..", 'I'}, {".---", 'J'},
    {"-.-", 'K'}, {".-..", 'L'}, {"--", 'M'}, {"-.", 'N'}, {"---", 'O'},
    {".--.", 'P'}, {"--.-", 'Q'}, {".-.", 'R'}, {"...", 'S'}, {"-", 'T'},
    {"..-", 'U'}, {"...-", 'V'}, {".--", 'W'}, {"-..-", 'X'}, {"-.--", 'Y'},
    {"--..", 'Z'}, {"-----", '0'}, {".----", '1'}, {"..---", '2'}, {"...--", '3'},
    {"....-", '4'}, {".....", '5'}, {"-....", '6'}, {"--...", '7'}, {"---..", '8'},
    {"----.", '9'}
  };

  for (size_t i = 0; i < sizeof(morseTable) / sizeof(morseTable[0]); i++) {
    if (strcmp(m, morseTable[i].morse) == 0) {
      return morseTable[i].letter;
    }
  }
  return 0;
}

inline bool timeReached(unsigned long nowMs, unsigned long deadlineMs) {
  return (long)(nowMs - deadlineMs) >= 0;
}

// ----------------- FILTERS -----------------
float Notch(float input) {
  float output = input;
  {
    static float z1, z2;
    float x = output - -1.58696045f * z1 - 0.96505858f * z2;
    output = 0.96588529f * x + -1.57986211f * z1 + 0.96588529f * z2;
    z2 = z1; z1 = x;
  }
  {
    static float z1, z2;
    float x = output - -1.62761184f * z1 - 0.96671306f * z2;
    output = 1.00000000f * x + -1.63566226f * z1 + 1.00000000f * z2;
    z2 = z1; z1 = x;
  }
  return output;
}

float EOGFilter(float input) {
  float output = input;
  {
    static float z1, z2;
    float x = output - -1.91327599f * z1 - 0.91688335f * z2;
    output = 0.95753983f * x + -1.91507967f * z1 + 0.95753983f * z2;
    z2 = z1; z1 = x;
  }
  return output;
}

float HorizontalLPF(float input) {
  static float y = 0.0f;
  y += A1_LPF_alpha * (input - y);
  return y;
}

float updateEOGEnvelope(float sample) {
  float absSample = fabs(sample);
  eogEnvelopeSum -= eogEnvelopeBuffer[eogEnvelopeIndex];
  eogEnvelopeSum += absSample;
  eogEnvelopeBuffer[eogEnvelopeIndex] = absSample;
  eogEnvelopeIndex = (eogEnvelopeIndex + 1) % ENVELOPE_WINDOW_SIZE;
  return eogEnvelopeSum / ENVELOPE_WINDOW_SIZE;
}

// ----------------- HID ACTION HANDLERS -----------------
void sendDot() {
  appendMorseChar('.');
  Serial.print("Morse += . -> current: "); Serial.println(morseBuf);

  unsigned long nowMs = millis();
  if (TYPE_SYMBOLS && (nowMs - lastHIDCommandTime) >= HID_COOLDOWN_MS) {
    Keyboard.write('.');
    lastHIDCommandTime = nowMs;
  }
}

void sendDash() {
  appendMorseChar('-');
  Serial.print("Morse += - -> current: "); Serial.println(morseBuf);

  unsigned long nowMs = millis();
  if (TYPE_SYMBOLS && (nowMs - lastHIDCommandTime) >= HID_COOLDOWN_MS) {
    Keyboard.write('-');
    lastHIDCommandTime = nowMs;
  }
}

// ----------------- SETUP & LOOP -----------------
void setup() {
  Serial.begin(BAUD_RATE);
  while (!Serial && millis() < 3000);

  pinMode(INPUT_PIN, INPUT);
  pinMode(HORIZ_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

  // Compute LPF Alpha
  A1_LPF_alpha = (2.0f * PI * A1_LPF_CUTOFF_HZ) / ((2.0f * PI * A1_LPF_CUTOFF_HZ) + SAMPLE_RATE);

  Keyboard.begin();

  // Startup guided calibration sequence
  delay(1000);
  Serial.println("\n\n==================================================");
  Serial.println("     STARTING ACTIVE EOG CALIBRATION SYSTEM       ");
  Serial.println("==================================================");
  Serial.println("Starting in 3 seconds... Get ready!");
  delay(1000); Serial.println("3...");
  delay(1000); Serial.println("2...");
  delay(1000); Serial.println("1...");
  delay(1000);

  Serial.println("\n--> STEP 1: Look STRAIGHT ahead (Center) [Hold 4s]...");
  calibPhaseStartTime = millis();
}

void loop() {
  static unsigned long lastMicros = 0;
  static long timer = 0;

  unsigned long nowMs = millis();

  // Non-blocking LED pulse indicator
  if (!timeReached(nowMs, ledPulseUntil)) digitalWrite(LED_PIN, HIGH);
  else digitalWrite(LED_PIN, LOW);

  // Exact 512 Hz Sampling Loop Engine
  unsigned long currentMicros = micros();
  long interval = (long)(currentMicros - lastMicros);
  lastMicros = currentMicros;

  timer -= interval;
  const long period = 1000000L / SAMPLE_RATE;

  while (timer < 0) {
    timer += period;

    // ----------------------------------------------------
    // 1. VERTICAL EOG (A0) - BLINK / MORSE PROCESSING
    // ----------------------------------------------------
    int rawA0 = analogRead(INPUT_PIN);
    float filteredA0 = Notch(rawA0);
    float eogA0 = EOGFilter(filteredA0);
    eogCircBuffer[writeIndex] = eogA0;
    writeIndex = (writeIndex + 1) % BUFFER_SIZE;
    if (samplesAvailable < BUFFER_SIZE) samplesAvailable++;

    // ----------------------------------------------------
    // 2. HORIZONTAL EOG (A1) - ACTIVE DSP & CALIBRATION
    // ----------------------------------------------------
    int rawA1 = analogRead(HORIZ_PIN);
    float filteredA1 = HorizontalLPF((float)rawA1);
    float rawVoltageA1 = filteredA1 * (5.0 / 1023.0);

    if (!baselineInitialized) {
      hBaseline = rawVoltageA1;
      hSmoothed = 0.0;
      baselineInitialized = true;
    }

    // Continuous High-Pass and Low-Pass Filters for A1
    hBaseline += BASELINE_ALPHA * (rawVoltageA1 - hBaseline);
    float highPassedA1 = rawVoltageA1 - hBaseline;
    hSmoothed += SMOOTH_ALPHA * (highPassedA1 - hSmoothed);

    // Active Guided Calibration Routine
    if (calibPhase != CALIB_COMPLETE) {
      unsigned long elapsed = nowMs - calibPhaseStartTime;

      switch (calibPhase) {
        case CALIB_SETTLING:
          if (elapsed >= 4000) {
            calibPhase = CALIB_RIGHT;
            calibPhaseStartTime = nowMs;
            maxPositivePeak = 0.0;
            maxNegativePeak = 0.0;
            Serial.println("\n--> STEP 2: Look RIGHT and HOLD your gaze! [Hold 3s]");
          }
          break;

        case CALIB_RIGHT:
          if (hSmoothed > maxPositivePeak) maxPositivePeak = hSmoothed;
          if (hSmoothed < maxNegativePeak) maxNegativePeak = hSmoothed;

          if (elapsed >= 3000) {
            calibPhase = CALIB_LEFT;
            calibPhaseStartTime = nowMs;
            Serial.println("\n--> STEP 3: Look LEFT and HOLD your gaze! [Hold 3s]");
          }
          break;

        case CALIB_LEFT:
          if (hSmoothed > maxPositivePeak) maxPositivePeak = hSmoothed;
          if (hSmoothed < maxNegativePeak) maxNegativePeak = hSmoothed;

          if (elapsed >= 3000) {
            POSITIVE_THRESHOLD = maxPositivePeak * 0.50;
            NEGATIVE_THRESHOLD = maxNegativePeak * 0.50;

            float minAbsPeak = min(fabs(maxPositivePeak), fabs(maxNegativePeak));
            EXIT_THRESHOLD = minAbsPeak * 0.20;

            calibPhase = CALIB_COMPLETE;

            Serial.println("\n==================================================");
            Serial.println("        CALIBRATION COMPLETE - SYSTEM READY       ");
            Serial.println("==================================================");
            Serial.print("Positive Threshold (+): "); Serial.println(POSITIVE_THRESHOLD, 4);
            Serial.print("Negative Threshold (-): "); Serial.println(NEGATIVE_THRESHOLD, 4);
            Serial.print("Exit Threshold       : "); Serial.println(EXIT_THRESHOLD, 4);
            Serial.println("--------------------------------------------------\n");
          }
          break;

        case CALIB_COMPLETE:
          break;
      }
      continue;
    }

    // ----------------------------------------------------
    // 3. HORIZONTAL MOVEMENT DETECTION STATE MACHINE
    // ----------------------------------------------------
    bool inRefractory = (nowMs - lastEventTime < REFRACTORY_MS);
    bool inLockout    = !timeReached(nowMs, directionLockoutUntil);
    bool isBlinking   = (currentEOGEnvelope >= BlinkLowerThreshold);

    switch (hState) {
      case NEUTRAL:
        // Ignore triggers during refractory, directional lockout recoil window, or vertical blinks
        if (inRefractory || inLockout || isBlinking) {
          aboveCount = 0;
          belowCount = 0;
          break;
        }

        if (hSmoothed > POSITIVE_THRESHOLD) {
          aboveCount++;
          belowCount = 0;
          if (aboveCount >= CONFIRM_SAMPLES) {
            hState = LEFT_ACTIVE;
            lastEventTime = nowMs;
            directionLockoutUntil = nowMs + DIRECTIONAL_COOLDOWN_MS; // Lock out reverse overshoot
            aboveCount = 0;
            rearmCount = 0;
            blink_suppress_until_ms = nowMs + 300;

            // Execute Morse Commit on LEFT
            char out = morseToChar(morseBuf);
            Serial.print("<<< LEFT <<< -> Commit Character: '"); 
            Serial.print(out ? out : '?'); 
            Serial.print("' from Morse: '"); Serial.print(morseBuf); Serial.println("'");

            if (out && (nowMs - lastHIDCommandTime) >= HID_COOLDOWN_MS) {
              Keyboard.write(out);
              lastHIDCommandTime = nowMs;
            }
            clearMorseBuf();
          }
        }
        else if (hSmoothed < NEGATIVE_THRESHOLD) {
          belowCount++;
          aboveCount = 0;
          if (belowCount >= CONFIRM_SAMPLES) {
            hState = RIGHT_ACTIVE;
            lastEventTime = nowMs;
            directionLockoutUntil = nowMs + DIRECTIONAL_COOLDOWN_MS; // Lock out reverse overshoot
            belowCount = 0;
            rearmCount = 0;
            blink_suppress_until_ms = nowMs + 300;

            // Execute Space Output on RIGHT
            Serial.println(">>> RIGHT >>> -> Output Space");
            if ((nowMs - lastHIDCommandTime) >= HID_COOLDOWN_MS) {
              Keyboard.write(' ');
              lastHIDCommandTime = nowMs;
            }
          }
        } else {
          aboveCount = 0;
          belowCount = 0;
        }
        break;

      case RIGHT_ACTIVE:
      case LEFT_ACTIVE:
        if (fabs(hSmoothed) < EXIT_THRESHOLD) {
          rearmCount++;
          if (rearmCount >= REARM_SAMPLES) {
            hState = NEUTRAL;
            rearmCount = 0;
          }
        } else {
          rearmCount = 0;
        }
        break;
    }
  }

  // Pause blink processing until active calibration completes
  if (calibPhase != CALIB_COMPLETE) return;

  // ----------------------------------------------------
  // 4. CIRCULAR BUFFER & ENVELOPE CALCULATION
  // ----------------------------------------------------
  while (samplesAvailable > 0) {
    float eog = eogCircBuffer[readIndex];
    readIndex = (readIndex + 1) % BUFFER_SIZE;
    samplesAvailable--;

    currentEOGEnvelope = updateEOGEnvelope(eog);
  }

  // ----------------------------------------------------
  // 5. BLINK DETECTION AND MORSE CODE HANDLER
  // ----------------------------------------------------
  if (timeReached(nowMs, blink_suppress_until_ms) &&
      blinkArmed &&
      currentEOGEnvelope >= BlinkUpperThreshold &&
      (nowMs - lastBlinkTime) >= BLINK_DEBOUNCE_MS) {

    blinkArmed = false;
    lastBlinkTime = nowMs;

    if (blinkCount == 0) {
      firstBlinkTime = nowMs;
      blinkCount = 1;
      Serial.println("Blink detected: count=1");
    } 
    else if (blinkCount == 1 && (nowMs - firstBlinkTime) <= DOUBLE_BLINK_MS) {
      secondBlinkTime = nowMs;
      blinkCount = 2;
      Serial.println("Blink detected: count=2");
    } 
    else if (blinkCount == 2) {
      if ((nowMs - firstBlinkTime) <= TRIPLE_WINDOW_MS) {
        blinkCount = 3;
        clearMorseBuf();
        Serial.println("Triple-blink: Cleared morse buffer");

        ledPulseUntil = nowMs + 50;
        blinkCount = 0;
        firstBlinkTime = 0;
        secondBlinkTime = 0;
        lastBlinkTime = nowMs;
        return;
      } else {
        firstBlinkTime = nowMs;
        blinkCount = 1;
        Serial.println("Blink detected: Restarted count=1");
      }
    } else {
      firstBlinkTime = nowMs;
      blinkCount = 1;
      Serial.println("Blink detected: Restarted count=1");
    }
  }

  if (currentEOGEnvelope < BlinkLowerThreshold) {
    blinkArmed = true;
  }

  // Finalize Dash (2 blinks timeout)
  if (blinkCount == 2 && (nowMs - firstBlinkTime) > TRIPLE_WINDOW_MS) {
    Serial.println("Dash (2 blinks) finalized"); 
    sendDash();
    blinkCount = 0;
  }

  // Finalize Dot (1 blink timeout)
  if (blinkCount == 1 && (nowMs - firstBlinkTime) > DOUBLE_BLINK_MS) {
    Serial.println("Dot (single blink) finalized");
    sendDot();
    blinkCount = 0;
  }
}