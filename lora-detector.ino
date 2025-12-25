/*
 * LoRa Activity Detector
 * For Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262)
 *
 * Scans for LoRa activity in the 915MHz band (US) using
 * Channel Activity Detection (CAD). Shows fun real-time
 * visualizations of LoRa activity in the area.
 *
 * Display Modes (press PRG button to cycle):
 *   1. SPECTRUM    - Frequency bars showing activity per channel
 *   2. RADAR       - Classic radar sweep with blips
 *   3. BIG PERCENT - Large activity percentage display
 *   4. METER       - Analog VU meter style gauge
 *   5. HEARTBEAT   - EKG-style scrolling waveform
 *   6. TERMINAL    - Retro hacker terminal with log
 *   7. TOWER       - Cell signal tower bars
 *
 * Hardware: Heltec WiFi LoRa 32 V3
 * Library: RadioLib, U8g2
 */

#include <RadioLib.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "secrets.h"  // WiFi credentials and server URL

// ============================================
// CONFIGURATION
// ============================================

#define LORA_BANDWIDTH      125.0
#define LORA_SPREADING      7
#define LORA_CODING_RATE    5

#define CAD_INTERVAL_MS     50
#define DISPLAY_UPDATE_MS   100
#define FREQ_HOP_SCANS      3

// Display Y offset - increase if top of screen is cut off by case
// Set to 0 for no offset, 4-8 if top is obscured
#define DISPLAY_Y_OFFSET    6

// Heltec V3 Pins
#define LORA_NSS            8
#define LORA_DIO1           14
#define LORA_RST            12
#define LORA_BUSY           13
#define OLED_SDA            17
#define OLED_SCL            18
#define OLED_RST            21
#define VEXT_PIN            36
#define BUTTON_PIN          0

// ============================================
// DISPLAY MODES
// ============================================

enum DisplayMode {
  MODE_SPECTRUM,
  MODE_RADAR,
  MODE_BIG_PERCENT,
  MODE_METER,
  MODE_HEARTBEAT,
  MODE_TERMINAL,
  MODE_TOWER,
  MODE_STATS,
  NUM_MODES
};

DisplayMode currentMode = MODE_SPECTRUM;
const char* modeNames[] = {
  "SPECTRUM", "RADAR", "BIG %", "METER", "HEARTBEAT", "TERMINAL", "TOWER", "STATS"
};

// ============================================
// FREQUENCY HOPPING
// ============================================

const float SCAN_FREQUENCIES[] = {
  903.9, 906.3, 909.1, 911.9, 914.9, 917.5, 920.1, 922.9
};
const int NUM_FREQUENCIES = 8;
int currentFreqIndex = 0;
int scansOnCurrentFreq = 0;

#define HISTORY_PER_FREQ 20
int freqActivityCount[NUM_FREQUENCIES];
int freqActivityPercent[NUM_FREQUENCIES];
bool freqHistory[NUM_FREQUENCIES][HISTORY_PER_FREQ];
int freqHistoryIndex[NUM_FREQUENCIES];

// ============================================
// GLOBALS
// ============================================

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);

#define HISTORY_SIZE 100
bool activityHistory[HISTORY_SIZE];
int historyIndex = 0;
unsigned long detectionCount = 0;
unsigned long totalScans = 0;

unsigned long lastCAD = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long startTime = 0;
unsigned long lastButtonPress = 0;

bool loraDetected = false;
int activityPercent = 0;

// Animation variables
int pulseSize = 0;
int radarAngle = 0;
int spectrumBounce[NUM_FREQUENCIES];

// Heartbeat waveform buffer
#define WAVEFORM_WIDTH 128
int waveform[WAVEFORM_WIDTH];
int waveformIndex = 0;

// Terminal log buffer
#define TERMINAL_LINES 5
#define TERMINAL_LINE_LEN 22
char terminalLog[TERMINAL_LINES][TERMINAL_LINE_LEN];
int terminalIndex = 0;
unsigned long uptimeSeconds = 0;
unsigned long lastSecond = 0;

// Meter needle smoothing
float smoothedPercent = 0;

// Stats tracking
int peakActivityPercent = 0;
unsigned long peakDetectionTime = 0;
unsigned long detectionsLastMinute = 0;
unsigned long lastMinuteCheck = 0;
unsigned long detectionsAtLastMinute = 0;
int hotThreshold = 10;  // Activity % to trigger "HOT" indicator

// Double-click detection
unsigned long lastClickTime = 0;
int clickCount = 0;
#define DOUBLE_CLICK_TIME 400  // ms between clicks to count as double-click
#define CLICK_TIMEOUT 500      // ms to wait before processing single click

// WiFi/Upload state
bool isUploading = false;
String uploadStatus = "";
unsigned long uploadStartTime = 0;

// ============================================
// CAD INTERRUPT
// ============================================

volatile bool cadDone = false;

void IRAM_ATTR onCADDone(void) {
  cadDone = true;
}

// ============================================
// FUNCTION PROTOTYPES
// ============================================

void displayError(int errorCode);
void startCAD();
void processCADResult();
void updateDisplay();
void hopToNextFrequency();
void checkButton();
void drawSpectrumView();
void drawRadarView();
void drawBigPercentView();
void drawMeterView();
void drawHeartbeatView();
void drawTerminalView();
void drawTowerView();
void drawStatsView();
void drawBlips(int cx, int cy, int maxR);
void updateAnimations();
void addTerminalLine(const char* line);
void updateStats();
void connectWiFiAndUpload();
void drawUploadScreen();
bool uploadStats();

// ============================================
// SETUP
// ============================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=================================");
  Serial.println("    LoRa Activity Detector");
  Serial.println("    Heltec WiFi LoRa 32 V3");
  Serial.println("=================================\n");

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);
  delay(100);

  display.begin();
  display.setFont(u8g2_font_6x10_tf);
  display.clearBuffer();
  display.drawStr(20, 30, "LoRa Detector");
  display.drawStr(25, 45, "Initializing...");
  display.sendBuffer();

  Serial.print("Initializing SX1262... ");
  int state = radio.begin(SCAN_FREQUENCIES[0], LORA_BANDWIDTH, LORA_SPREADING, LORA_CODING_RATE);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("OK!");
  } else {
    Serial.print("FAILED! Error: ");
    Serial.println(state);
    displayError(state);
    while (true) delay(1000);
  }

  radio.setDio1Action(onCADDone);

  // Initialize arrays
  for (int i = 0; i < HISTORY_SIZE; i++) activityHistory[i] = false;
  for (int i = 0; i < WAVEFORM_WIDTH; i++) waveform[i] = 32;
  for (int i = 0; i < TERMINAL_LINES; i++) terminalLog[i][0] = '\0';

  for (int f = 0; f < NUM_FREQUENCIES; f++) {
    freqActivityCount[f] = 0;
    freqActivityPercent[f] = 0;
    freqHistoryIndex[f] = 0;
    spectrumBounce[f] = 0;
    for (int h = 0; h < HISTORY_PER_FREQ; h++) freqHistory[f][h] = false;
  }

  startTime = millis();
  lastSecond = millis();

  // Add initial terminal message
  addTerminalLine("> LORA SCAN v1.0");
  addTerminalLine("> 903-923 MHz BAND");
  addTerminalLine("> READY...");

  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(25, 15, "READY!");
  display.drawStr(5, 30, "903-923 MHz Band");
  display.drawStr(5, 45, "PRG: cycle views");
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(5, 60, "8 display modes!");
  display.sendBuffer();
  delay(2000);

  Serial.println("Display modes: SPECTRUM, RADAR, BIG%, METER, HEARTBEAT, TERMINAL, TOWER, STATS");
  Serial.println("Press PRG button to cycle\n");
}

// ============================================
// MAIN LOOP
// ============================================

void loop() {
  unsigned long now = millis();

  checkButton();

  if (now - lastCAD >= CAD_INTERVAL_MS) {
    lastCAD = now;
    startCAD();
  }

  if (cadDone) {
    processCADResult();
    cadDone = false;
  }

  if (now - lastDisplayUpdate >= DISPLAY_UPDATE_MS) {
    lastDisplayUpdate = now;
    updateDisplay();
  }

  // Update uptime for terminal
  if (now - lastSecond >= 1000) {
    lastSecond = now;
    uptimeSeconds++;
  }

  // Update stats
  updateStats();
}

// ============================================
// BUTTON HANDLING (with double-click detection)
// ============================================

void checkButton() {
  static bool lastButtonState = HIGH;
  bool currentButtonState = digitalRead(BUTTON_PIN);

  // Detect button press (HIGH -> LOW transition)
  if (lastButtonState == HIGH && currentButtonState == LOW) {
    unsigned long now = millis();

    if (now - lastClickTime < DOUBLE_CLICK_TIME) {
      clickCount++;
    } else {
      clickCount = 1;
    }
    lastClickTime = now;
  }

  lastButtonState = currentButtonState;

  // Process clicks after timeout
  if (clickCount > 0 && millis() - lastClickTime > CLICK_TIMEOUT) {
    if (clickCount >= 2) {
      // Double-click: Upload stats!
      Serial.println("Double-click detected - uploading stats!");
      connectWiFiAndUpload();
    } else {
      // Single click: Cycle display mode
      currentMode = (DisplayMode)((currentMode + 1) % NUM_MODES);
      Serial.print("Mode: ");
      Serial.println(modeNames[currentMode]);
    }
    clickCount = 0;
  }
}

// ============================================
// FREQUENCY HOPPING
// ============================================

void hopToNextFrequency() {
  currentFreqIndex = (currentFreqIndex + 1) % NUM_FREQUENCIES;
  radio.setFrequency(SCAN_FREQUENCIES[currentFreqIndex]);
}

// ============================================
// CAD FUNCTIONS
// ============================================

void startCAD() {
  radio.startChannelScan();
}

void processCADResult() {
  int state = radio.getChannelScanResult();
  totalScans++;
  scansOnCurrentFreq++;

  bool detected = (state == RADIOLIB_LORA_DETECTED);

  if (detected) {
    loraDetected = true;
    detectionCount++;
    freqActivityCount[currentFreqIndex]++;

    // Add to terminal log
    char buf[24];
    sprintf(buf, "> %03lu DET %.0fMHz",
            detectionCount, SCAN_FREQUENCIES[currentFreqIndex]);
    addTerminalLine(buf);

    Serial.print(">>> DETECTED on ");
    Serial.print(SCAN_FREQUENCIES[currentFreqIndex], 1);
    Serial.println(" MHz!");
  } else {
    loraDetected = false;
  }

  // Update histories
  activityHistory[historyIndex] = detected;
  historyIndex = (historyIndex + 1) % HISTORY_SIZE;

  int fi = currentFreqIndex;
  freqHistory[fi][freqHistoryIndex[fi]] = detected;
  freqHistoryIndex[fi] = (freqHistoryIndex[fi] + 1) % HISTORY_PER_FREQ;

  // Calculate percentages
  int activeCount = 0;
  for (int i = 0; i < HISTORY_SIZE; i++) {
    if (activityHistory[i]) activeCount++;
  }
  activityPercent = (activeCount * 100) / HISTORY_SIZE;

  for (int f = 0; f < NUM_FREQUENCIES; f++) {
    int count = 0;
    for (int h = 0; h < HISTORY_PER_FREQ; h++) {
      if (freqHistory[f][h]) count++;
    }
    freqActivityPercent[f] = (count * 100) / HISTORY_PER_FREQ;
  }

  // Update waveform for heartbeat view
  waveform[waveformIndex] = detected ? 10 : 32;
  waveformIndex = (waveformIndex + 1) % WAVEFORM_WIDTH;

  if (scansOnCurrentFreq >= FREQ_HOP_SCANS) {
    scansOnCurrentFreq = 0;
    hopToNextFrequency();
  }
}

// ============================================
// TERMINAL LOG
// ============================================

void addTerminalLine(const char* line) {
  strncpy(terminalLog[terminalIndex], line, TERMINAL_LINE_LEN - 1);
  terminalLog[terminalIndex][TERMINAL_LINE_LEN - 1] = '\0';
  terminalIndex = (terminalIndex + 1) % TERMINAL_LINES;
}

// ============================================
// DISPLAY UPDATE
// ============================================

void updateDisplay() {
  display.clearBuffer();

  switch (currentMode) {
    case MODE_SPECTRUM:   drawSpectrumView(); break;
    case MODE_RADAR:      drawRadarView(); break;
    case MODE_BIG_PERCENT: drawBigPercentView(); break;
    case MODE_METER:      drawMeterView(); break;
    case MODE_HEARTBEAT:  drawHeartbeatView(); break;
    case MODE_TERMINAL:   drawTerminalView(); break;
    case MODE_TOWER:      drawTowerView(); break;
    case MODE_STATS:      drawStatsView(); break;
  }

  display.sendBuffer();
  updateAnimations();
}

// ============================================
// 1. SPECTRUM BARS VIEW
// ============================================

void drawSpectrumView() {
  const int Y = DISPLAY_Y_OFFSET;
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(0, 7 + Y, "SPECTRUM");

  char buf[16];
  sprintf(buf, "Det:%lu", detectionCount);
  display.drawStr(75, 7 + Y, buf);
  display.drawLine(0, 9 + Y, 127, 9 + Y);

  int barWidth = 12, barGap = 4, maxBarHeight = 36, barBottom = 56, startX = 4;

  for (int i = 0; i < NUM_FREQUENCIES; i++) {
    int x = startX + i * (barWidth + barGap);
    int targetHeight = (freqActivityPercent[i] * maxBarHeight) / 100;
    int bounce = (i == currentFreqIndex && loraDetected) ? spectrumBounce[i] : 0;
    int barHeight = constrain(targetHeight + bounce, 2, maxBarHeight);
    int barTop = barBottom - barHeight;

    display.drawFrame(x, barBottom - maxBarHeight, barWidth, maxBarHeight);
    display.drawBox(x + 1, barTop + 1, barWidth - 2, barHeight - 1);

    if (i == currentFreqIndex) {
      display.drawTriangle(x + barWidth/2, barBottom - maxBarHeight - 5,
                           x + barWidth/2 - 3, barBottom - maxBarHeight - 1,
                           x + barWidth/2 + 3, barBottom - maxBarHeight - 1);
    }

    sprintf(buf, "%d", ((int)SCAN_FREQUENCIES[i]) % 100);
    display.setFont(u8g2_font_4x6_tf);
    display.drawStr(x + 2, 63, buf);
  }

  if (loraDetected) {
    display.setFont(u8g2_font_5x7_tf);
    display.drawStr(105, 63, ">>>");
  }
}

// ============================================
// 2. RADAR VIEW
// ============================================

void drawRadarView() {
  const int Y = DISPLAY_Y_OFFSET;
  int cx = 32, cy = 32 + Y/2, r = 26;

  display.drawCircle(cx, cy, 9);
  display.drawCircle(cx, cy, 18);
  display.drawCircle(cx, cy, 26);
  display.drawLine(cx - r, cy, cx + r, cy);
  display.drawLine(cx, cy - r, cx, cy + r);

  float angle = radarAngle * PI / 180.0;
  display.drawLine(cx, cy, cx + r * cos(angle), cy + r * sin(angle));

  if (loraDetected) {
    display.drawCircle(cx, cy, pulseSize % 28);
  }
  drawBlips(cx, cy, r);

  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(68, 8 + Y, "RADAR");
  display.drawLine(68, 10 + Y, 127, 10 + Y);

  char buf[20];
  sprintf(buf, "%.0fMHz", SCAN_FREQUENCIES[currentFreqIndex]);
  display.drawStr(68, 20 + Y, buf);

  display.drawStr(68, 32 + Y, "Activity:");
  display.drawFrame(68, 34 + Y, 58, 8);
  display.drawBox(70, 36 + Y, (activityPercent * 54) / 100, 4);

  sprintf(buf, "%d%%", activityPercent);
  display.drawStr(100, 32 + Y, buf);

  sprintf(buf, "Det:%lu", detectionCount);
  display.drawStr(68, 50 + Y, buf);

  display.drawStr(68, 63, loraDetected ? "**SIGNAL**" : "Scanning...");
}

void drawBlips(int cx, int cy, int maxR) {
  randomSeed(detectionCount / 10);
  for (int i = 0; i < activityPercent / 20; i++) {
    float a = random(360) * PI / 180.0;
    int d = random(10, maxR - 2);
    display.drawDisc(cx + d * cos(a), cy + d * sin(a), 2);
  }
  if (loraDetected) display.drawDisc(cx, cy, 4);
}

// ============================================
// 3. BIG PERCENT VIEW
// ============================================

void drawBigPercentView() {
  const int Y = DISPLAY_Y_OFFSET;
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(0, 7 + Y, "ACTIVITY LEVEL");
  display.drawLine(0, 9 + Y, 127, 9 + Y);

  // Giant percentage
  char buf[8];
  sprintf(buf, "%d", activityPercent);

  display.setFont(u8g2_font_logisoso32_tn);
  int width = display.getStrWidth(buf);
  display.drawStr((128 - width) / 2 - 10, 46 + Y, buf);

  display.setFont(u8g2_font_ncenB14_tr);
  display.drawStr((128 + width) / 2 - 5, 46 + Y, "%");

  // Progress bar
  display.drawFrame(10, 50, 108, 8);
  int barW = (activityPercent * 104) / 100;
  if (barW > 0) display.drawBox(12, 52, barW, 4);

  // Stats at bottom
  display.setFont(u8g2_font_5x7_tf);
  sprintf(buf, "Det:%lu", detectionCount);
  display.drawStr(5, 63, buf);

  sprintf(buf, "%.0fMHz", SCAN_FREQUENCIES[currentFreqIndex]);
  display.drawStr(80, 63, buf);

  // Flash on detection
  if (loraDetected) {
    display.drawFrame(0, Y, 128, 64 - Y);
    display.drawFrame(1, Y + 1, 126, 62 - Y);
  }
}

// ============================================
// 4. ANALOG METER VIEW
// ============================================

void drawMeterView() {
  const int Y = DISPLAY_Y_OFFSET;
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(30, 7 + Y, "SIGNAL METER");

  int cx = 64, cy = 48 + Y/2, r = 36;

  // Smooth the needle
  smoothedPercent += (activityPercent - smoothedPercent) * 0.15;

  // Draw arc (semicircle using lines)
  for (int a = 180; a <= 360; a += 10) {
    float rad = a * PI / 180.0;
    int x1 = cx + (r - 2) * cos(rad);
    int y1 = cy + (r - 2) * sin(rad);
    int x2 = cx + r * cos(rad);
    int y2 = cy + r * sin(rad);
    display.drawLine(x1, y1, x2, y2);
  }

  // Draw tick marks and labels
  display.setFont(u8g2_font_4x6_tf);
  for (int pct = 0; pct <= 100; pct += 25) {
    float angle = (180 + (pct * 180 / 100)) * PI / 180.0;
    int tx = cx + (r + 5) * cos(angle);
    int ty = cy + (r + 5) * sin(angle);
    char buf[4];
    sprintf(buf, "%d", pct);
    display.drawStr(tx - 4, ty + 2, buf);
  }

  // Draw needle
  float needleAngle = (180 + (smoothedPercent * 180 / 100)) * PI / 180.0;
  int nx = cx + (r - 8) * cos(needleAngle);
  int ny = cy + (r - 8) * sin(needleAngle);
  display.drawLine(cx, cy, nx, ny);
  display.drawDisc(cx, cy, 3);

  // Stats
  display.setFont(u8g2_font_5x7_tf);
  char buf[16];
  sprintf(buf, "%d%%", activityPercent);
  display.drawStr(55, 63, buf);

  if (loraDetected) {
    display.drawStr(90, 63, ">>>");
  }
}

// ============================================
// 5. HEARTBEAT/EKG VIEW
// ============================================

void drawHeartbeatView() {
  const int Y = DISPLAY_Y_OFFSET;
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(0, 7 + Y, "HEARTBEAT");

  char buf[16];
  sprintf(buf, "Det:%lu", detectionCount);
  display.drawStr(75, 7 + Y, buf);
  display.drawLine(0, 9 + Y, 127, 9 + Y);

  // Draw waveform (shifted down)
  int baseY = 36 + Y;
  for (int i = 0; i < WAVEFORM_WIDTH - 1; i++) {
    int idx1 = (waveformIndex + i) % WAVEFORM_WIDTH;
    int idx2 = (waveformIndex + i + 1) % WAVEFORM_WIDTH;

    int y1 = waveform[idx1];
    int y2 = waveform[idx2];

    // Add some variation to the baseline
    if (y1 > 30) y1 = baseY + random(-1, 2);
    else y1 = baseY - 18;  // Spike up

    if (y2 > 30) y2 = baseY + random(-1, 2);
    else y2 = baseY - 18;

    display.drawLine(i, y1, i + 1, y2);
  }

  // Stats at bottom (no offset - keep at bottom)
  display.setFont(u8g2_font_5x7_tf);
  sprintf(buf, "Act:%d%%", activityPercent);
  display.drawStr(0, 63, buf);

  sprintf(buf, "%.0fMHz", SCAN_FREQUENCIES[currentFreqIndex]);
  display.drawStr(50, 63, buf);

  display.drawStr(100, 63, loraDetected ? ">>>" : "---");
}

// ============================================
// 6. RETRO TERMINAL VIEW
// ============================================

void drawTerminalView() {
  const int Y = DISPLAY_Y_OFFSET;
  display.setFont(u8g2_font_5x7_tf);

  // Draw terminal frame
  display.drawFrame(0, Y, 128, 64 - Y);
  display.drawStr(3, 8 + Y, "LORA TERMINAL");
  display.drawLine(1, 10 + Y, 126, 10 + Y);

  // Draw log lines
  display.setFont(u8g2_font_4x6_tf);
  for (int i = 0; i < TERMINAL_LINES; i++) {
    int lineIdx = (terminalIndex + i) % TERMINAL_LINES;
    display.drawStr(3, 18 + Y + i * 8, terminalLog[lineIdx]);
  }

  // Blinking cursor
  if ((millis() / 500) % 2 == 0) {
    display.drawStr(3, 56, "_");
  }

  // Status bar
  display.setFont(u8g2_font_4x6_tf);
  char buf[24];
  sprintf(buf, "UP:%lus DET:%lu %d%%", uptimeSeconds, detectionCount, activityPercent);
  display.drawStr(25, 62, buf);
}

// ============================================
// 7. SIGNAL TOWER VIEW
// ============================================

void drawTowerView() {
  const int Y = DISPLAY_Y_OFFSET;
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(30, 7 + Y, "SIGNAL TOWER");
  display.drawLine(0, 9 + Y, 127, 9 + Y);

  // Draw 5 signal bars
  int barWidths[] = {12, 12, 12, 12, 12};
  int barHeights[] = {8, 14, 22, 30, 38};
  int startX = 20;
  int gap = 6;
  int bottom = 54;

  for (int i = 0; i < 5; i++) {
    int x = startX + i * (barWidths[i] + gap);
    int h = barHeights[i];
    int y = bottom - h;

    // Draw bar outline
    display.drawFrame(x, y, barWidths[i], h);

    // Fill if activity level is high enough
    int threshold = (i + 1) * 20;  // 20%, 40%, 60%, 80%, 100%
    if (activityPercent >= threshold) {
      display.drawBox(x + 1, y + 1, barWidths[i] - 2, h - 2);
    }
  }

  // Detection indicator
  if (loraDetected) {
    // Draw signal waves
    int wx = startX + 5 * (12 + gap) - 5;
    display.drawCircle(wx, 38, 5);
    display.drawCircle(wx, 38, 10);
    display.drawCircle(wx, 38, 15);
  }

  // Stats
  display.setFont(u8g2_font_6x10_tf);
  char buf[16];
  sprintf(buf, "%d%%", activityPercent);
  display.drawStr(50, 63, buf);

  display.setFont(u8g2_font_5x7_tf);
  sprintf(buf, "Det:%lu", detectionCount);
  display.drawStr(85, 63, buf);
}

// ============================================
// 8. STATS VIEW
// ============================================

void drawStatsView() {
  const int Y = DISPLAY_Y_OFFSET;
  char buf[24];

  display.setFont(u8g2_font_5x7_tf);

  // Title with HOT indicator
  if (activityPercent >= hotThreshold) {
    display.drawStr(0, 7 + Y, "** HOT **  STATS");
  } else {
    display.drawStr(0, 7 + Y, "SESSION STATS");
  }
  display.drawLine(0, 9 + Y, 127, 9 + Y);

  display.setFont(u8g2_font_5x7_tf);
  int lineY = 18 + Y;
  int lineH = 9;

  // Uptime
  int hours = uptimeSeconds / 3600;
  int mins = (uptimeSeconds % 3600) / 60;
  int secs = uptimeSeconds % 60;
  sprintf(buf, "Uptime: %02d:%02d:%02d", hours, mins, secs);
  display.drawStr(0, lineY, buf);
  lineY += lineH;

  // Total detections
  sprintf(buf, "Total Detections: %lu", detectionCount);
  display.drawStr(0, lineY, buf);
  lineY += lineH;

  // Detections per minute
  sprintf(buf, "Det/min: %lu", detectionsLastMinute);
  display.drawStr(0, lineY, buf);
  lineY += lineH;

  // Current activity
  sprintf(buf, "Current Activity: %d%%", activityPercent);
  display.drawStr(0, lineY, buf);
  lineY += lineH;

  // Peak activity
  sprintf(buf, "Peak Activity: %d%%", peakActivityPercent);
  display.drawStr(0, lineY, buf);

  // HOT indicator bar at bottom if active
  if (activityPercent >= hotThreshold) {
    // Flashing HOT bar
    if ((millis() / 300) % 2 == 0) {
      display.drawBox(0, 58, 128, 6);
      display.setDrawColor(0);
      display.drawStr(50, 63, "HOT!");
      display.setDrawColor(1);
    }
  }
}

void updateStats() {
  // Track peak activity
  if (activityPercent > peakActivityPercent) {
    peakActivityPercent = activityPercent;
    peakDetectionTime = millis();
  }

  // Calculate detections per minute
  unsigned long now = millis();
  if (now - lastMinuteCheck >= 60000) {
    detectionsLastMinute = detectionCount - detectionsAtLastMinute;
    detectionsAtLastMinute = detectionCount;
    lastMinuteCheck = now;
  }
}

// ============================================
// ANIMATIONS
// ============================================

void updateAnimations() {
  radarAngle = (radarAngle + 6) % 360;

  if (loraDetected) {
    pulseSize += 2;
    if (pulseSize > 30) pulseSize = 0;
  }

  for (int i = 0; i < NUM_FREQUENCIES; i++) {
    if (spectrumBounce[i] > 0) spectrumBounce[i] -= 2;
  }
  if (loraDetected) spectrumBounce[currentFreqIndex] = 8;
}

// ============================================
// ERROR DISPLAY
// ============================================

void displayError(int errorCode) {
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(25, 25, "INIT FAILED!");
  char buf[32];
  sprintf(buf, "Error: %d", errorCode);
  display.drawStr(30, 45, buf);
  display.sendBuffer();
}

// ============================================
// WIFI AND UPLOAD FUNCTIONS
// ============================================

void drawUploadScreen() {
  const int Y = DISPLAY_Y_OFFSET;
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(20, 15 + Y, "UPLOADING...");
  display.drawLine(0, 20 + Y, 127, 20 + Y);

  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(0, 35 + Y, uploadStatus.c_str());

  // Spinning indicator
  int spin = (millis() / 200) % 4;
  const char* spinner[] = {"|", "/", "-", "\\"};
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(60, 55, spinner[spin]);

  display.sendBuffer();
}

void connectWiFiAndUpload() {
  isUploading = true;
  uploadStartTime = millis();

  // Show connecting screen
  uploadStatus = "Connecting WiFi...";
  drawUploadScreen();

  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Wait for connection (max 15 seconds)
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
    drawUploadScreen();
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi connection failed!");
    uploadStatus = "WiFi FAILED!";
    drawUploadScreen();
    delay(2000);
    WiFi.disconnect(true);
    isUploading = false;
    return;
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  uploadStatus = "WiFi OK! Uploading...";
  drawUploadScreen();

  // Attempt upload
  bool success = uploadStats();

  if (success) {
    uploadStatus = "Upload SUCCESS!";
    Serial.println("Upload successful!");
  } else {
    uploadStatus = "Upload FAILED!";
    Serial.println("Upload failed!");
  }

  drawUploadScreen();
  delay(2000);

  // Disconnect WiFi to save power
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  isUploading = false;
  Serial.println("WiFi disconnected");
}

bool uploadStats() {
  HTTPClient http;

  Serial.print("Uploading to: ");
  Serial.println(SERVER_URL);

  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);  // 10 second timeout

  // Build JSON payload
  String json = "{";
  json += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
  json += "\"uptime_seconds\":" + String(uptimeSeconds) + ",";
  json += "\"total_detections\":" + String(detectionCount) + ",";
  json += "\"detections_per_min\":" + String(detectionsLastMinute) + ",";
  json += "\"current_activity_pct\":" + String(activityPercent) + ",";
  json += "\"peak_activity_pct\":" + String(peakActivityPercent) + ",";
  json += "\"freq_detections\":[";
  for (int i = 0; i < NUM_FREQUENCIES; i++) {
    json += String(freqActivityCount[i]);
    if (i < NUM_FREQUENCIES - 1) json += ",";
  }
  json += "]}";

  Serial.println("Payload: " + json);

  int httpCode = http.POST(json);

  Serial.print("HTTP Response: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("Response: " + response);
  }

  http.end();

  return (httpCode == 200);
}
