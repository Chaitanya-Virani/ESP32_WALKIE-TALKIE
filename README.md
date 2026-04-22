ESP-NOW Half-Duplex Audio Communication System
Markdown

# 📡 ESP-NOW Half-Duplex Audio Communication System
### Real-time wireless voice communication between two ESP32 modules over ESP-NOW

![ESP32](https://img.shields.io/badge/Platform-ESP32-blue)
![ESP-NOW](https://img.shields.io/badge/Protocol-ESP--NOW-green)
![Arduino](https://img.shields.io/badge/Framework-Arduino-teal)
![License](https://img.shields.io/badge/License-MIT-yellow)
![Status](https://img.shields.io/badge/Status-Working-brightgreen)

---

## 🎯 What Is This Project?

This project is a **wireless walkie-talkie system** built entirely from scratch using two ESP32 
microcontrollers. It transmits real-time voice audio wirelessly using **ESP-NOW** — a 
lightweight, connectionless protocol by Espressif that works without any router, internet, 
or Wi-Fi infrastructure.

You speak into a microphone on one device. Your voice is captured, processed, packetized, 
sent wirelessly, received, buffered, and played back on the other device's speaker — 
all within **300-400 milliseconds**, in real time.

> **No internet. No router. No Bluetooth pairing. Just two ESP32s talking directly to each other.**

---

## 📸 System Overview
┌─────────────────────────────────────────────────────────────────┐
│ │
│ [YELLOW ESP32] 2.4 GHz Radio [BLACK ESP32] │
│ │
│ Microphone (MAX4466) Speaker (GPIO25) │
│ │ ▲ │
│ ▼ │ │
│ ADC (GPIO34) ──── ESP-NOW Packets ────► Jitter Buffer │
│ │ ◄─── ESP-NOW Packets ──── DAC Playback │
│ Audio Processing │
│ DC + HPF + LPF │
│ Noise Gate + AGC │
│ │ │
│ [Hold GPIO0] = TALK [Release GPIO0] = LISTEN │
│ │
└─────────────────────────────────────────────────────────────────┘

text


---

## 🧩 The Problem I Was Solving

Building real-time audio over ESP-NOW sounds simple — but it is not. 
Here are the actual challenges I faced and solved:

### Challenge 1: Packet Loss at High Rate
When I first tried sending audio at **1600 packets/second** (5 samples per packet), 
the system dropped **55-65% of all packets**. The ESP-NOW queue was saturating.

**Solution:** Restructured to send **100 samples per packet at 80 packets/second**. 
This is within ESP-NOW's comfort zone and gives ~95% delivery rate.

### Challenge 2: Buffer Underruns
The receiver's playback timer was consuming samples faster than they arrived 
due to wireless jitter. This caused **8000+ underruns per second** — 
resulting in choppy, broken audio.

**Solution:** Implemented a **600ms jitter buffer** with a **300ms pre-roll** 
(playback doesn't start until 300ms of audio is buffered). This gives enough 
cushion to absorb any wireless timing variation.

### Challenge 3: High-Pitched Ringing on PTT Press
Every time the push-to-talk button was pressed, a loud high-pitched ring occurred. 
This was caused by:
- Wrong high-pass filter formula (caused resonance)
- DAC click when timer stopped mid-waveform
- Old filter state memory from previous session

**Solution:** Fixed HPF formula, added DAC mute before timer stop, 
full filter state reset, 120ms speaker settling delay, and 200ms soft mute on TX start.

### Challenge 4: Acoustic Feedback (Howling)
The microphone was picking up the speaker output and re-amplifying it, 
creating a feedback loop — the classic "howling" problem.

**Solution:** Multi-stage suppression: Two-stage low-pass filter cuts 
high frequencies where feedback resonates, aggressive noise gate silences 
weak signals (echo is weaker than direct voice), reduced DAC output scale, 
and conservative AGC limits.

### Challenge 5: Unidirectional Communication
Early versions only worked one way — YELLOW to BLACK. BLACK's microphone 
had no effect on YELLOW's speaker.

**Solution:** Single unified firmware with proper peer registration, 
interrupt-safe buffer resets, and clean mode transition logic.

---

## 🔧 Hardware Requirements

| Component | Quantity | Notes |
|-----------|----------|-------|
| ESP32 Development Board | 2 | Any standard ESP32 (WROOM/WROVER) |
| MAX4466 Microphone Module | 2 | Electret mic with amplifier |
| Small Speaker | 2 | 8Ω, 0.5W minimum |
| Push Button | 2 | Momentary normally-open (or use built-in BOOT button) |
| USB Power Supply | 2 | 5V, 1A minimum per board |
| Jumper Wires | — | For connections |
| 100nF Capacitor | 2 | Noise filtering on ADC input (optional but recommended) |

### Wiring Diagram
SENDER / RECEIVER (Same wiring on both devices):

MAX4466 Microphone: Speaker:
┌─────────────┐ ┌─────────────┐
│ VCC ────────┼── 3.3V │ (+) ─────── │── GPIO25 (DAC)
│ GND ────────┼── GND │ (-) ─────── │── GND
│ OUT ────────┼── GPIO34 (ADC) └─────────────┘
└─────────────┘
│
[100nF cap to GND] ← Optional noise filter

Button:
GPIO0 ──────── [Button] ──────── GND
(Has internal pull-up, reads LOW when pressed)

text


> ⚠️ **IMPORTANT:** The MAX4466 has a blue potentiometer on the module.  
> Turn it **fully clockwise** for maximum gain before testing.

---

## 📦 Software Requirements

| Requirement | Version |
|------------|---------|
| Arduino IDE | 2.x recommended |
| ESP32 Board Package | **3.x** (this is critical — API changed from 2.x) |
| WiFi Library | Built-in with ESP32 package |
| ESP-NOW Library | Built-in with ESP32 package |

> ⚠️ **This code is written for ESP32 Arduino Core v3.x.**  
> It will NOT compile on v2.x due to changed timer and callback APIs.

---

## ⚡ Quick Start

### Step 1: Clone the Repository
```bash
git clone https://github.com/yourusername/esp32-espnow-audio.git
cd esp32-espnow-audio
Step 2: Find Your ESP32 MAC Addresses
Upload this to each ESP32 to get its MAC address:

C++

#include <WiFi.h>
void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  Serial.println(WiFi.macAddress());
}
void loop() {}
Step 3: Update MAC Addresses in Code
Open audio_transceiver.ino and update:

C++

// For YELLOW device:
const uint8_t PEER_MAC[] = {0xAC, 0x15, 0x18, 0xD4, 0x5A, 0xCC}; // BLACK's MAC

// For BLACK device:
const uint8_t PEER_MAC[] = {0x4C, 0xC3, 0x82, 0xBE, 0xCB, 0x68}; // YELLOW's MAC
Step 4: Flash Each Device
C++

// Before flashing YELLOW:
#define THIS_IS_YELLOW
//#define THIS_IS_BLACK

// Before flashing BLACK:
//#define THIS_IS_YELLOW
#define THIS_IS_BLACK
Step 5: Test
Power both ESP32s
Open Serial Monitor (115200 baud) on both
Wait for ✓ System ready!
Hold GPIO0 button on YELLOW → speak into microphone
BLACK's speaker should play your voice after ~300ms buffering
🏗️ System Architecture
How It Works — Complete Flow
text

TRANSMIT PATH (when button held):

Human Voice
    │
    ▼
MAX4466 Microphone
    │  (converts sound to electrical signal 0-3.3V)
    ▼
ESP32 ADC @ GPIO34
    │  (samples at 8000 Hz, 12-bit = 0 to 4095)
    ▼
Hardware Timer ISR (fires every 125µs)
    │  (reads ADC, sets flag for main loop)
    ▼
Audio Processing Pipeline (6 stages)
    │  (cleans and normalizes the audio)
    ▼
Packet Buffer (collects 100 samples)
    │  (every 12.5ms = 80 packets/sec)
    ▼
ESP-NOW Send (214 bytes per packet)
    │  (wireless, channel 11, 2.4GHz)
    ▼
~~~~~~~~~~~~~~~~ AIR ~~~~~~~~~~~~~~~~
    │
    ▼
ESP-NOW Receive Callback
    │  (validated by CRC checksum)
    ▼
Jitter Buffer (4800 samples = 600ms)
    │  (absorbs wireless timing variation)
    ▼
Hardware Timer ISR (fires every 125µs)
    │  (reads one sample per tick)
    ▼
ESP32 DAC @ GPIO25
    │  (converts digital sample to 0-3.3V)
    ▼
Speaker
    │  (converts voltage to sound)
    ▼
Human Ear
🎛️ Audio Processing Pipeline
Every raw microphone sample passes through 6 processing stages
before transmission. This is what transforms a noisy ADC reading
into intelligible speech.

Stage 1: DC Offset Removal
text

Problem:  MAX4466 outputs audio centered around 1.65V (= ADC 2048)
          This constant bias wastes half your dynamic range

Before:   Signal rides at 2048 ──────────╮╰──────────
After:    Signal centered at 0   ────╮╰──────╮╰────

Formula:  y[n] = x[n] - x[n-1] + 0.995 × y[n-1]
Effect:   Removes DC component, preserves all audio frequencies
Stage 2: High-Pass Filter (~300 Hz cutoff)
text

Problem:  Low frequencies carry rumble, vibration, hum
          Table tapping, power hum (50/60Hz), breath noise

Removes:  Everything below 300 Hz
Keeps:    Voice (300Hz - 3.4kHz) and above

Formula:  y[n] = α × (y[n-1] + x[n] - x[n-1])    α = 0.85
Note:     This is the CORRECT 1st-order HPF formula.
          Wrong formula causes resonance and ringing!
Stage 3: Two-Stage Low-Pass Filter (~1.4 kHz cutoff)
text

Problem:  High frequencies carry hiss, electronic noise,
          and most importantly — acoustic feedback frequencies

Removes:  Everything above 1.4 kHz
Keeps:    Core voice frequencies (300Hz - 1.4kHz)
          This is intentionally narrow — like a telephone

Why two stages?
  Single stage = -6 dB/octave rolloff (gentle)
  Two stages   = -12 dB/octave rolloff (steep)
  Steeper = better noise/feedback rejection

Formula:  lp1 += α × (input - lp1)    ← First pass
          lp2 += α × (lp1 - lp2)      ← Second pass
          α = 0.45
Stage 4: Noise Gate
text

Problem:  Microphone always picks up background noise.
          During silence, it also picks up speaker output
          which can start a feedback loop.

How it works:
  ├─ If signal level > threshold (450) → OPEN gate → pass audio
  ├─ If signal level < threshold → hold for 100ms then CLOSE gate
  └─ When closed → output is ZERO (silence)

Smooth transitions (GATE_SPEED = 0.001) prevent click artifacts
when gate opens or closes.

Effect:   Dead silence between words → no feedback can build
          Strong voice → passes through cleanly
Stage 5: Automatic Gain Control (AGC)
text

Problem:  People speak at different volumes.
          Quiet speaker sounds too quiet.
          Loud speaker causes distortion.

How it works:
  ├─ Measures signal envelope (running average of absolute value)
  ├─ Fast attack (0.003) — quickly responds to loud sounds
  ├─ Slow decay (0.00005) — slowly releases on quiet periods
  └─ Calculates gain = Target (500) / Measured Level

  Loud voice  → gain < 1 → reduce volume
  Quiet voice → gain > 1 → boost volume
  Max gain = 1.5× (prevents feedback amplification)
Stage 6: Hard Clipping
text

Problem:  Combined gain from all stages might push signal
          beyond the valid int16 range (-2048 to +2047)

Solution: Simple boundary enforcement
  if (sample >  2047) sample =  2047
  if (sample < -2047) sample = -2047

Prevents: Integer overflow which would cause extreme distortion
📡 ESP-NOW Protocol
Why ESP-NOW?
Feature	ESP-NOW	Standard WiFi	Bluetooth
Needs Router	❌ No	✅ Yes	❌ No
Pairing Required	❌ No	✅ Yes	✅ Yes
Latency	~2ms	~50-200ms	~20-150ms
Range	~200m	~50m indoor	~10m
Max Packet	250 bytes	Unlimited	Limited
Power Usage	Very Low	High	Medium
Packet Structure (214 bytes)
text

┌──────────┬──────────┬───────────┬───────┬──────────────┬─────┐
│timestamp │   seq    │sampleCount│ flags │   samples    │ crc │
│ 4 bytes  │ 2 bytes  │  1 byte   │1 byte │  200 bytes   │  2B │
│          │          │           │       │ (100×int16)  │     │
└──────────┴──────────┴───────────┴───────┴──────────────┴─────┘

timestamp:   Sender's sample counter — helps receiver detect timing
seq:         Packet number — receiver detects lost packets by gaps
sampleCount: Always 100 — how many audio samples in this packet
flags:       Reserved for future use
samples:     100 × 16-bit signed audio samples = 200 bytes of audio
crc:         CRC-16 checksum of all above fields — detects corruption
Packet Rate Math
text

Sample Rate:       8000 samples/second
Samples per Packet: 100 samples
Packet Rate:       8000 ÷ 100 = 80 packets/second
Packet Interval:   1000ms ÷ 80 = 12.5ms between packets
Data Rate:         214 bytes × 80 = 17,120 bytes/second
WiFi Capacity:     ~1,000,000 bytes/second
We use:            ~1.7% of available WiFi bandwidth
⏱️ Jitter Buffer — The Key to Smooth Audio
This is the most important concept in the receiver.

The Problem Without a Buffer
text

Network sends packets every ~12.5ms (sometimes 8ms, sometimes 20ms)
DAC needs a sample every exactly 125µs — no exceptions

Without buffer:          With 300ms buffer:
Packet late → silence    Packet late → buffer covers it
Packet early → rush      Packet early → absorbed into buffer
Result: choppy audio     Result: smooth continuous audio
Buffer States
text

State 1: BUFFERING (startup)
├─ Just started or just switched to RX mode
├─ Incoming packets fill the buffer
├─ DAC outputs silence (128 = midpoint = no sound)
└─ When buffer reaches 2400 samples (300ms) → switch to PLAYING

State 2: PLAYING (normal operation)
├─ DAC timer fires every 125µs
├─ Reads one sample from buffer
├─ Applies DAC_SCALE (0.65) to control volume
└─ Outputs voltage on GPIO25

Underrun (buffer empty while playing):
├─ Output silence for that sample (128)
├─ Count the underrun
└─ Continue — buffer will refill
Buffer Size Choice
text

JITTER_BUF_SIZE = 4800 samples = 600ms at 8kHz
BUFFER_TARGET   = 2400 samples = 300ms pre-roll

300ms pre-roll means:
├─ System can absorb up to 300ms of WiFi jitter
├─ A packet 300ms late will not cause an underrun
├─ Trade-off: 300ms added latency at start
└─ Total latency: ~300-400ms (acceptable for walkie-talkie)
🔁 Half-Duplex Mode Switching
Push-To-Talk (PTT) Operation
text

Button RELEASED → RECEIVER MODE (default)
Button HELD     → TRANSMITTER MODE

Both devices boot into RECEIVER MODE.
Press button to talk, release to listen.
Only one device transmits at a time.
What Happens When You Press PTT
text

1.  [0ms]   GPIO0 reads LOW (button pressed)
2.  [50ms]  Debounce confirms stable LOW → trigger TX mode
3.  [50ms]  DAC immediately set to 128 (silence)
4.  [60ms]  Playback timer stopped
5.  [180ms] 120ms settling delay (speaker cone stops vibrating)
6.  [180ms] ADC configured for microphone input
7.  [180ms] All audio filter states reset to zero
8.  [180ms] Transmit buffer cleared
9.  [180ms] ADC pipeline drained (32 dummy reads)
10. [180ms] txMuteCounter = 1600 (200ms of forced silence)
11. [180ms] Sampling timer started (8000 Hz ADC reads begin)
12. [380ms] Soft mute expires → real audio starts transmitting
What Happens When You Release PTT
text

1.  [0ms]   GPIO0 reads HIGH (button released)
2.  [50ms]  Debounce confirms stable HIGH → trigger RX mode
3.  [50ms]  Sampling timer stopped (ADC reads stop)
4.  [50ms]  Audio processor state reset
5.  [50ms]  Jitter buffer completely reset (interrupt-safe)
6.  [50ms]  DAC set to 128 (silence)
7.  [150ms] 100ms settling delay
8.  [150ms] Playback timer started
9.  [150ms] Buffer enters BUFFERING state
10. [450ms] Buffer fills with 300ms of audio
11. [450ms] Playback begins automatically
Button Debounce
text

Mechanical buttons bounce — they rapidly connect/disconnect 
for 5-20ms before settling. Without debounce:

Raw signal: ─╮╭╮╭╮╭─────────────── (false triggers!)
Debounced:  ─╯            ╰─────── (clean single trigger)

Implementation: State must be stable for 50ms before registering.
Uses non-blocking millis() comparison — never uses delay() in handleButton().
📊 Performance Metrics
Achieved in Testing
Metric	Value	Notes
Packet Delivery Rate	~95%	At 80 packets/sec, indoor, 3m range
CRC Error Rate	0%	All corrupted packets rejected
Buffer Underruns	~0/sec	With 300ms pre-roll buffer
End-to-End Latency	~350ms	Acceptable for PTT communication
Audio Quality	Intelligible voice	Telephone-quality (300-1400Hz)
Range (tested)	3-5 meters	Limited by test environment
WiFi Channel	11	Less congested than channels 1 and 6
Serial Monitor Output
text

Transmitter (YELLOW, talking):
[YELLOW][TX] Pkts:  480 ACK:  456(95%) AGC:1.20 Gate:0.98

Receiver (BLACK, listening):
[BLACK][RX] Pkts:  456 Lost:  24(5%) Buf:2400/4800 Undrn:0 (PLAYING)
🛠️ Configuration Reference
All tunable parameters are at the top of the file:

C++

// ═══ SYSTEM ═══
const uint8_t  ESPNOW_CHANNEL     = 11;      // WiFi channel (1, 6, or 11)
const uint32_t SAMPLE_RATE        = 8000;    // Audio sample rate (Hz)
const uint16_t SAMPLES_PER_PACKET = 100;     // Samples per ESP-NOW packet
const uint32_t JITTER_BUF_SIZE    = 4800;    // Buffer size (samples)
const uint32_t BUFFER_TARGET      = 2400;    // Pre-roll before playback

// ═══ AUDIO PROCESSING ═══
const int   TX_MUTE_SAMPLES   = 1600;    // Silence after PTT press (samples)
const float GATE_THRESHOLD    = 450.0f;  // Noise gate sensitivity
const int   GATE_HOLD_SAMPLES = 800;     // Gate hold time (samples)
const float GATE_SPEED        = 0.001f;  // Gate open/close speed
const float AGC_MAX_GAIN      = 1.5f;    // Maximum amplification
const float AGC_TARGET        = 500.0f;  // AGC target level
const float HPF_ALPHA         = 0.85f;   // High-pass filter coefficient
const float LPF_ALPHA         = 0.45f;   // Low-pass filter coefficient
const float DAC_SCALE         = 0.65f;   // Speaker volume (0.0-1.0)
Tuning Guide
Problem	Parameter	Direction
Ringing on PTT press	TX_MUTE_SAMPLES	↑ Increase
Background noise audible	GATE_THRESHOLD	↑ Increase
Voice cuts off between words	GATE_HOLD_SAMPLES	↑ Increase
Voice too quiet	DAC_SCALE + AGC_TARGET	↑ Increase
Acoustic feedback/howling	DAC_SCALE	↓ Decrease
Audio too muffled	LPF_ALPHA	↑ Increase
Too much high-freq noise	LPF_ALPHA	↓ Decrease
Choppy audio (underruns)	BUFFER_TARGET	↑ Increase
Too much startup delay	BUFFER_TARGET	↓ Decrease
🔍 Troubleshooting
No sound at all
text

☐ Check GPIO25 is connected to speaker positive
☐ Check GND is connected to speaker negative
☐ Verify Serial shows [PLAYING] not [BUFFERING]
☐ Check PEER_MAC addresses are correct and swapped between devices
☐ Verify both devices on same ESPNOW_CHANNEL
Only noise, no voice
text

☐ MAX4466 potentiometer — turn fully CLOCKWISE (maximum gain)
☐ Check microphone connected to GPIO34, not GPIO25
☐ Upload ADC diagnostic code — check Range > 500 when speaking
☐ Verify MAX4466 VCC = 3.3V (not 5V)
Broken/choppy audio
text

☐ Check Serial for high Underrun count
☐ Increase BUFFER_TARGET (try 3200)
☐ Move devices closer together
☐ Change ESPNOW_CHANNEL (try 1 or 6)
☐ Use USB power bank instead of PC USB port
High-pitched ringing on PTT
text

☐ Increase TX_MUTE_SAMPLES (try 2400 or 3200)
☐ Separate microphone and speaker physically
☐ Decrease DAC_SCALE (reduce speaker volume)
☐ Increase settling delay in enterTransmitter()
One-way communication only
text

☐ Verify THIS_IS_YELLOW / THIS_IS_BLACK defines are set correctly
☐ Verify PEER_MAC is the OTHER device's MAC (not your own)
☐ Both devices must be flashed with same code version
📁 Project Structure
text

esp32-espnow-audio/
│
├── audio_transceiver.ino     ← Main unified firmware (flash to both devices)
│
├── diagnostics/
│   ├── adc_diagnostic.ino    ← Check microphone signal level
│   ├── link_test_sender.ino  ← Test ESP-NOW link quality
│   └── link_test_receiver.ino
│
├── docs/
│   ├── filtering_explained.md    ← Detailed filter theory
│   ├── jitter_buffer.md          ← Buffer design explanation
│   └── espnow_protocol.md        ← ESP-NOW internals
│
└── README.md                 ← This file
🧠 Key Learnings & Design Decisions
Why 8000 Hz sample rate?
Telephone voice quality uses 8kHz. Human speech has most energy
between 300Hz and 3.4kHz. 8kHz satisfies the Nyquist theorem
(must sample at 2× the highest frequency) for voice content.
Higher rates would waste bandwidth without improving voice clarity.

Why 100 samples per packet?
This is the sweet spot between:

Too few (5 samples): 1600 packets/sec → ESP-NOW queue saturates → 55% loss
Too many (800 samples): 10 packets/sec → 100ms gaps per lost packet
100 samples: 80 packets/sec → sustainable rate, 12.5ms gap per loss
Why channel 11?
In most environments, channels 1, 6, and 11 are the standard non-overlapping
channels in 2.4GHz WiFi. Channel 11 is often least congested.
All nearby routers and devices sharing the channel create interference —
using a less congested channel reduces packet loss significantly.

Why disable WiFi power saving?
ESP32's WiFi power saving mode adds up to 100ms of latency per packet
by putting the radio to sleep between transmissions. For audio streaming
this is catastrophic — it causes buffer underruns and choppy audio.
WIFI_PS_NONE keeps the radio always active.

Why a 300ms pre-roll buffer?
Wireless networks have jitter — packets don't arrive at perfectly
regular intervals. A packet might arrive 5ms early or 50ms late.
By pre-buffering 300ms of audio, the system can absorb up to 300ms
of jitter without the playback timer ever running out of samples.
This is the same technique used in VoIP and streaming services.

Why single firmware for both devices?
Maintaining two separate firmware files means every bug fix must be
applied twice and kept in sync. A single firmware with a compile-time
#define switch is cleaner, less error-prone, and easier to update.

🔬 Technical Specifications
Parameter	Value
Microcontroller	ESP32 (Xtensa LX6 dual-core, 240MHz)
Protocol	ESP-NOW (IEEE 802.11 based)
WiFi Channel	11 (2.462 GHz)
Audio Sample Rate	8000 Hz
Audio Bit Depth	12-bit ADC → 16-bit processing → 8-bit DAC
Samples per Packet	100
Packet Rate	80 packets/second
Packet Size	214 bytes (200 audio + 14 header/CRC)
Audio Bandwidth	300 Hz – 1400 Hz
Jitter Buffer	4800 samples (600ms)
Pre-roll	2400 samples (300ms)
End-to-End Latency	~300-400ms
PTT Soft Mute	200ms
Speaker Settling	120ms
Error Detection	CRC-16 CCITT
TX Power	21 dBm (maximum)
🤝 Contributing
Contributions are welcome! Areas for improvement:

Echo cancellation — Digital AEC algorithm to eliminate feedback mathematically
Adaptive jitter buffer — Dynamically resize based on network conditions
Compression — Implement µ-law or ADPCM to reduce packet size
Full duplex — Simultaneous TX and RX (requires echo cancellation)
Multi-device — Broadcast to multiple receivers simultaneously
Encryption — Enable ESP-NOW encryption for secure communication
Battery optimization — Smart power management for portable use
📜 License
MIT License — free to use, modify, and distribute.
If you use this in a project, a mention or star ⭐ is appreciated!

👤 Author
Built from scratch with extensive debugging, iterative testing,
and real-world optimization over multiple development sessions.

Every challenge in this project — from the 55% packet loss crisis,
to the high-pitched ringing on PTT, to the acoustic feedback howling —
was encountered, diagnosed from serial monitor data,
and solved through systematic engineering.

🌟 Acknowledgments
Espressif Systems for the ESP-NOW protocol documentation
Arduino ESP32 Community for board package support
Every serial monitor output that revealed what was actually going wrong