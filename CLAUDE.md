# LoRa Activity Detector - Development Notes

A portable LoRa signal detector for the Heltec WiFi LoRa 32 V3. Scans the 900 MHz ISM band for LoRa transmissions with multiple visualization modes and cloud upload capability.

## Hardware

| Component | Details |
|-----------|---------|
| **Board** | Heltec WiFi LoRa 32 V3 |
| **MCU** | ESP32-S3 (dual-core, 240MHz) |
| **LoRa** | SX1262 (900 MHz US ISM band) |
| **Display** | 0.96" OLED 128x64 (SSD1306) |
| **Connectivity** | WiFi 2.4GHz, Bluetooth LE, LoRa |

## Features

- **16 Display Modes** - 8 general views + 8 frequency-specific heartbeat monitors
- **Frequency Hopping** - Scans 8 frequencies across 903-923 MHz
- **Channel Activity Detection (CAD)** - Hardware-based LoRa preamble detection
- **Per-Frequency Monitoring** - Dedicated heartbeat display for each frequency
- **Double-click WiFi Upload** - Upload stats to cloud dashboard
- **Cloud Dashboard** - https://lora-detector.fly.dev/
- **SQLite Persistence** - 1 year data retention with historical summaries
- **Historical Analysis** - 7 day, 30 day, 90 day, 1 year aggregations

## Quick Start

```bash
./deploy.sh          # Compile and upload
./deploy.sh monitor  # View serial output (run in separate terminal)
```

## Commands

```bash
./deploy.sh          # Compile + upload (default)
./deploy.sh compile  # Compile only
./deploy.sh upload   # Upload only
./deploy.sh monitor  # Serial monitor
./deploy.sh libs     # Install required libraries
```

## Required Libraries

Installed automatically by deploy.sh:

- **RadioLib** - LoRa radio control with CAD support
- **U8g2** - OLED display graphics

## Display Modes

Press PRG button (single click) to cycle through 16 modes:

### General Views (Modes 1-8)

| Mode | Description |
|------|-------------|
| SPECTRUM | 8 frequency bars with current channel indicator |
| RADAR | Classic radar sweep with detection blips |
| BIG_PERCENT | Large activity percentage display |
| METER | Analog VU meter with needle animation |
| HEARTBEAT | EKG-style scrolling waveform (all frequencies) |
| TERMINAL | Retro hacker terminal with scrolling log |
| TOWER | Cell signal tower style bars |
| STATS | Detailed text statistics with "HOT" indicator |

### Frequency-Specific Views (Modes 9-16)

Each frequency has its own dedicated heartbeat monitor:

| Mode | Frequency | Primary Use |
|------|-----------|-------------|
| 9 | 903.9 MHz | LoRaWAN Ch0 |
| 10 | 906.3 MHz | LoRaWAN Uplink |
| 11 | 909.1 MHz | LoRaWAN Mid |
| 12 | 911.9 MHz | Meshtastic |
| 13 | 914.9 MHz | LoRaWAN |
| 14 | 917.5 MHz | Amazon Sidewalk |
| 15 | 920.1 MHz | LoRaWAN |
| 16 | 922.9 MHz | LoRaWAN Downlink |

Each frequency screen shows:
- Title with frequency value
- Subtitle with primary use (LoRaWAN, Sidewalk, Meshtastic, etc.)
- EKG heartbeat waveform for that frequency only
- Detection count and activity % for that specific frequency

## Button Controls

| Action | Function |
|--------|----------|
| Single click PRG | Cycle to next display mode |
| Double click PRG | Connect WiFi and upload stats (must be fast - within 250ms) |

## How It Works

Uses **Channel Activity Detection (CAD)** - a hardware feature of the SX1262 chip that detects LoRa preambles without decoding the full packet. Much faster and more power-efficient than packet reception.

- Scans every 50ms
- Hops between 8 frequencies (3 scans per frequency)
- Tracks detections over 5-second rolling window
- Calculates activity percentage per frequency
- Cannot decode packet contents (just detects presence)

## Scan Frequencies

| Frequency | Primary Use |
|-----------|-------------|
| 903.9 MHz | LoRaWAN US915 Ch0 |
| 906.3 MHz | LoRaWAN US915 uplink |
| 909.1 MHz | LoRaWAN US915 |
| 911.9 MHz | Meshtastic default |
| 914.9 MHz | LoRaWAN US915 |
| 917.5 MHz | Amazon Sidewalk |
| 920.1 MHz | LoRaWAN US915 |
| 922.9 MHz | LoRaWAN US915 downlink |

## What It Detects

Any LoRa transmission in the 900 MHz band:
- **Amazon Sidewalk** - Ring doorbells, Echo speakers, Tile trackers
- **Meshtastic** - Off-grid mesh communicators
- **LoRaWAN** - Smart meters, parking sensors, agricultural sensors
- **DIY LoRa** - Custom projects, asset trackers

## WiFi Upload Configuration

Create `secrets.h` (gitignored):

```cpp
#ifndef SECRETS_H
#define SECRETS_H

#define WIFI_SSID "YourWiFiName"
#define WIFI_PASSWORD "YourPassword"
#define SERVER_URL "https://lora-detector.fly.dev/upload"
#define DEVICE_ID "lora-detector-1"

#endif
```

## Server Backend

**URL:** https://lora-detector.fly.dev/

### Stack
- Go 1.24 with pure-Go SQLite (modernc.org/sqlite)
- Fly.io hosting with 1GB persistent volume
- Auto-cleanup of data older than 1 year

### API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Dashboard web interface |
| `/upload` | POST | Receive stats from detector |
| `/stats` | GET | Plain text stats summary |
| `/api/stats` | GET | JSON current stats |
| `/api/history` | GET | JSON historical summaries (7/30/90/365 days) |

### Upload Payload

```json
{
  "device_id": "lora-detector-1",
  "uptime_seconds": 1847,
  "total_detections": 386,
  "detections_per_min": 15,
  "current_activity_pct": 5,
  "peak_activity_pct": 23,
  "freq_detections": [45, 52, 38, 67, 41, 55, 48, 40]
}
```

### Deploy Server

```bash
cd server

# First time - create volume
fly volumes create lora_data --region dfw --size 1
fly scale count 1

# Deploy
fly deploy
```

## Firmware Configuration

Key constants in `lora-detector.ino`:

```cpp
#define DISPLAY_Y_OFFSET    6       // Offset if case covers top of display
#define CAD_INTERVAL_MS     50      // Scan rate
#define FREQ_HOP_SCANS      3       // Scans per frequency before hopping
#define DOUBLE_CLICK_TIME   250     // ms between clicks for double-click (fast!)
#define CLICK_TIMEOUT       350     // ms to wait before processing click

const float SCAN_FREQUENCIES[] = {
  903.9, 906.3, 909.1, 911.9, 914.9, 917.5, 920.1, 922.9
};
```

## Pin Reference (Heltec V3)

| Function | GPIO |
|----------|------|
| LoRa NSS | 8 |
| LoRa DIO1 | 14 |
| LoRa RST | 12 |
| LoRa BUSY | 13 |
| OLED SDA | 17 |
| OLED SCL | 18 |
| OLED RST | 21 |
| Vext (OLED power) | 36 |
| PRG Button | 0 |

## Troubleshooting

### "INIT FAILED" on display
- Check antenna is connected (transmitting without antenna can damage the radio)
- Verify correct board selected in deploy.sh

### No detections
- LoRa is relatively uncommon - try near:
  - Amazon Echo/Ring devices (Sidewalk)
  - Smart home hubs
  - Industrial areas with sensors
- Try different frequencies

### Serial not working
- Board uses USB-C
- May need to hold BOOT button while connecting
- Check `ls /dev/ttyUSB* /dev/ttyACM*`

### Display cut off by case
- Adjust `DISPLAY_Y_OFFSET` constant (default: 6 pixels)

### WiFi upload fails
- Verify credentials in secrets.h
- Check signal strength (detector may be far from router)
- Verify server is running: `curl https://lora-detector.fly.dev/stats`

## Build Info

| Metric | Value |
|--------|-------|
| Board FQBN | esp32:esp32:heltec_wifi_lora_32_V3 |
| Firmware Size | ~1.1 MB (33% of flash) |
| Compile Time | ~45 seconds |
| Server Image | ~12 MB (Go binary + Alpine) |

## Project Files

```
lora-detector/
├── lora-detector.ino              # Main firmware
├── secrets.h                      # WiFi credentials (gitignored)
├── deploy.sh                      # Build script
├── CLAUDE.md                      # This file
├── README.md                      # User documentation
├── SCREEN_LAYOUTS.md              # Display mode designs
├── fly.io-LoRa-Detector-Dashboard.png  # Dashboard screenshot
├── .gitignore
├── build/                         # Compiled output
└── server/
    ├── main.go                    # Go server + SQLite
    ├── fly.toml                   # Fly.io config
    ├── Dockerfile                 # Go 1.24 Alpine
    ├── go.mod                     # Dependencies
    └── go.sum                     # Checksums
```
