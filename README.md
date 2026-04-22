# 📡 ESP32 Wireless Walkie-Talkie (ESP-NOW)

### Real-Time Half-Duplex Audio Communication using ESP32

![ESP32](https://img.shields.io/badge/Platform-ESP32-blue)
![Protocol](https://img.shields.io/badge/Protocol-ESP--NOW-green)
![Framework](https://img.shields.io/badge/Framework-Arduino-teal)
![Status](https://img.shields.io/badge/Status-Working-brightgreen)
![License](https://img.shields.io/badge/License-MIT-yellow)

---
![module image]("module image.jpeg")
## 🚀 Overview

This project implements a **real-time wireless audio communication system** using two ESP32 devices and the **ESP-NOW protocol**.

It functions like a **walkie-talkie**, enabling direct device-to-device voice transmission without:
- ❌ Wi-Fi router  
- ❌ Internet  
- ❌ Bluetooth pairing  

> 🎯 Designed as a **low-latency embedded communication system** with custom buffering, filtering, and packet optimization.

---

## ⚡ Key Features

- 📡 **Peer-to-peer communication** using ESP-NOW  
- 🎙️ **Real-time audio streaming (~300–400 ms latency)**  
- 🔁 **Half-duplex push-to-talk system**  
- 📦 **Optimized packet transmission (95% delivery rate)**  
- 🎛️ **Custom audio processing pipeline**  
- 🧠 **Jitter buffer for smooth playback (no choppy audio)**  
- 🔇 **Noise reduction + AGC + filtering**

---

## System Diagram

![diagram]("diagram.png")

---

## 🔧 Hardware Required

- 2 × ESP32 Development Boards  
- 2 × MAX4466 Microphone Modules  
- 2 × 8Ω Speakers  
- 2 × Push Buttons (GPIO0)  
- Jumper wires + USB power  

---

## ⚙️ Software Stack

- Arduino IDE (ESP32 Core v3.x)  
- ESP-NOW (built-in)  
- WiFi Library  

---

## 📊 Performance

| Metric | Value |
|------|------|
| Latency | ~300–400 ms |
| Packet Delivery | ~95% |
| Sample Rate | 8000 Hz |
| Packet Rate | 80 packets/sec |
| Audio Quality | Telephone-grade |

---

## 🔬 Core Engineering Challenges Solved

### 1. Packet Loss (60% → 95%)
Optimized transmission from **1600 packets/sec → 80 packets/sec**

### 2. Audio Jitter & Underruns
Implemented **600ms jitter buffer + 300ms pre-roll**

### 3. Noise & Feedback
Designed **multi-stage DSP pipeline**:
- High-pass + Low-pass filters  
- Noise gate  
- Automatic Gain Control  

### 4. Mode Switching
Built robust **half-duplex TX/RX switching system**

---

## 🎛️ Audio Processing Pipeline

- DC Offset Removal  
- High-Pass Filter (~300Hz)  
- Two-Stage Low-Pass Filter (~1.4kHz)  
- Noise Gate  
- Automatic Gain Control (AGC)  
- Clipping Protection  

---

## 📦 Packet Design

- 100 samples per packet  
- 80 packets/sec  
- ~17 KB/s data rate  
- CRC-based error detection  

---

## ⚡ Quick Start

```bash
git clone https://github.com/Chaitanya-Virani/ESP32_WALKIE-TALKIE.git

🧠 Key Learnings
Real-time embedded communication design
Wireless protocol optimization (ESP-NOW)
Signal processing for noisy environments
Buffering strategies for jitter handling
Hardware + software co-design
🚀 Future Improvements
Full-duplex communication
Audio compression (ADPCM / µ-law)
Echo cancellation
Multi-device broadcasting
Encryption support
