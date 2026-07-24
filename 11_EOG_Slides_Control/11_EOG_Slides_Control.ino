// This code is designed to work specifically on:
// 1. Arduino UNO R4 Minima
// 2. Arduino UNO R4 WiFi

// Slide Control for Presentations using EOG (Eye Movement) Detection
// Double Blink = Next Slide (Right Arrow)
// Triple Blink = Previous Slide (Left Arrow)
// Adapted from R4-EXG.ino with HID keyboard functionality

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

// Copyright (c) 2025 Krishnanshu Mittal - krishnanshu@upsidedownlabs.tech
// Copyright (c) 2025 Aman Maheshwari - aman@upsidedownlabs.tech 
// Copyright (c) 2025 Deepak Khatri - deepak@upsidedownlabs.tech
// Copyright (c) 2025 Upside Down Labs - contact@upsidedownlabs.tech

// At Upside Down Labs, we create open-source DIY neuroscience hardware and software.
// Our mission is to make neuroscience affordable and accessible for everyone.
// By supporting us with your purchase, you help spread innovation and open science.
// Thank you for being part of this journey with us!

#include <Arduino.h>
#include <Keyboard.h>  // HID keyboard library for Arduino R4
#include <math.h>

// #define DEBUG  // Uncomment this line to enable debugging

// ----------------- USER CONFIGURATION -----------------
#define SAMPLE_RATE       512          // samples per second
#define BAUD_RATE         115200
#define INPUT_PIN         A0
#define LED_PIN           LED_BUILTIN

// Envelope Configuration for EOG detection
#define ENVELOPE_WINDOW_MS 100  // Smoothing window in milliseconds
#define ENVELOPE_WINDOW_SIZE ((ENVELOPE_WINDOW_MS * SAMPLE_RATE) / 1000)

// Blink Detection Thresholds - adjust these based on your setup
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
const unsigned long DOUBLE_BLINK_MS     = 600;   // max time between the two blinks
const unsigned long TRIPLE_BLINK_MS     = 600;   // max time between second and third blink
unsigned long lastBlinkTime     = 0;             // time of most recent blink
unsigned long firstBlinkTime    = 0;             // time of the first blink in a pair
unsigned long secondBlinkTime   = 0;
int         blinkCount         = 0;             // how many valid blinks so far (0–3)
float currentEOGEnvelope = 0;

// HID Command Cooldown to prevent rapid-fire commands
const unsigned long HID_COOLDOWN_MS = 1000;  // 1 second between HID commands
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

// High-Pass Butterworth IIR digital filter, generated using filter_gen.py.
// Sampling rate: 512.0 Hz, frequency: 5.0 Hz.
// Filter is order 2, implemented as second-order sections (biquads).
// Reference: https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.butter.html
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

// HID Keyboard Functions
void sendNextSlide() {
  unsigned long nowMs = millis();
  if ((nowMs - lastHIDCommandTime) >= HID_COOLDOWN_MS) {
    Keyboard.press(KEY_RIGHT_ARROW);
    delay(50);  // Hold key for 50ms
    Keyboard.release(KEY_RIGHT_ARROW);
    lastHIDCommandTime = nowMs;
    
    Serial.println("HID: Next Slide (Right Arrow)");
    
    // LED feedback - quick double flash
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
  }
}

void sendPreviousSlide() {
  unsigned long nowMs = millis();
  if ((nowMs - lastHIDCommandTime) >= HID_COOLDOWN_MS) {
    Keyboard.press(KEY_LEFT_ARROW);
    delay(50);  // Hold key for 50ms
    Keyboard.release(KEY_LEFT_ARROW);
    lastHIDCommandTime = nowMs;
    
    Serial.println("HID: Previous Slide (Left Arrow)");
    
    // LED feedback - quick triple flash
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
  }
}

void setup() {
  Serial.begin(BAUD_RATE);
  delay(100);
  
  pinMode(INPUT_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  
  // Initialize HID Keyboard
  Keyboard.begin();
  
  // LED startup sequence
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);
  delay(300);
  digitalWrite(LED_PIN, HIGH);
  delay(300);
  digitalWrite(LED_PIN, LOW);
  
  lastSegmentTimeMs = millis();  // Initialize the segment timer
  
  Serial.println("Arduino R4 Slide Controller Ready!");
  Serial.println("Double Blink = Next Slide");
  Serial.println("Triple Blink = Previous Slide");
  Serial.println("Starting EOG monitoring at 512 Hz...");
}

void loop() {
    static unsigned long lastMicros = 0;
    static long timer = 0;
    
    digitalWrite(LED_PIN, LOW);  // Default LED state
    
    // Timing-based sampling for 512 Hz
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
    }
    
    // Process all available samples from circular buffer
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
    
    // Get current time for blink detection logic
    unsigned long nowMs = millis();
    
    // ===== SEGMENT STATISTICS PROCESSING =====
    if ((nowMs - lastSegmentTimeMs) >= (1000UL * SEGMENT_SEC)) {
        if(segmentIndex > 0) {
            // Compute min/max/avg for the completed segment
            eogMin = eogBuffer[0]; 
            eogMax = eogBuffer[0];  
            float eogSum = 0;
            
            for (uint16_t i = 0; i < segmentIndex; i++) {
                float eogVal = eogBuffer[i];
                
                // EOG statistics
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
    
    // ===== BLINK DETECTION AND HID CONTROL =====
    if (currentEOGEnvelope > BlinkLowerThreshold && 
        currentEOGEnvelope < BlinkUpperThreshold && 
        (nowMs - lastBlinkTime) >= BLINK_DEBOUNCE_MS) {
        
        lastBlinkTime = nowMs;
        
        if (blinkCount == 0) {
            // First blink of sequence
            firstBlinkTime = nowMs;
            blinkCount = 1;
        }
        else if (blinkCount == 1 && (nowMs - firstBlinkTime) <= DOUBLE_BLINK_MS) {
            // Second blink within time window
            secondBlinkTime = nowMs;
            blinkCount = 2;
        }
        else if (blinkCount == 2 && (nowMs - secondBlinkTime) <= TRIPLE_BLINK_MS) {
            // Triple blink detected - Previous Slide
            Serial.println("Triple blink detected!");
            sendPreviousSlide();
            blinkCount = 0;
        }
        else {
            // Either too late or extra blink - restart sequence
            firstBlinkTime = nowMs;
            blinkCount = 1;
        }
    }
    
    // ===== BLINK TIMEOUT HANDLING =====
    // If we had 2 blinks but no third arrived in time, treat as double blink
    if (blinkCount == 2 && (nowMs - secondBlinkTime) > TRIPLE_BLINK_MS) {
        Serial.println("Double blink detected!");
        sendNextSlide();
        blinkCount = 0;
    }
    
    // If we never got the second blink in time, reset
    if (blinkCount == 1 && (nowMs - firstBlinkTime) > DOUBLE_BLINK_MS) {
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
            Serial.println("EOG: "); Serial.print(currentEOGEnvelope);
        }
        lastDebugPrint = nowMs;
    }
    #endif
}