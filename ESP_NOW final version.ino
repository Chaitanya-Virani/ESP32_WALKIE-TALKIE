/*
  ESP-NOW BIDIRECTIONAL AUDIO - HALF DUPLEX
  ESP32 Board v3.x - FULLY UPDATED WITH ALL FIXES
  
  YELLOW: 4C:C3:82:BE:CB:68
  BLACK:  AC:15:18:D4:5A:CC
  
  GPIO0  = BOOT button (hold = TX, release = RX)
  GPIO34 = ADC (microphone input MAX4466)
  GPIO25 = DAC (speaker output)
  
  FIXES APPLIED:
  - Correct HPF formula (no more ringing)
  - Soft mute on PTT press (100ms silence)
  - DAC muted before mode switch (no click)
  - Full audio processor reset on TX entry
  - ADC pipeline drain on TX entry
  - Longer DAC settle time on RX entry
  - Interrupt-safe jitter buffer reset
*/

// ============================================================
// SET THIS BEFORE FLASHING - COMMENT/UNCOMMENT ONE LINE ONLY
// ============================================================
#define THIS_IS_YELLOW
// #define THIS_IS_BLACK

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <driver/adc.h>
#include <driver/dac.h>

// ============================================================
// DEVICE IDENTITY
// ============================================================
#ifdef THIS_IS_YELLOW
  const uint8_t PEER_MAC[] = {0xAC, 0x15, 0x18, 0xD4, 0x5A, 0xCC};
  const char*   MY_NAME    = "YELLOW";
#else
  const uint8_t PEER_MAC[] = {0x68, 0xFE, 0x71, 0x0C, 0x10, 0x98};
  const char*   MY_NAME    = "BLACK";
#endif

// ============================================================
// CONFIGURATION
// ============================================================
const uint8_t  ESPNOW_CHANNEL     = 11;
const uint32_t SAMPLE_RATE        = 8000;
const uint16_t SAMPLES_PER_PACKET = 100;
const uint32_t JITTER_BUF_SIZE    = 4000;   // 500ms buffer
const uint32_t BUFFER_TARGET      = 1600;   // 200ms before playback
const uint8_t  BUTTON_PIN         = 0;      // GPIO0 BOOT button
const uint32_t DEBOUNCE_MS        = 50;     // Button debounce
const int      TX_MUTE_SAMPLES    = 0;    // 100ms soft mute on PTT

// ============================================================
// PACKET STRUCTURE (must match on both devices)
// ============================================================
struct __attribute__((packed)) AudioPacket {
  uint32_t timestamp;
  uint16_t seq;
  uint8_t  sampleCount;
  uint8_t  flags;
  int16_t  samples[SAMPLES_PER_PACKET];
  uint16_t crc;
};

// ============================================================
// SYSTEM MODE
// ============================================================
enum class Mode { RECEIVER, TRANSMITTER };
volatile Mode currentMode = Mode::RECEIVER;

// ============================================================
// AUDIO PROCESSOR (TX side)
// ============================================================
struct AudioProcessor {
  // DC removal state
  float dcX     = 0.0f;
  float dcY     = 0.0f;

  // High-pass filter state
  float hpY     = 0.0f;   // previous output
  float hpX     = 0.0f;   // previous input

  // AGC state
  float agcGain = 1.0f;
  float agcEnv  = 0.0f;

  void reset() {
    dcX = dcY = 0.0f;
    hpY = hpX = 0.0f;
    agcGain = 1.0f;
    agcEnv  = 0.0f;
  }
};

// ============================================================
// JITTER BUFFER (RX side)
// ============================================================
struct JitterBuffer {
  int16_t           data[JITTER_BUF_SIZE];
  volatile uint32_t writePos  = 0;
  volatile uint32_t readPos   = 0;
  volatile uint32_t level     = 0;
  volatile bool     buffering = true;
  uint32_t          dropped   = 0;

  void reset() {
    portDISABLE_INTERRUPTS();
    writePos  = 0;
    readPos   = 0;
    level     = 0;
    buffering = true;
    dropped   = 0;
    portENABLE_INTERRUPTS();
  }

  uint32_t available() {
    return (writePos - readPos + JITTER_BUF_SIZE) % JITTER_BUF_SIZE;
  }

  bool canWrite(uint32_t len) {
    return (available() + len) < (JITTER_BUF_SIZE - 100);
  }

  void write(const int16_t* src, int len) {
    for (int i = 0; i < len; i++) {
      data[writePos] = src[i];
      writePos = (writePos + 1) % JITTER_BUF_SIZE;
    }
    level = available();
  }

  int16_t read() {
    if (readPos == writePos) return 0;  // Underrun
    int16_t s = data[readPos];
    readPos = (readPos + 1) % JITTER_BUF_SIZE;
    level = available();
    return s;
  }
};

// ============================================================
// GLOBALS
// ============================================================
AudioProcessor audio;
JitterBuffer   jbuf;

// TX state
int16_t  txBuf[SAMPLES_PER_PACKET];
int      txBufIdx     = 0;
uint16_t txSeq        = 0;
uint32_t txSampleCnt  = 0;
int      txMuteCounter = 1600;   // Soft mute countdown

// Timers
hw_timer_t* samplerTimer  = nullptr;
hw_timer_t* playbackTimer = nullptr;

// ADC ISR flags
volatile bool adcReady = false;
volatile int  adcRaw   = 0;

// Statistics
volatile uint32_t txCount   = 0;
volatile uint32_t ackCount  = 0;
volatile uint32_t rxCount   = 0;
volatile uint32_t crcErrors = 0;
volatile uint32_t underruns = 0;
unsigned long     lastStats = 0;

// ============================================================
// CRC-16
// ============================================================
uint16_t calcCRC(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
    }
  }
  return crc;
}

// ============================================================
// AUDIO PROCESSING (TX pipeline) - FIXED HPF FORMULA
// ============================================================
int16_t processAudio(int raw) {
  // Convert ADC (0-4095) to signed (-2048 to +2047)
  float s = (float)raw - 2048.0f;

  // ===== STAGE 1: DC Removal =====
  // y[n] = x[n] - x[n-1] + alpha * y[n-1]
  float dc = s - audio.dcX + 0.995f * audio.dcY;
  audio.dcX = s;
  audio.dcY = dc;
  s = dc;

  // ===== STAGE 2: High-Pass Filter (FIXED FORMULA) =====
  // Correct 1st order HPF: y[n] = alpha * (y[n-1] + x[n] - x[n-1])
  // Previous formula was wrong and caused ringing/resonance
  float hpOut  = 0.85f * (audio.hpY + s - audio.hpX);
  audio.hpX    = s;        // Store current input as previous
  audio.hpY    = hpOut;    // Store current output as previous
  s = hpOut;

  // ===== STAGE 3: AGC (slow, stable) =====
  float absS = (s < 0) ? -s : s;

  // Fast attack, slow decay envelope follower
  if (absS > audio.agcEnv) {
    audio.agcEnv += 0.003f   * (absS - audio.agcEnv);
  } else {
    audio.agcEnv += 0.00005f * (absS - audio.agcEnv);
  }

  // Calculate gain only when signal is meaningful
  if (audio.agcEnv > 30.0f) {
    audio.agcGain = 600.0f / audio.agcEnv;
    // Clamp gain to safe range
    if (audio.agcGain > 1.8f) audio.agcGain = 1.8f;
    if (audio.agcGain < 0.9f) audio.agcGain = 0.9f;
  }

  // Apply fixed gain + AGC
  s *= 1.5f * audio.agcGain;

  // ===== STAGE 4: Hard Clip =====
  if (s >  2047.0f) s =  2047.0f;
  if (s < -2047.0f) s = -2047.0f;

  return (int16_t)s;
}

// ============================================================
// TIMER ISRs
// ============================================================

// ADC sampling at 8000 Hz (TX mode)
void IRAM_ATTR onSamplerTick() {
  adcRaw   = adc1_get_raw(ADC1_CHANNEL_6);
  adcReady = true;
}

// DAC playback at 8000 Hz (RX mode)
void IRAM_ATTR onPlaybackTick() {
  // Phase 1: Buffering - wait for enough data
  if (jbuf.buffering) {
    dac_output_voltage(DAC_CHANNEL_1, 128);  // Output silence
    if (jbuf.level >= BUFFER_TARGET) {
      jbuf.buffering = false;  // Enough data - start playing
    }
    return;
  }

  // Phase 2: Playing
  int16_t s = jbuf.read();

  // Track underruns
  if (jbuf.level == 0 && s == 0) underruns++;

  // Convert int16 (-2048 to +2047) → DAC (0 to 255)
  int v = ((int)s + 2048) >> 4;
  if (v < 0)   v = 0;
  if (v > 255) v = 255;

  dac_output_voltage(DAC_CHANNEL_1, (uint8_t)v);
}

// ============================================================
// ESP-NOW CALLBACKS
// ============================================================
void onSent(const wifi_tx_info_t* info, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) ackCount++;
}

void onReceive(const esp_now_recv_info* info,
               const uint8_t* data, int len) {
  // Only buffer audio when in receiver mode
  if (currentMode != Mode::RECEIVER) return;

  // Validate size
  if (len != (int)sizeof(AudioPacket)) return;

  AudioPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));

  // Validate CRC
  uint16_t expectedCRC = calcCRC(
    (uint8_t*)&pkt, sizeof(pkt) - sizeof(pkt.crc));
  if (expectedCRC != pkt.crc) {
    crcErrors++;
    return;
  }

  // Drop if buffer full
  if (!jbuf.canWrite(pkt.sampleCount)) {
    jbuf.dropped++;
    return;
  }

  // Write to jitter buffer
  jbuf.write(pkt.samples, pkt.sampleCount);
  rxCount++;
}

// ============================================================
// TIMER MANAGEMENT
// ============================================================
void startSampler() {
  if (samplerTimer != nullptr) return;
  samplerTimer = timerBegin(SAMPLE_RATE);
  timerAttachInterrupt(samplerTimer, &onSamplerTick);
  timerAlarm(samplerTimer, 1, true, 0);
}

void stopSampler() {
  if (samplerTimer == nullptr) return;
  timerEnd(samplerTimer);
  samplerTimer = nullptr;
  adcReady     = false;
  adcRaw       = 0;
}

void startPlayback() {
  if (playbackTimer != nullptr) return;
  dac_output_enable(DAC_CHANNEL_1);
  dac_output_voltage(DAC_CHANNEL_1, 128);
  playbackTimer = timerBegin(SAMPLE_RATE);
  timerAttachInterrupt(playbackTimer, &onPlaybackTick);
  timerAlarm(playbackTimer, 1, true, 0);
}

void stopPlayback() {
  if (playbackTimer == nullptr) return;
  timerEnd(playbackTimer);
  playbackTimer = nullptr;
  dac_output_voltage(DAC_CHANNEL_1, 128);  // Silence
}

// ============================================================
// MODE: ENTER RECEIVER
// ============================================================
void enterReceiver() {
  if (currentMode == Mode::RECEIVER) return;

  Serial.printf("\n[%s] >>> ENTERING RECEIVER MODE <<<\n", MY_NAME);

  // 1. Stop ADC sampling first
  stopSampler();

  // 2. Reset audio processor (clean state for next TX session)
  audio.reset();

  // 3. Reset TX state
  memset(txBuf, 0, sizeof(txBuf));
  txBufIdx     = 0;
  txSampleCnt  = 0;
  txMuteCounter = 0;

  // 4. Reset jitter buffer (clears old audio)
  jbuf.reset();
  rxCount   = 0;
  underruns = 0;
  crcErrors = 0;

  // 5. Mute DAC and wait for it to settle (prevents pop)
  dac_output_voltage(DAC_CHANNEL_1, 128);
  delay(80);

  // 6. Start DAC playback timer
  startPlayback();

  // 7. Set mode last
  currentMode = Mode::RECEIVER;

  Serial.printf("[%s] RX MODE - Hold GPIO0 to talk\n\n", MY_NAME);
}

// ============================================================
// MODE: ENTER TRANSMITTER
// ============================================================
void enterTransmitter() {
  if (currentMode == Mode::TRANSMITTER) return;

  Serial.printf("\n[%s] >>> ENTERING TRANSMITTER MODE <<<\n", MY_NAME);

  // 1. Mute DAC FIRST before stopping timer
  //    This is critical - prevents the click that causes ringing
  dac_output_voltage(DAC_CHANNEL_1, 128);
  delay(10);

  // 2. Stop DAC playback
  stopPlayback();

  // 3. Longer settling delay (lets speaker/amp fully settle)
  delay(50);

  // 4. Configure ADC
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);

  // 5. CRITICAL: Full audio processor reset
  //    Old filter memory causes ringing when TX starts
  audio.reset();

  // 6. Reset TX buffers (prevents garbage in first packet)
  memset(txBuf, 0, sizeof(txBuf));
  txBufIdx    = 0;
  txSampleCnt = 0;
  txCount     = 0;
  ackCount    = 0;

  // 7. Drain ADC pipeline (first few reads are unreliable)
  for (int i = 0; i < 16; i++) {
    adc1_get_raw(ADC1_CHANNEL_6);
    delayMicroseconds(125);  // One sample period at 8kHz
  }
  adcReady = false;
  adcRaw   = 0;

  // 8. Set soft mute: suppress first 100ms after PTT press
  //    Hides any remaining switching transient
  txMuteCounter = TX_MUTE_SAMPLES;

  // 9. Start ADC sampler
  startSampler();

  // 10. Set mode LAST
  currentMode = Mode::TRANSMITTER;

  Serial.printf("[%s] TX MODE - Release GPIO0 to stop\n\n", MY_NAME);
}

// ============================================================
// BUTTON HANDLER (non-blocking debounce)
// ============================================================
void handleButton() {
  static bool      lastRaw      = HIGH;
  static bool      stableState  = HIGH;
  static uint32_t  lastChangeMs = 0;
  static Mode      wantedMode   = Mode::RECEIVER;

  bool    raw = (bool)digitalRead(BUTTON_PIN);
  uint32_t now = millis();

  // Detect any edge
  if (raw != lastRaw) {
    lastChangeMs = now;
    lastRaw      = raw;
  }

  // Only register after stable for DEBOUNCE_MS
  if ((now - lastChangeMs) >= DEBOUNCE_MS && stableState != raw) {
    stableState = raw;
    wantedMode  = (stableState == LOW)
                  ? Mode::TRANSMITTER
                  : Mode::RECEIVER;
  }

  // Apply mode change if needed
  if (wantedMode != currentMode) {
    if (wantedMode == Mode::TRANSMITTER) {
      enterTransmitter();
    } else {
      enterReceiver();
    }
  }
}

// ============================================================
// TX WORK (called from loop in TX mode)
// ============================================================
void doTxWork() {
  if (!adcReady) return;
  adcReady = false;

  // Process audio sample
  int16_t s = processAudio(adcRaw);

  // Apply soft mute: suppress first TX_MUTE_SAMPLES samples
  // This hides any switching transient/click/ring
  if (txMuteCounter > 0) {
    txMuteCounter--;
    s = 0;  // Force silence during mute period
  }

  // Add to packet buffer
  txBuf[txBufIdx++] = s;
  txSampleCnt++;

  // Send when buffer full
  if (txBufIdx >= SAMPLES_PER_PACKET) {
    txBufIdx = 0;

    AudioPacket pkt;
    pkt.timestamp   = txSampleCnt - SAMPLES_PER_PACKET;
    pkt.seq         = txSeq++;
    pkt.sampleCount = SAMPLES_PER_PACKET;
    pkt.flags       = 0;
    memcpy(pkt.samples, txBuf, sizeof(txBuf));
    pkt.crc = calcCRC((uint8_t*)&pkt, sizeof(pkt) - sizeof(pkt.crc));

    esp_now_send(PEER_MAC, (uint8_t*)&pkt, sizeof(pkt));
    txCount++;
  }
}

// ============================================================
// WIFI + ESP-NOW INIT
// ============================================================
void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_max_tx_power(84);
  delay(100);
}

void initESPNOW() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("FATAL: ESP-NOW init failed!");
    while (1) delay(500);
  }

  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onReceive);

  // Register peer (same peer for TX and RX - bidirectional)
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, PEER_MAC, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = false;
  peer.ifidx   = WIFI_IF_STA;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("FATAL: Failed to add peer!");
    while (1) delay(500);
  }
}

// ============================================================
// STARTUP BANNER
// ============================================================
void printBanner() {
  Serial.println("\n╔══════════════════════════════════════════════╗");
  Serial.println("║  ESP-NOW BIDIRECTIONAL AUDIO (Half-Duplex)  ║");
  Serial.println("╚══════════════════════════════════════════════╝");
  Serial.printf("  Device    : %s\n", MY_NAME);
  Serial.printf("  Peer MAC  : %02X:%02X:%02X:%02X:%02X:%02X\n",
    PEER_MAC[0], PEER_MAC[1], PEER_MAC[2],
    PEER_MAC[3], PEER_MAC[4], PEER_MAC[5]);
  Serial.printf("  Channel   : %u\n", ESPNOW_CHANNEL);
  Serial.printf("  Rate      : %lu Hz | %u samp/pkt | %lu pkt/s\n",
    SAMPLE_RATE, SAMPLES_PER_PACKET,
    SAMPLE_RATE / SAMPLES_PER_PACKET);
  Serial.printf("  Jitter Buf: %lu samp = %lu ms\n",
    JITTER_BUF_SIZE,
    (JITTER_BUF_SIZE * 1000) / SAMPLE_RATE);
  Serial.printf("  PTT Mute  : %d samp = %d ms\n",
    TX_MUTE_SAMPLES,
    (TX_MUTE_SAMPLES * 1000) / SAMPLE_RATE);
  Serial.println("\n  [Hold GPIO0] = TALK (TX mode)");
  Serial.println("  [Release]    = LISTEN (RX mode)");
  Serial.println();
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  printBanner();

  // Button (GPIO0 = active LOW with internal pull-up)
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // DAC init
  dac_output_enable(DAC_CHANNEL_1);
  dac_output_voltage(DAC_CHANNEL_1, 128);

  // WiFi + ESP-NOW
  initWiFi();
  initESPNOW();

  // Force transition into RX mode at startup
  currentMode = Mode::TRANSMITTER;   // Trick: force state transition
  enterReceiver();

  Serial.println("✓ System ready!\n");
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
  // 1. Handle button + mode switching
  handleButton();

  // 2. TX audio work (only in TX mode)
  if (currentMode == Mode::TRANSMITTER) {
    doTxWork();
  }

  // 3. Print statistics every 2 seconds
  unsigned long now = millis();
  if (now - lastStats >= 2000) {
    lastStats = now;

    if (currentMode == Mode::TRANSMITTER) {
      float ackRate = (txCount > 0)
        ? (100.0f * (float)ackCount / (float)txCount)
        : 0.0f;
      Serial.printf(
        "[%s][TX] Pkts:%5lu ACK:%5lu(%.0f%%) AGC:%.2f Mute:%d\n",
        MY_NAME,
        (unsigned long)txCount,
        (unsigned long)ackCount,
        ackRate,
        audio.agcGain,
        txMuteCounter);

    } else {
      Serial.printf(
        "[%s][RX] Pkts:%5lu Buf:%4lu/%lu CRC:%lu Undrn:%lu %s\n",
        MY_NAME,
        (unsigned long)rxCount,
        (unsigned long)jbuf.level,
        JITTER_BUF_SIZE,
        (unsigned long)crcErrors,
        (unsigned long)underruns,
        jbuf.buffering ? "(BUFFERING)" : "(PLAYING)");
      underruns = 0;
    }
  }
}