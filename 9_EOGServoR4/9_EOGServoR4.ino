// Claw Controller - BioAmp EXG Pill
// Use for Bionic (wolverine) claw as well
// https://github.com/upsidedownlabs/BioAmp-EXG-Pill

// Upside Down Labs invests time and resources providing this open source code,
// please support Upside Down Labs and open-source hardware by purchasing
// products from Upside Down Labs!

// Copyright (c) 2025 Krishnanshu Mittal - krishnanshu@upsidedownlabs.tech
// Copyright (c) 2025 Deepak Khatri - deepak@upsidedownlabs.tech
// Copyright (c) 2025 Upside Down Labs - contact@upsidedownlabs.tech

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

#if defined(ESP32) 
  // For ESP32 Servo library
  #include <ESP32Servo.h>
#else
  // Arduino Servo library
  #include <Servo.h>
#endif

// Samples per second
#define SAMPLE_RATE 512

// Make sure to set the same baud rate on your Serial Monitor/Plotter
#define BAUD_RATE 115200

// Change if not using A0 analog pin
#define INPUT_PIN A2

// Servo pin (Change as per your connection)
#define SERVO_PIN 2 // Pin2 for Muscle BioAmp Shield v0.3

// Blink Threshold value, different for each user
// Check by plotting EOG envelopee data on Serial plotter
#define BlinkThreshold 30

// Servo open & close angles
#define SERVO_OPEN 10
#define SERVO_CLOSE 90

#define ENVELOPE_WINDOW_MS 100  // Smoothing window in milliseconds
#define ENVELOPE_WINDOW_SIZE ((ENVELOPE_WINDOW_MS * SAMPLE_RATE) / 1000)

// EOG Envelope baseline value
// Minimum value without flexing hand
#define EOG_ENVELOPE_BASELINE 4

// EOG Envelope divider
// Minimum value increase required to turn on a single LED
#define EOG_ENVELOPE_DIVIDER 4

const unsigned long BLINK_DEBOUNCE_MS   = 250;   // minimal spacing between individual blinks
const unsigned long DOUBLE_BLINK_MS     = 600;   // max time between the two blinks
unsigned long lastBlinkTime     = 0;             // time of most recent blink
unsigned long firstBlinkTime    = 0;             // time of the first blink in a pair
int         blinkCount          = 0;             // how many valid blinks so far (0–2)

float envelopeBuffer[ENVELOPE_WINDOW_SIZE] = {0};
int envelopeIndex = 0;
float envelopeSum = 0;
float currentEOGEnvelope = 0;

Servo servo;
// Servo toggle flag
bool flag = 0;

// Muscle BioAmp Shield v0.3 LED pin numbers in-order
int led_bar[] = {8, 9, 10, 11, 12, 13};
int total_leds = sizeof(led_bar) / sizeof(led_bar[0]);

void setup() {
  // Serial connection begin
  Serial.begin(BAUD_RATE);
  // Initialize all the led_bar
    for (int i = 0; i < total_leds; i++) {
      pinMode(led_bar[i], OUTPUT);
    }
   servo.attach(SERVO_PIN);
   servo.write(0);
}

void loop() {
  // Calculate elapsed time
  static unsigned long past = 0;
  unsigned long present = micros();
  unsigned long interval = present - past;
  past = present;

  // Run timer
  static long timer = 0;
  timer -= interval;

  // Sample and get envelope
  if(timer < 0) {
    timer += 1000000 / SAMPLE_RATE;
   
    // RAW Values
    float sensor_value = analogRead(INPUT_PIN);
    
    float filtered = highpass(EOGFilter(Notch(sensor_value)));
    
    // EOG envelopee
    currentEOGEnvelope = EOGEnvelope(abs(filtered));

    // Update LED bar graph
    for(int i = 0; i<total_leds; i++){
      if(i>(currentEOGEnvelope/EOG_ENVELOPE_DIVIDER - EOG_ENVELOPE_BASELINE)){
          digitalWrite(led_bar[i], LOW);
      } else {
          digitalWrite(led_bar[i], HIGH);
      }
    }
     
    // Normal running state processing
    unsigned long nowMs = millis();

    // 1) Did we cross threshold and respect per‑blink debounce?
  if (currentEOGEnvelope > BlinkThreshold && (nowMs - lastBlinkTime) >= BLINK_DEBOUNCE_MS) {
    lastBlinkTime = nowMs;    // mark this blink

    // 2) Count it
    if (blinkCount == 0) {
      // first blink of the pair
      firstBlinkTime = nowMs;
      blinkCount = 1;
    }
    else if (blinkCount == 1 && (nowMs - firstBlinkTime) <= DOUBLE_BLINK_MS) {
      if(flag == 1){
        servo.write(SERVO_OPEN);
        flag = 0;
      }
      else {
        servo.write(SERVO_CLOSE);
        flag = 1;
      }
      blinkCount = 0;
    }
    else {
      // either too late or extra blink → restart sequence
      firstBlinkTime = nowMs;
      blinkCount = 1;
    }
  }

  // 3) Timeout: if we never got the second blink in time, reset
  if (blinkCount == 1 && (nowMs - firstBlinkTime) > DOUBLE_BLINK_MS) {
    blinkCount = 0;
  } 
    Serial.print(sensor_value);
    // Data seprator
    Serial.print(",");
    // EOG envelope
    Serial.println(currentEOGEnvelope);

  }
}

// --- Filter Functions ---
// Band-Stop Butterworth IIR digital filter, generated using filter_gen.py.
// Sampling rate: 512.0 Hz, frequency: [48.0, 52.0] Hz.
// Filter is order 2, implemented as second-order sections (biquads).
// Reference: https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.butter.html
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

// Low-Pass Butterworth IIR digital filter, generated using filter_gen.py.
// Sampling rate: 512.0 Hz, frequency: 45.0 Hz.
// Filter is order 2, implemented as second-order sections (biquads).
// Reference: https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.butter.html
float EOGFilter(float input)
{
  float output = input;
  {
    static float z1, z2; // filter section state
    float x = output - -1.24200128*z1 - 0.45885207*z2;
    output = 0.05421270*x + 0.10842539*z1 + 0.05421270*z2;
    z2 = z1;
    z1 = x;
  }
  return output;
}

// High-Pass Butterworth IIR digital filter, generated using filter_gen.py.
// Sampling rate: 512.0 Hz, frequency: 5.0 Hz.
// Filter is order 2, implemented as second-order sections (biquads).
// Reference: https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.butter.html
float highpass(float input)
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

// ----------------- ENVELOPE FUNCTION -----------------
float EOGEnvelope(float sample) {
  float absSample = fabs(sample);  // Rectify EOG signal

  // Update circular buffer and running sum
  envelopeSum -= envelopeBuffer[envelopeIndex];
  envelopeSum += absSample;
  envelopeBuffer[envelopeIndex] = absSample;
  envelopeIndex = (envelopeIndex + 1) % ENVELOPE_WINDOW_SIZE;

  return envelopeSum / ENVELOPE_WINDOW_SIZE;  // Return moving average
}