// BlinkMorse
// https://github.com/dhurv21/BlinkMorse

// Copyright (c) 2025 Dhruv Tiwari - dhruv.copied@gmail.com
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



#include <Arduino.h>
#include <Keyboard.h>  // HID keyboard library for Arduino R4
#include <math.h>

// #define DEBUG  // Uncomment this line to enable debugging
// #define HDBG   // Uncomment for horizontal classifier debug (~75ms prints)

// ----------------- USER CONFIGURATION -----------------
#define SAMPLE_RATE       512          // samples per second
#define BAUD_RATE         115200
#define INPUT_PIN         A0
#define HORIZ_PIN         A1
#define LED_PIN           LED_BUILTIN

// Envelope Configuration for EOG detection
#define ENVELOPE_WINDOW_MS 100  // Smoothing window in milliseconds
#define ENVELOPE_WINDOW_SIZE ((ENVELOPE_WINDOW_MS * SAMPLE_RATE) / 1000)

// Blink Detection Thresholds - tune per hardware
// Hysteresis: a blink is registered when the envelope rises above the upper
// threshold, and re-armed only after it falls back below the lower threshold.
const float BlinkLowerThreshold = 30.0;
const float BlinkUpperThreshold = 50.0;

// Circular buffer for timing-based sampling
#define BUFFER_SIZE 64
float eogCircBuffer[BUFFER_SIZE];
int writeIndex = 0;
int readIndex = 0;
int samplesAvailable = 0;

// Blink Detection Configuration
const unsigned long BLINK_DEBOUNCE_MS   = 200;   // minimal spacing between individual blinks
const unsigned long DOUBLE_BLINK_MS     = 600;   // max time between two blinks to consider them a pair
const unsigned long TRIPLE_WINDOW_MS = 900; // commit window; user will tune 800–1000 later
const bool TYPE_SYMBOLS = false; // false: do NOT HID‑type '.' or '-'
unsigned long lastBlinkTime     = 0;             // time of most recent raw blink detection
unsigned long firstBlinkTime    = 0;             // time of the first blink in a sequence
unsigned long secondBlinkTime   = 0;
int         blinkCount         = 0;             // how many blinks detected in current sequence (0–2)
bool        blinkArmed         = true;          // true when envelope is below lower threshold (ready for next blink)
float currentEOGEnvelope = 0;
unsigned long ledPulseUntil = 0; // end time for a non‑blocking 50 ms LED pulse on commit

// Horizontal EOG (A1) Configuration
#define HBUF_SIZE 250
const uint16_t BASELINE_SAMPLES = 125;
float hBaseline = 0, hStd = 0;
bool hCalibrated = false;
const float DEVIATION_SIGMA = 5.0; // start permissive; can tune to 5–6 later
const uint16_t MIN_MOVEMENT_SAMPLES = 24;  // ~39 ms at 512 Hz; tune later to 24–28 once stable
const uint16_t COOLDOWN_SAMPLES = 30;
unsigned long blink_suppress_until_ms = 0; // suppress A0 blink detection during this window

// A1 simple 10 Hz low-pass (one-pole) after notch
const float A1_LPF_CUTOFF_HZ = 10.0f;
float A1_LPF_alpha = 0.0f;   // computed in setup

// Neutral-only slow baseline/std updates for A1
const float H_BASELINE_BETA = 0.003f;  // 0.2–0.5% per sample at 512 Hz is OK; tune 0.002–0.006
float hVar = 0.0f;                     // variance EMA for std

enum HLabel { H_NEUTRAL=0, H_LEFT=1, H_RIGHT=2 };
float hBuf[HBUF_SIZE];
int hWriteIndex = 0;
uint16_t hSamplesCollected = 0;
HLabel lastObserved = H_NEUTRAL;
uint16_t obsCount = 0;
HLabel lastAccepted = H_NEUTRAL;
uint16_t cooldown = 0;

// HID Command Cooldown to prevent rapid-fire commands
const unsigned long HID_COOLDOWN_MS = 1000;  // 1 second between HID outputs (adjustable)
unsigned long lastHIDCommandTime = 0;

// EOG Envelope Processing Variables
float eogEnvelopeBuffer[ENVELOPE_WINDOW_SIZE] = {0};
int eogEnvelopeIndex = 0;
float eogEnvelopeSum = 0;

// EOG Statistics for debug display
#define SEGMENT_SEC 1
#define SAMPLES_PER_SEGMENT (SAMPLE_RATE * SEGMENT_SEC)
float eogBuffer[SAMPLES_PER_SEGMENT] = {0};
uint16_t segmentIndex = 0;
unsigned long lastSegmentTimeMs = 0;
float eogAvg = 0, eogMin = 0, eogMax = 0;
bool segmentStatsReady = false;

// Morse buffer and helpers
const uint8_t MAX_MORSE_LEN = 8;
char morseBuf[MAX_MORSE_LEN + 1] = {0};
uint8_t morseLen = 0;

/**
 * @brief Append a single morse symbol to the buffer if space remains.
 * @param c The symbol to append ('.' or '-'); ignored if the buffer is full.
 */
inline void appendMorseChar(char c) { if (morseLen < MAX_MORSE_LEN) { morseBuf[morseLen++] = c; morseBuf[morseLen] = '\0'; } }

/**
 * @brief Reset the morse buffer to empty.
 */
inline void clearMorseBuf() { morseLen = 0; morseBuf[0] = '\0'; }

/**
 * @brief Remove and return the most recently appended morse symbol.
 * @return The removed symbol, or 0 if the buffer was already empty.
 */
char popMorseChar() { if (morseLen == 0) return 0; char c = morseBuf[--morseLen]; morseBuf[morseLen] = '\0'; return c; }

/**
 * @brief Decode a morse string into its corresponding character.
 * @param m Null-terminated morse string (dots and dashes), e.g. "-.-".
 * @return The decoded character (A–Z or 0–9), or 0 if no match is found.
 */
char morseToChar(const char* m) {
  // Implement International Morse for A–Z and 0–9; return 0 if not found.
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

  for (int i = 0; i < sizeof(morseTable) / sizeof(morseTable[0]); i++) {
    if (strcmp(m, morseTable[i].morse) == 0) {
      return morseTable[i].letter;
    }
  }
  return 0;
}

// --- Filter Functions ---
/**
 * @brief Mains-noise notch filter (48–52 Hz band-stop) for the A0 channel.
 * @param input Raw sample from the vertical (A0) EOG channel.
 * @return The band-stop filtered sample (second-order sections, dedicated state).
 */
float Notch(float input)
{
  float output = input;
  {
    static float z1, z2; // filter section state
    float x = output - -1.58696045*z1 - 0.96505858*z2;
    output = 0.96588529*x + -1.57986211*z1 + 0.96588529*z2;
    z2 = z1;
    z1 = x;
  }
  {
    static float z1, z2; // filter section state
    float x = output - -1.62761184*z1 - 0.96671306*z2;
    output = 1.00000000*x + -1.63566226*z1 + 1.00000000*z2;
    z2 = z1;
    z1 = x;
  }
  return output;
}

/**
 * @brief Mains-noise notch filter (48–52 Hz band-stop) for the A1 channel.
 * @param input Raw sample from the horizontal (A1) EOG channel.
 * @return The band-stop filtered sample (separate filter state from Notch()).
 */
float NotchA1(float input)
{
  float output = input;
  {
    static float z1, z2; // filter section state
    float x = output - -1.58696045*z1 - 0.96505858*z2;
    output = 0.96588529*x + -1.57986211*z1 + 0.96588529*z2;
    z2 = z1;
    z1 = x;
  }
  {
    static float z1, z2; // filter section state
    float x = output - -1.62761184*z1 - 0.96671306*z2;
    output = 1.00000000*x + -1.63566226*z1 + 1.00000000*z2;
    z2 = z1;
    z1 = x;
  }
  return output;
}

/**
 * @brief 5 Hz high-pass filter isolating the A0 blink signal from drift.
 * @param input Notch-filtered sample from the A0 channel.
 * @return The high-pass filtered sample.
 */
float EOGFilter(float input)
{
  float output = input;
  {
    static float z1, z2; // filter section state
    float x = output - -1.91327599*z1 - 0.91688335*z2;
    output = 0.95753983*x + -1.91507967*z1 + 0.95753983*z2;
    z2 = z1;
    z1 = x;
  }
  return output;
}

/**
 * @brief One-pole 10 Hz low-pass filter smoothing the A1 horizontal signal.
 * @param input Notch-filtered sample from the A1 channel.
 * @return The low-pass filtered sample (uses A1_LPF_alpha from setup()).
 */
float EOGFilterA1(float input)
{
  static float y = 0.0f;
  y += A1_LPF_alpha * (input - y);
  return y;
}

/**
 * @brief Update the moving-average envelope of the rectified A0 signal.
 * @param sample Latest filtered A0 sample (sign is removed internally).
 * @return The current envelope value (mean of |sample| over the window).
 */
float updateEOGEnvelope(float sample)
{
  float absSample = fabs(sample);

  // Update circular buffer and running sum
  eogEnvelopeSum -= eogEnvelopeBuffer[eogEnvelopeIndex];
  eogEnvelopeSum += absSample;
  eogEnvelopeBuffer[eogEnvelopeIndex] = absSample;
  eogEnvelopeIndex = (eogEnvelopeIndex + 1) % ENVELOPE_WINDOW_SIZE;

  return eogEnvelopeSum / ENVELOPE_WINDOW_SIZE;  // Return moving average
}

/**
 * @brief Rollover-safe check of whether a millis() deadline has passed.
 * @param nowMs Current millis() timestamp.
 * @param deadlineMs The deadline timestamp to compare against.
 * @return true once nowMs has reached or passed deadlineMs (handles wrap-around).
 */
inline bool timeReached(unsigned long nowMs, unsigned long deadlineMs) {
  return (long)(nowMs - deadlineMs) >= 0;
}

// HID send functions
/**
 * @brief Append a dot to the morse buffer and optionally HID-type it.
 *
 * Logs the updated buffer; only writes '.' over USB HID when TYPE_SYMBOLS is
 * true and the HID cooldown has elapsed.
 */
void sendDot() {
  appendMorseChar('.');
  Serial.print("Morse += . -> current: "); Serial.println(morseBuf);

  unsigned long nowMs = millis();
  if (TYPE_SYMBOLS == true && (nowMs - lastHIDCommandTime) >= HID_COOLDOWN_MS) {
    Keyboard.write('.');   // sends a dot to the focused application
    Serial.println("HID: Sent '.'");
    lastHIDCommandTime = nowMs;
  } else {
    if (TYPE_SYMBOLS == false) {
      Serial.println("HID: Dot not typed (TYPE_SYMBOLS = false)");
    } else {
      Serial.println("HID: Dot suppressed due to cooldown");
    }
  }
}

/**
 * @brief Append a dash to the morse buffer and optionally HID-type it.
 *
 * Logs the updated buffer; only writes '-' over USB HID when TYPE_SYMBOLS is
 * true and the HID cooldown has elapsed.
 */
void sendDash() {
  appendMorseChar('-');
  Serial.print("Morse += - -> current: "); Serial.println(morseBuf);

  unsigned long nowMs = millis();
  if (TYPE_SYMBOLS == true && (nowMs - lastHIDCommandTime) >= HID_COOLDOWN_MS) {
    Keyboard.write('-');   // sends a dash (minus) to the focused application
    Serial.println("HID: Sent '-'");
    lastHIDCommandTime = nowMs;
  } else {
    if (TYPE_SYMBOLS == false) {
      Serial.println("HID: Dash not typed (TYPE_SYMBOLS = false)");
    } else {
      Serial.println("HID: Dash suppressed due to cooldown");
    }
  }
}

/**
 * @brief Arduino setup: init serial, pins, A1 filter alpha, HID, and LED cue.
 *
 * Runs once at boot. Computes the A1 low-pass alpha, starts the HID keyboard,
 * plays the LED startup sequence, and prints usage instructions.
 */
void setup() {
  Serial.begin(BAUD_RATE);
  delay(100);

  pinMode(INPUT_PIN, INPUT);
  pinMode(HORIZ_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

  Serial.println("H: calibrating...");

  // Compute one-pole LPF alpha for A1
  A1_LPF_alpha = (2.0f * PI * A1_LPF_CUTOFF_HZ) / ((2.0f * PI * A1_LPF_CUTOFF_HZ) + SAMPLE_RATE);

  // Initialize HID Keyboard
  Keyboard.begin();

  // LED startup sequence (visual confirmation)
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);
  delay(300);
  digitalWrite(LED_PIN, HIGH);
  delay(300);
  digitalWrite(LED_PIN, LOW);

  lastSegmentTimeMs = millis();  // Initialize the segment timer

  Serial.println("Arduino R4 Morse Blink HID Controller Ready!");
  Serial.println("1 Blink = Dot (.)");
  Serial.println("2 Blinks = Dash (-)");
  Serial.println("3 Blinks = Delete last symbol");
  Serial.println("Make sure a text field is focused on your computer to receive keystrokes.");
}

/**
 * @brief Main loop: sample both EOG channels, classify, and decode morse.
 *
 * Performs timing-based sampling at SAMPLE_RATE, filters A0 (blinks) and A1
 * (horizontal), calibrates and runs the A1 left/right classifier, builds the
 * envelope, detects blinks via hysteresis, and emits dots/dashes or commits
 * characters/spaces over HID.
 */
void loop() {
    static unsigned long lastMicros = 0;
    static long timer = 0;

    unsigned long nowMs = millis();

    // Non-blocking LED pulse
    if (!timeReached(nowMs, ledPulseUntil)) digitalWrite(LED_PIN, HIGH); else digitalWrite(LED_PIN, LOW);

    // Timing-based sampling for SAMPLE_RATE
    unsigned long currentMicros = micros();
    long interval = (long)(currentMicros - lastMicros);
    lastMicros = currentMicros;

    timer -= interval;
    const long period = 1000000L / SAMPLE_RATE;
    while (timer < 0) {
        timer += period;
        int raw = analogRead(INPUT_PIN);
        float filtered = Notch(raw);
        float eog = EOGFilter(filtered);
        eogCircBuffer[writeIndex] = eog;
        writeIndex = (writeIndex + 1) % BUFFER_SIZE;
        if (samplesAvailable < BUFFER_SIZE) {
            samplesAvailable++;
        }

        // A1 horizontal EOG sampling
        int rawA1 = analogRead(HORIZ_PIN);
        float fNotchA1 = NotchA1((float)rawA1);
        float eogA1 = EOGFilterA1(fNotchA1);

        // Push to A1 classifier buffer
        hBuf[hWriteIndex] = eogA1;
        hWriteIndex = (hWriteIndex + 1) % HBUF_SIZE;
        if (hSamplesCollected < HBUF_SIZE) {
            hSamplesCollected++;
        }

        // Calibrate baseline once we have enough samples
        if (!hCalibrated && hSamplesCollected >= BASELINE_SAMPLES) {
            // Compute mean baseline and std
            float sum = 0;
            for (int i = 0; i < BASELINE_SAMPLES; i++) {
                sum += hBuf[i];
            }
            hBaseline = sum / BASELINE_SAMPLES;

            float variance = 0;
            for (int i = 0; i < BASELINE_SAMPLES; i++) {
                float diff = hBuf[i] - hBaseline;
                variance += diff * diff;
            }
            hStd = sqrt(variance / BASELINE_SAMPLES);
            hVar = variance / BASELINE_SAMPLES; // seed adaptive variance to match calibration
            hCalibrated = true;

            Serial.print("H: calibrated baseline="); Serial.print(hBaseline);
            Serial.print(", std="); Serial.println(hStd);
        }

        // A1 horizontal classifier (after calibration)
        if (hCalibrated) {
            if (hStd < 1e-3f) { hStd = 1e-3f; } // avoid zero/near-zero std corner
            float deviation = eogA1 - hBaseline;
            float thr = DEVIATION_SIGMA * hStd;
            if (thr < 6.0f) thr = 6.0f; // absolute floor to avoid hypersensitivity on tiny std
            HLabel label = (deviation > thr) ? H_RIGHT : (deviation < -thr) ? H_LEFT : H_NEUTRAL;

            #ifdef HDBG
            static unsigned long lastHDbgPrint = 0;
            if ((nowMs - lastHDbgPrint) >= 75) {
                Serial.print("H dev="); Serial.print(deviation);
                Serial.print(" thr="); Serial.print(thr);
                Serial.print(" lbl="); Serial.print((int)label);
                Serial.print(" obs="); Serial.print(obsCount);
                Serial.print(" acc="); Serial.print((int)lastAccepted);
                Serial.print(" cd="); Serial.println(cooldown);
                lastHDbgPrint = nowMs;
            }
            #endif

            // Neutral-only slow baseline and std tracking (recompute only when stable)
            if (lastAccepted == H_NEUTRAL && label == H_NEUTRAL && cooldown == 0) {
              float err = eogA1 - hBaseline;
              hBaseline += H_BASELINE_BETA * err;
              // Update variance EMA around the (updated) baseline
              hVar = (1.0f - H_BASELINE_BETA) * hVar + H_BASELINE_BETA * (err * err);
              hStd = sqrtf(hVar);
            }

            // Update observation count
            if (label == lastObserved) {
                obsCount++;
            } else {
                lastObserved = label;
                obsCount = 1;
            }

            // Decrement cooldown
            if (cooldown > 0) cooldown--;

            // Accept when label != H_NEUTRAL && obsCount >= MIN_MOVEMENT_SAMPLES && cooldown == 0
            if (label != H_NEUTRAL && obsCount >= MIN_MOVEMENT_SAMPLES && cooldown == 0 && lastAccepted == H_NEUTRAL) {
                if (label == H_LEFT) {
                    char out = morseToChar(morseBuf);
                    Serial.print("H: commit LEFT -> decode '"); Serial.print(out ? out : '?'); Serial.print("' from '"); Serial.print(morseBuf); Serial.println("'");
                    if (out && (nowMs - lastHIDCommandTime) >= HID_COOLDOWN_MS) {
                        Keyboard.write(out);
                        lastHIDCommandTime = nowMs;
                    }
                    clearMorseBuf();
                } else if (label == H_RIGHT) {
                    Serial.println("H: commit RIGHT -> space");
                    if ((nowMs - lastHIDCommandTime) >= HID_COOLDOWN_MS) {
                        Keyboard.write(' ');
                        lastHIDCommandTime = nowMs;
                    }
                }

                lastAccepted = label;
                cooldown = COOLDOWN_SAMPLES;
                obsCount = 0;
                blink_suppress_until_ms = nowMs + 180; // 150–200 ms
            }

            // Re-arm on NEUTRAL
            if (label == H_NEUTRAL && obsCount >= MIN_MOVEMENT_SAMPLES && lastAccepted != H_NEUTRAL) {
                lastAccepted = H_NEUTRAL;
                obsCount = 0;
            }
        }
    }

    // Process available samples
    while (samplesAvailable > 0) {
        float eog = eogCircBuffer[readIndex];
        readIndex = (readIndex + 1) % BUFFER_SIZE;
        samplesAvailable--;

        // Process the sample (envelope calculation)
        currentEOGEnvelope = updateEOGEnvelope(eog);

        // Add to segment buffer for statistics
        if(segmentIndex < SAMPLES_PER_SEGMENT) {
            eogBuffer[segmentIndex] = currentEOGEnvelope;
            segmentIndex++;
        }
    }

    // Segment statistics (every SEGMENT_SEC seconds)
    if ((nowMs - lastSegmentTimeMs) >= (1000UL * SEGMENT_SEC)) {
        if(segmentIndex > 0) {
            eogMin = eogBuffer[0];
            eogMax = eogBuffer[0];
            float eogSum = 0;

            for (uint16_t i = 0; i < segmentIndex; i++) {
                float eogVal = eogBuffer[i];

                if (eogVal < eogMin) eogMin = eogVal;
                if (eogVal > eogMax) eogMax = eogVal;
                eogSum += eogVal;
            }

            eogAvg = eogSum / segmentIndex;
            segmentStatsReady = true;
        }

        lastSegmentTimeMs = nowMs;
        segmentIndex = 0;
    }

    // ===== BLINK DETECTION AND MORSE HID OUTPUT =====
    // Hysteresis: trigger on the upper threshold while armed; re-arm only after
    // the envelope falls back below the lower threshold (see falling-edge check below).
    if (timeReached(nowMs, blink_suppress_until_ms) &&
        blinkArmed &&
        currentEOGEnvelope >= BlinkUpperThreshold &&
        (nowMs - lastBlinkTime) >= BLINK_DEBOUNCE_MS) {

        blinkArmed = false;   // disarm until envelope falls back below lower threshold
        lastBlinkTime = nowMs;

        if (blinkCount == 0) {
            // First blink of sequence
            firstBlinkTime = nowMs;
            blinkCount = 1;
            Serial.println("Blink detected: count=1");
        }
        else if (blinkCount == 1 && (nowMs - firstBlinkTime) <= DOUBLE_BLINK_MS) {
            // Second blink within time window
            secondBlinkTime = nowMs;
            blinkCount = 2;
            Serial.println("Blink detected: count=2");
        }
        else if (blinkCount == 2) {
            if ((nowMs - firstBlinkTime) <= TRIPLE_WINDOW_MS) {
                // Triple-blink clear
                blinkCount = 3;
                clearMorseBuf();
                Serial.println("Triple-blink: cleared morse buffer");

                ledPulseUntil = nowMs + 50; // brief non‑blocking LED confirmation
                blinkCount = 0;
                firstBlinkTime = 0;
                secondBlinkTime = 0;
                lastBlinkTime = nowMs;
                return; // prevent dot/dash finalizers below from running in the same iteration
            } else {
                // Too late for triple - restart sequence
                firstBlinkTime = nowMs;
                blinkCount = 1;
                Serial.println("Blink detected: restarted count=1");
            }
        }
        else {
            // Either too late or extra blink - restart sequence
            firstBlinkTime = nowMs;
            blinkCount = 1;
            Serial.println("Blink detected: restarted count=1");
        }
    } else if (!timeReached(nowMs, blink_suppress_until_ms) && currentEOGEnvelope >= BlinkUpperThreshold) {
        static unsigned long lastSuppressLog = 0;
        if ((nowMs - lastSuppressLog) >= 1000) {
            Serial.println("Blink suppressed (horizontal cooldown)");
            lastSuppressLog = nowMs;
        }
    }

    // Re-arm on the falling edge once the envelope returns below the lower threshold.
    if (currentEOGEnvelope < BlinkLowerThreshold) {
        blinkArmed = true;
    }

    // If we had 2 blinks but no third arrived before the triple window closed, treat as dash.
    // Wait the full TRIPLE_WINDOW_MS (measured from the first blink) so an early
    // second blink can't cut off the triple-blink (clear) window.
    if (blinkCount == 2 && (nowMs - firstBlinkTime) > TRIPLE_WINDOW_MS) {
        Serial.println("Dash (2 blinks) finalized");
        sendDash();
        blinkCount = 0;
    }

    // If we never got the second blink in time, treat the first as dot
    if (blinkCount == 1 && (nowMs - firstBlinkTime) > DOUBLE_BLINK_MS) {
        Serial.println("Dot (single blink) finalized");
        sendDot();
        blinkCount = 0;
    }

    // Optional: Print EOG envelope value for debugging/calibration
    #ifdef DEBUG
    static unsigned long lastDebugPrint = 0;
    if ((nowMs - lastDebugPrint) >= 1000) {  // Every 1 second
        if (segmentStatsReady) {
            Serial.print("EOG: (Avg: "); Serial.print(eogAvg);
            Serial.print(", Min: "); Serial.print(eogMin);
            Serial.print(", Max: "); Serial.print(eogMax); Serial.println(")");
        } else {
            Serial.print("EOG: "); Serial.print(currentEOGEnvelope); Serial.println();
        }
        lastDebugPrint = nowMs;
    }
    #endif
}
