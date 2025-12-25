# LoRa Detector Screen Layout Ideas

Display: 128x64 OLED (SSD1306) on Heltec WiFi LoRa 32 V3

---

## Current: RADAR VIEW (Default)

```
┌────────────────────────────────────────┐
│  ╭───────╮   │ LoRa SCAN              │
│  │   +   │   │ 915.0 MHz              │
│  │  /    │   │ Activity: [████──] 45% │
│  │       │   │ Detect: 127            │
│  ╰───────╯   │ ** SIGNAL **           │
└────────────────────────────────────────┘
```

**Features:**
- Animated radar sweep line
- Concentric circles
- Blips appear when LoRa detected
- Stats panel on right side

**Pros:** Looks cool, spy/military aesthetic
**Cons:** Hard to read activity level at a glance

---

## Option 1: BIG PERCENT

```
┌────────────────────────────┐
│                            │
│         4 5 %              │
│     [████████░░░░]         │
│                            │
│  915MHz    Detects: 127    │
└────────────────────────────┘
```

**Features:**
- Giant activity percentage (main focus)
- Simple horizontal progress bar
- Minimal stats at bottom

**Pros:**
- Super easy to read from across the room
- Clear at-a-glance status
- Simple and bold

**Cons:**
- Less visually interesting
- Doesn't show detection events in real-time

**Implementation Notes:**
- Use largest font available (u8g2_font_logisoso32_tn or similar)
- Bar could pulse/animate when detecting
- Consider color inversion when activity > 50%

---

## Option 2: SIGNAL METER (VU/Analog Gauge)

```
┌────────────────────────────┐
│    LoRa Signal Meter       │
│      ╭─────────────╮       │
│     ╱   |   |   |   ╲      │
│    ╱    |   |   |    ╲     │
│   ◯─────────⟋─────────◯   │
│   0%              100%     │
└────────────────────────────┘
```

**Features:**
- Semicircular gauge with tick marks
- Animated needle that swings based on activity
- Smooth needle movement with momentum/damping

**Pros:**
- Classic analog aesthetic
- Satisfying needle animation
- Intuitive reading

**Cons:**
- Complex to draw (arc math)
- May be busy on small screen

**Implementation Notes:**
- Draw arc using line segments
- Needle angle: map activity 0-100% to 180-0 degrees
- Add needle damping: `displayedValue += (actualValue - displayedValue) * 0.1`
- Tick marks at 0, 25, 50, 75, 100%

---

## Option 3: HEARTBEAT / EKG

```
┌────────────────────────────┐
│ LoRa: 915MHz    Det: 127   │
│                            │
│ ___╱╲___╱╲_____╱╲___╱╲____ │
│                            │
│ Activity: 23%   SCANNING   │
└────────────────────────────┘
```

**Features:**
- Scrolling waveform graph
- Flatline when quiet
- Sharp spike when LoRa detected
- Continuous left-to-right scroll

**Pros:**
- Immediate visual feedback on detection
- Medical/sci-fi aesthetic
- Shows detection timing/patterns

**Cons:**
- Spikes may be brief and easy to miss
- Requires continuous redraw

**Implementation Notes:**
- Store array of Y values (width of screen)
- Shift array left each frame, add new value on right
- Normal Y = center line
- Detection Y = spike up (and down for EKG look)
- Draw as connected line segments

---

## Option 4: SPECTRUM BARS

```
┌────────────────────────────┐
│ 906 912 915 918 923  MHz   │
│  █   █   █   █   █         │
│  █   █   ██  █   █         │
│  ██  ██  ██  ██  █         │
│  ██  ██  ███ ██  ██        │
│ [SCANNING]    Det: 127     │
└────────────────────────────┘
```

**Features:**
- Multiple frequency bands displayed
- Bar height = recent activity on that frequency
- Requires frequency hopping to be meaningful

**Pros:**
- Shows activity across the band
- Spectrum analyzer aesthetic
- Can identify which frequencies are active

**Cons:**
- Requires implementing frequency hopping
- More complex logic
- May be misleading if not hopping

**Implementation Notes:**
- Enable `hopToNextFrequency()` function
- Track activity per frequency in separate arrays
- 5-6 bars fit comfortably
- Bars decay over time if no detection
- Frequencies: 906, 912, 915, 918, 923 MHz (US LoRaWAN/Sidewalk)

---

## Option 5: RETRO TERMINAL

```
┌────────────────────────────┐
│> LORA SCAN v1.0            │
│> 915.0 MHz ACTIVE          │
│> 14:23:01 SIGNAL DETECTED  │
│> 14:23:03 SIGNAL DETECTED  │
│> 14:23:15 CHANNEL CLEAR    │
│> TOTAL: 127  RATE: 23%  _  │
└────────────────────────────┘
```

**Features:**
- Scrolling text log
- Timestamp on each event
- Blinking cursor
- Hacker/terminal aesthetic

**Pros:**
- Shows event history
- Satisfying scrolling text
- Easy to see when detections happened

**Cons:**
- Requires RTC or uptime counter for timestamps
- Small font may be hard to read
- Text-heavy

**Implementation Notes:**
- Store circular buffer of last N log lines
- Each line: timestamp + message
- Use small fixed-width font (u8g2_font_5x7_tf)
- Blink cursor by toggling `_` visibility
- Scroll up when new line added

---

## Option 6: SIGNAL TOWER (Cell Bars)

```
┌────────────────────────────┐
│                      ╷     │
│   915 MHz       ╷   ╷│╷    │
│            ╷   ╷│╷  │││    │
│  Det: 127  │╷  │││  │││    │
│  Act: 45%  ││  │││  │││    │
│           ▔▔▔▔▔▔▔▔▔▔▔▔▔    │
└────────────────────────────┘
```

**Features:**
- Classic cell signal strength bars
- 5 bars of increasing height
- Bars light up based on activity percentage

**Pros:**
- Universally understood visual
- Very easy to read
- Clean and simple

**Cons:**
- Less unique/interesting
- Discrete levels (not smooth)

**Implementation Notes:**
- 5 bars at 20% increments
- Bar heights: 8, 16, 24, 32, 40 pixels
- Bar width: ~10px with 5px gaps
- Fill bars from left based on activity %
- Could animate bars "bouncing" on detection

---

## Option 7: MATRIX RAIN (Bonus Idea)

```
┌────────────────────────────┐
│ 1 0   1 0 1   0 1 1 0   1  │
│ 0 1 0   1   0 1 0   1 0    │
│   0 1 0   1 0   1 0   0 1  │
│ 1   0 1 0   0 1   0 1   0  │
│────────────────────────────│
│ 915MHz  Act:45%  Det:127   │
└────────────────────────────┘
```

**Features:**
- Falling binary digits (Matrix-style)
- Speed/density increases with activity
- Stats bar at bottom

**Pros:**
- Very cool aesthetic
- Activity level intuitive (more rain = more activity)
- Mesmerizing to watch

**Cons:**
- May be distracting
- CPU intensive with many falling characters

**Implementation Notes:**
- Array of "raindrops" with X position and Y position
- Each raindrop falls at random speed
- More activity = more raindrops spawned
- Characters: 0, 1 (or hex digits for variety)

---

## Option 8: CIRCULAR PROGRESS (Bonus Idea)

```
┌────────────────────────────┐
│                            │
│      ╭──────────╮          │
│     ╱   45%      ╲         │
│    │    DET       │        │
│    │    127       │        │
│     ╲            ╱         │
│      ╰──────────╯          │
│   915 MHz    SCANNING      │
└────────────────────────────┘
```

**Features:**
- Circular progress ring
- Percentage and stats in center
- Ring fills clockwise based on activity

**Pros:**
- Modern/clean look
- Single focal point
- Smooth animation possible

**Cons:**
- Arc drawing is complex
- May look incomplete at low percentages

---

## Implementation Plan

To add multiple layouts with cycling:

1. Create enum for layout modes:
```cpp
enum DisplayMode {
  MODE_RADAR,      // Current default
  MODE_BIG_PERCENT,
  MODE_SIGNAL_METER,
  MODE_HEARTBEAT,
  MODE_SPECTRUM,
  MODE_TERMINAL,
  MODE_SIGNAL_TOWER,
  NUM_MODES
};
```

2. Add button handler to cycle modes:
```cpp
// Heltec V3 has PRG button on GPIO 0
#define BUTTON_PIN 0

if (digitalRead(BUTTON_PIN) == LOW) {
  currentMode = (currentMode + 1) % NUM_MODES;
  delay(300); // Debounce
}
```

3. Update display function:
```cpp
void updateDisplay() {
  display.clearBuffer();

  switch (currentMode) {
    case MODE_RADAR:       drawRadarView(); break;
    case MODE_BIG_PERCENT: drawBigPercent(); break;
    case MODE_HEARTBEAT:   drawHeartbeat(); break;
    // etc.
  }

  display.sendBuffer();
}
```

---

## Quick Reference: U8g2 Fonts

| Font | Size | Use Case |
|------|------|----------|
| `u8g2_font_logisoso32_tn` | 32px | Big numbers |
| `u8g2_font_ncenB24_tr` | 24px | Large text |
| `u8g2_font_ncenB14_tr` | 14px | Medium headers |
| `u8g2_font_6x10_tf` | 10px | Normal text |
| `u8g2_font_5x7_tf` | 7px | Small/compact |
| `u8g2_font_4x6_tf` | 6px | Tiny text |

---

## Favorites (Ranked by Readability)

1. **BIG PERCENT** - Clearest, best for at-a-glance
2. **SIGNAL TOWER** - Universally understood
3. **HEARTBEAT** - Best real-time feedback
4. **SIGNAL METER** - Classic and cool
5. **RADAR** - Current, looks awesome but busy
6. **SPECTRUM** - Great if frequency hopping enabled
7. **TERMINAL** - Fun but text-heavy
