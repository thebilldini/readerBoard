/*
 * NeoPixel Reader Board
 * Hardware: Arduino Mega
 * Layout:   7 independent strips, 291 LEDs each, wrapped in a circle
 *
 * Text scrolls right to left. Configure via Serial at 9600 baud.
 */

#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>

// ── User config ───────────────────────────────────────────────────────────────
#define NUM_ROWS                7
#define NUM_COLS                291
#define DEFAULT_BRIGHTNESS      40   // 0–255
#define DEFAULT_SCROLL_DELAY_MS 40   // ms per scroll step
#define DEFAULT_MESSAGE_GAP     12   // blank columns between repeats
#define MESSAGE_CAPACITY        96
#define MAX_SEQUENCE_MESSAGES   5
#define SETTINGS_MAGIC          0x5242443BUL

char     messageText[MESSAGE_CAPACITY] = "CHILDREN'S MUSEUM  ";
uint8_t  brightness      = DEFAULT_BRIGHTNESS;
uint16_t scrollDelayMs   = DEFAULT_SCROLL_DELAY_MS;
uint8_t  messageGap      = DEFAULT_MESSAGE_GAP;

#define DEFAULT_TEXT_R 255
#define DEFAULT_TEXT_G 80
#define DEFAULT_TEXT_B 0

uint8_t solidR = DEFAULT_TEXT_R;
uint8_t solidG = DEFAULT_TEXT_G;
uint8_t solidB = DEFAULT_TEXT_B;

// ── Sequence ──────────────────────────────────────────────────────────────────
char     seqMessages[MAX_SEQUENCE_MESSAGES][MESSAGE_CAPACITY];
uint8_t  seqColorMode[MAX_SEQUENCE_MESSAGES];
uint8_t  seqSolidR[MAX_SEQUENCE_MESSAGES];
uint8_t  seqSolidG[MAX_SEQUENCE_MESSAGES];
uint8_t  seqSolidB[MAX_SEQUENCE_MESSAGES];
uint8_t  seqRainbowFlow[MAX_SEQUENCE_MESSAGES];
uint8_t  seqLength      = 0;
uint8_t  seqEnabled     = 0;
uint8_t  seqIndex       = 0;      // currently-showing message (0-based)
uint16_t seqDurationSec = 10;     // seconds to display each message

enum DisplayState { STATE_SCROLL, STATE_WASH };
DisplayState  displayState  = STATE_SCROLL;
unsigned long stateStartMs  = 0;
int           washFront     = 0;  // leading column of rainbow wash (0..NUM_COLS)
uint16_t      washPhase     = 0;  // drives rainbow hue animation during wash

// ── Color modes ───────────────────────────────────────────────────────────────
enum ColorMode {
  COLOR_MODE_SOLID,
  COLOR_MODE_CYCLE,
  COLOR_MODE_RAINBOW,
  COLOR_MODE_FIRE,
  COLOR_MODE_STRIPES,
  COLOR_MODE_TWINKLE,
};

enum MenuAction {
  MENU_ACTION_NONE,
  MENU_ACTION_SPEED,
  MENU_ACTION_BRIGHTNESS,
  MENU_ACTION_MESSAGE,
  MENU_ACTION_SPACING,
  MENU_ACTION_COLOR,
  MENU_ACTION_SOLID_COLOR,
  MENU_ACTION_RAINBOW_STYLE,
  MENU_ACTION_SEQUENCE,
  MENU_ACTION_SEQ_SLOT,
  MENU_ACTION_SEQ_MESSAGE,
  MENU_ACTION_SEQ_COLOR,
  MENU_ACTION_SEQ_SOLID_COLOR,
  MENU_ACTION_SEQ_RAINBOW_STYLE,
  MENU_ACTION_SEQ_DURATION,
};

ColorMode  colorMode       = COLOR_MODE_SOLID;
uint16_t   colorPhase      = 0;
int        scrollOffset    = 0;
bool       serialMenuShown = false;
MenuAction pendingAction   = MENU_ACTION_NONE;
bool       rainbowFlow     = false;
uint8_t    pendingSlot     = 0;  // which seqMessages[] slot is being edited

// ── EEPROM layout ─────────────────────────────────────────────────────────────
struct SavedSettings {
  uint32_t magic;
  char     message[MESSAGE_CAPACITY];
  uint8_t  brightness;
  uint16_t scrollDelayMs;
  uint8_t  solidR;
  uint8_t  solidG;
  uint8_t  solidB;
  uint8_t  colorMode;
  uint8_t  rainbowFlow;
  uint8_t  messageGap;
  uint8_t  seqEnabled;
  uint8_t  seqLength;
  uint16_t seqDurationSec;
  char     seqMessages[MAX_SEQUENCE_MESSAGES][MESSAGE_CAPACITY];
  uint8_t  seqColorModes[MAX_SEQUENCE_MESSAGES];
  uint8_t  seqSolidR[MAX_SEQUENCE_MESSAGES];
  uint8_t  seqSolidG[MAX_SEQUENCE_MESSAGES];
  uint8_t  seqSolidB[MAX_SEQUENCE_MESSAGES];
  uint8_t  seqRainbowFlow[MAX_SEQUENCE_MESSAGES];
};

struct ColorPreset {
  const char* name;
  uint8_t red, green, blue;
};

const ColorPreset solidColorPresets[10] = {
  {"Red",     255,   0,   0},
  {"Green",     0, 255,   0},
  {"Blue",      0,   0, 255},
  {"Yellow",  255, 255,   0},
  {"Cyan",      0, 255, 255},
  {"Magenta", 255,   0, 255},
  {"Orange",  255,  80,   0},
  {"White",   255, 255, 255},
  {"Pink",    255,  40, 120},
  {"Purple",  140,   0, 255},
};

// ── Pin setup ─────────────────────────────────────────────────────────────────
// Row 0 (top) … Row 6 (bottom); pins 2–8
Adafruit_NeoPixel strips[NUM_ROWS] = {
  Adafruit_NeoPixel(NUM_COLS, 2, NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(NUM_COLS, 3, NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(NUM_COLS, 4, NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(NUM_COLS, 5, NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(NUM_COLS, 6, NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(NUM_COLS, 7, NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(NUM_COLS, 8, NEO_GRB + NEO_KHZ800),
};

// ── 5×8 font (ASCII 32–126) ───────────────────────────────────────────────────
// Each char = 5 bytes (one per column). Bit 0 = top row, bit 7 = bottom row.
static const uint8_t font5x8[][5] PROGMEM = {
  {0x00,0x00,0x00,0x00,0x00}, // 32 space
  {0x00,0x00,0x5F,0x00,0x00}, // 33 !
  {0x00,0x07,0x00,0x07,0x00}, // 34 "
  {0x14,0x7F,0x14,0x7F,0x14}, // 35 #
  {0x24,0x2A,0x7F,0x2A,0x12}, // 36 $
  {0x23,0x13,0x08,0x64,0x62}, // 37 %
  {0x36,0x49,0x55,0x22,0x50}, // 38 &
  {0x00,0x05,0x03,0x00,0x00}, // 39 '
  {0x00,0x1C,0x22,0x41,0x00}, // 40 (
  {0x00,0x41,0x22,0x1C,0x00}, // 41 )
  {0x14,0x08,0x3E,0x08,0x14}, // 42 *
  {0x08,0x08,0x3E,0x08,0x08}, // 43 +
  {0x00,0x50,0x30,0x00,0x00}, // 44 ,
  {0x08,0x08,0x08,0x08,0x08}, // 45 -
  {0x00,0x60,0x60,0x00,0x00}, // 46 .
  {0x20,0x10,0x08,0x04,0x02}, // 47 /
  {0x3E,0x51,0x49,0x45,0x3E}, // 48 0
  {0x00,0x42,0x7F,0x40,0x00}, // 49 1
  {0x42,0x61,0x51,0x49,0x46}, // 50 2
  {0x21,0x41,0x45,0x4B,0x31}, // 51 3
  {0x18,0x14,0x12,0x7F,0x10}, // 52 4
  {0x27,0x45,0x45,0x45,0x39}, // 53 5
  {0x3C,0x4A,0x49,0x49,0x30}, // 54 6
  {0x01,0x71,0x09,0x05,0x03}, // 55 7
  {0x36,0x49,0x49,0x49,0x36}, // 56 8
  {0x06,0x49,0x49,0x29,0x1E}, // 57 9
  {0x00,0x36,0x36,0x00,0x00}, // 58 :
  {0x00,0x56,0x36,0x00,0x00}, // 59 ;
  {0x08,0x14,0x22,0x41,0x00}, // 60 <
  {0x14,0x14,0x14,0x14,0x14}, // 61 =
  {0x00,0x41,0x22,0x14,0x08}, // 62 >
  {0x02,0x01,0x51,0x09,0x06}, // 63 ?
  {0x32,0x49,0x79,0x41,0x3E}, // 64 @
  {0x7E,0x11,0x11,0x11,0x7E}, // 65 A
  {0x7F,0x49,0x49,0x49,0x36}, // 66 B
  {0x3E,0x41,0x41,0x41,0x22}, // 67 C
  {0x7F,0x41,0x41,0x22,0x1C}, // 68 D
  {0x7F,0x49,0x49,0x49,0x41}, // 69 E
  {0x7F,0x09,0x09,0x09,0x01}, // 70 F
  {0x3E,0x41,0x49,0x49,0x7A}, // 71 G
  {0x7F,0x08,0x08,0x08,0x7F}, // 72 H
  {0x00,0x41,0x7F,0x41,0x00}, // 73 I
  {0x20,0x40,0x41,0x3F,0x01}, // 74 J
  {0x7F,0x08,0x14,0x22,0x41}, // 75 K
  {0x7F,0x40,0x40,0x40,0x40}, // 76 L
  {0x7F,0x02,0x04,0x02,0x7F}, // 77 M
  {0x7F,0x04,0x08,0x10,0x7F}, // 78 N
  {0x3E,0x41,0x41,0x41,0x3E}, // 79 O
  {0x7F,0x09,0x09,0x09,0x06}, // 80 P
  {0x3E,0x41,0x51,0x21,0x5E}, // 81 Q
  {0x7F,0x09,0x19,0x29,0x46}, // 82 R
  {0x46,0x49,0x49,0x49,0x31}, // 83 S
  {0x01,0x01,0x7F,0x01,0x01}, // 84 T
  {0x3F,0x40,0x40,0x40,0x3F}, // 85 U
  {0x1F,0x20,0x40,0x20,0x1F}, // 86 V
  {0x3F,0x40,0x38,0x40,0x3F}, // 87 W
  {0x63,0x14,0x08,0x14,0x63}, // 88 X
  {0x07,0x08,0x70,0x08,0x07}, // 89 Y
  {0x61,0x51,0x49,0x45,0x43}, // 90 Z
  {0x00,0x7F,0x41,0x41,0x00}, // 91 [
  {0x02,0x04,0x08,0x10,0x20}, // 92 backslash
  {0x00,0x41,0x41,0x7F,0x00}, // 93 ]
  {0x04,0x02,0x01,0x02,0x04}, // 94 ^
  {0x40,0x40,0x40,0x40,0x40}, // 95 _
  {0x00,0x01,0x02,0x04,0x00}, // 96 `
  {0x20,0x54,0x54,0x54,0x78}, // 97 a
  {0x7F,0x48,0x44,0x44,0x38}, // 98 b
  {0x38,0x44,0x44,0x44,0x20}, // 99 c
  {0x38,0x44,0x44,0x48,0x7F}, // 100 d
  {0x38,0x54,0x54,0x54,0x18}, // 101 e
  {0x08,0x7E,0x09,0x01,0x02}, // 102 f
  {0x0C,0x52,0x52,0x52,0x3E}, // 103 g
  {0x7F,0x08,0x04,0x04,0x78}, // 104 h
  {0x00,0x44,0x7D,0x40,0x00}, // 105 i
  {0x20,0x40,0x44,0x3D,0x00}, // 106 j
  {0x7F,0x10,0x28,0x44,0x00}, // 107 k
  {0x00,0x41,0x7F,0x40,0x00}, // 108 l
  {0x7C,0x04,0x18,0x04,0x78}, // 109 m
  {0x7C,0x08,0x04,0x04,0x78}, // 110 n
  {0x38,0x44,0x44,0x44,0x38}, // 111 o
  {0x7C,0x14,0x14,0x14,0x08}, // 112 p
  {0x08,0x14,0x14,0x18,0x7C}, // 113 q
  {0x7C,0x08,0x04,0x04,0x08}, // 114 r
  {0x48,0x54,0x54,0x54,0x20}, // 115 s
  {0x04,0x3F,0x44,0x40,0x20}, // 116 t
  {0x3C,0x40,0x40,0x20,0x7C}, // 117 u
  {0x1C,0x20,0x40,0x20,0x1C}, // 118 v
  {0x3C,0x40,0x30,0x40,0x3C}, // 119 w
  {0x44,0x28,0x10,0x28,0x44}, // 120 x
  {0x0C,0x50,0x50,0x50,0x3C}, // 121 y
  {0x44,0x64,0x54,0x4C,0x44}, // 122 z
  {0x00,0x08,0x36,0x41,0x00}, // 123 {
  {0x00,0x00,0x7F,0x00,0x00}, // 124 |
  {0x00,0x41,0x36,0x08,0x00}, // 125 }
  {0x10,0x08,0x08,0x10,0x08}, // 126 ~
};

#define CHAR_WIDTH   5
#define CHAR_SPACING 1

// ── Helpers ───────────────────────────────────────────────────────────────────

int messagePixelWidth() {
  int width = messageGap;
  int length = (int)strlen(messageText);
  for (int i = 0; i < length; i++) width += CHAR_WIDTH + CHAR_SPACING;
  return width;
}

const char* colorModeName() {
  switch (colorMode) {
    case COLOR_MODE_CYCLE:   return "cycle";
    case COLOR_MODE_RAINBOW: return rainbowFlow ? "rainbow flow" : "rainbow static";
    case COLOR_MODE_FIRE:    return "fire";
    case COLOR_MODE_STRIPES: return "stripes";
    case COLOR_MODE_TWINKLE: return "twinkle";
    default:                 return "solid";
  }
}

void saveSettings() {
  SavedSettings s;
  s.magic = SETTINGS_MAGIC;
  memcpy(s.message, messageText, MESSAGE_CAPACITY);
  s.message[MESSAGE_CAPACITY - 1] = '\0';
  s.brightness      = brightness;
  s.scrollDelayMs   = scrollDelayMs;
  s.solidR          = solidR;
  s.solidG          = solidG;
  s.solidB          = solidB;
  s.colorMode       = (uint8_t)colorMode;
  s.rainbowFlow     = rainbowFlow ? 1 : 0;
  s.messageGap      = messageGap;
  s.seqEnabled      = seqEnabled;
  s.seqLength       = seqLength;
  s.seqDurationSec  = seqDurationSec;
  for (int i = 0; i < MAX_SEQUENCE_MESSAGES; i++) {
    memcpy(s.seqMessages[i], seqMessages[i], MESSAGE_CAPACITY);
    s.seqMessages[i][MESSAGE_CAPACITY - 1] = '\0';
    s.seqColorModes[i]  = seqColorMode[i];
    s.seqSolidR[i]      = seqSolidR[i];
    s.seqSolidG[i]      = seqSolidG[i];
    s.seqSolidB[i]      = seqSolidB[i];
    s.seqRainbowFlow[i] = seqRainbowFlow[i];
  }
  EEPROM.put(0, s);
}

void loadSettings() {
  SavedSettings s;
  EEPROM.get(0, s);

  if (s.magic != SETTINGS_MAGIC) return;
  if (s.brightness > 255) return;
  if (s.scrollDelayMs < 1 || s.scrollDelayMs > 5000) return;
  if (s.colorMode > (uint8_t)COLOR_MODE_TWINKLE) return;
  if (s.rainbowFlow > 1) return;
  if (s.messageGap > 120) return;
  if (s.seqLength > MAX_SEQUENCE_MESSAGES) return;
  if (s.seqEnabled > 1) return;

  memcpy(messageText, s.message, MESSAGE_CAPACITY);
  messageText[MESSAGE_CAPACITY - 1] = '\0';
  if (messageText[0] == '\0') strcpy(messageText, " ");

  brightness      = s.brightness;
  scrollDelayMs   = s.scrollDelayMs;
  solidR          = s.solidR;
  solidG          = s.solidG;
  solidB          = s.solidB;
  colorMode       = (ColorMode)s.colorMode;
  rainbowFlow     = s.rainbowFlow == 1;
  messageGap      = s.messageGap;
  seqEnabled      = s.seqEnabled;
  seqLength       = s.seqLength;
  seqDurationSec  = s.seqDurationSec;
  for (int i = 0; i < MAX_SEQUENCE_MESSAGES; i++) {
    memcpy(seqMessages[i], s.seqMessages[i], MESSAGE_CAPACITY);
    seqMessages[i][MESSAGE_CAPACITY - 1] = '\0';
    seqColorMode[i]   = (s.seqColorModes[i] <= (uint8_t)COLOR_MODE_TWINKLE) ? s.seqColorModes[i] : COLOR_MODE_SOLID;
    seqSolidR[i]      = s.seqSolidR[i];
    seqSolidG[i]      = s.seqSolidG[i];
    seqSolidB[i]      = s.seqSolidB[i];
    seqRainbowFlow[i] = s.seqRainbowFlow[i] > 1 ? 0 : s.seqRainbowFlow[i];
  }
}

// ── Serial menu ───────────────────────────────────────────────────────────────

void printSequence() {
  Serial.print("sequence: ");
  Serial.println(seqEnabled ? "enabled" : "disabled");
  Serial.print("duration: ");
  Serial.print(seqDurationSec);
  Serial.println(" sec per message");
  Serial.print("slots: ");
  Serial.println(seqLength);
  for (int i = 0; i < MAX_SEQUENCE_MESSAGES; i++) {
    Serial.print("  slot ");
    Serial.print(i + 1);
    Serial.print(": ");
    if (i < seqLength && seqMessages[i][0] != '\0') {
      Serial.print(seqMessages[i]);
      Serial.print("  [");
      Serial.print(colorModeNameOf(seqColorMode[i], seqRainbowFlow[i]));
      Serial.println("]");
    } else {
      Serial.println("(empty)");
    }
  }
}

void printMenu() {
  Serial.println();
  Serial.println("================ Reader Board Menu ================");
  Serial.println("  1. Show this menu");
  Serial.println("  2. Show current settings");
  Serial.println("  3. Change scroll speed");
  Serial.println("  4. Change brightness");
  Serial.println("  5. Change message");
  Serial.println("  6. Change spacing");
  Serial.println("  7. Change color / mode");
  Serial.println("  8. Manage message sequence");
  Serial.println();
  Serial.println("Type the menu number and press Enter.");
  Serial.println("Type 'cancel' to back out of a prompt.");
  Serial.println("===================================================");
}

void printStatus() {
  Serial.println("Current settings:");
  Serial.print("message: "); Serial.println(messageText);
  Serial.print("speed: ");   Serial.print(scrollDelayMs); Serial.println(" ms");
  Serial.print("brightness: "); Serial.println(brightness);
  Serial.print("message gap: "); Serial.print(messageGap); Serial.println(" columns");
  Serial.print("mode: "); Serial.println(colorModeName());
  Serial.print("solid rgb: ");
  Serial.print(solidR); Serial.print(' ');
  Serial.print(solidG); Serial.print(' ');
  Serial.println(solidB);
  printSequence();
}

void printFullMenuScreen() {
  Serial.println();
  Serial.println("Reader board online");
  printStatus();
  printMenu();
}

void promptFor(MenuAction action) {
  pendingAction = action;
  switch (action) {
    case MENU_ACTION_SPEED:
      Serial.println(); Serial.println("Enter new speed in milliseconds (1-5000):"); break;
    case MENU_ACTION_BRIGHTNESS:
      Serial.println(); Serial.println("Enter brightness (0-255):"); break;
    case MENU_ACTION_MESSAGE:
      Serial.println(); Serial.println("Enter the new scrolling message:"); break;
    case MENU_ACTION_SPACING:
      Serial.println(); Serial.println("Enter blank spacing between message repeats (0-120 columns):"); break;
    case MENU_ACTION_COLOR:
      Serial.println();
      Serial.println("Color / mode menu:");
      Serial.println("  1. Solid color");
      Serial.println("  2. Cycle");
      Serial.println("  3. Rainbow");
      Serial.println("  4. Fire");
      Serial.println("  5. Stripes");
      Serial.println("  6. Twinkle");
      Serial.println("Enter 1-6:"); break;
    case MENU_ACTION_SOLID_COLOR:
      Serial.println();
      Serial.println("Solid color choices:");
      Serial.println("  1. Red    2. Green  3. Blue");
      Serial.println("  4. Yellow 5. Cyan   6. Magenta");
      Serial.println("  7. Orange 8. White  9. Pink  10. Purple");
      Serial.println("Enter 1-10:"); break;
    case MENU_ACTION_RAINBOW_STYLE:
      Serial.println();
      Serial.println("Rainbow style:  1. Static  2. Flow");
      Serial.println("Enter 1 or 2:"); break;
    case MENU_ACTION_SEQUENCE:
      Serial.println();
      Serial.println("Sequence menu:");
      Serial.println("  1. List slots");
      Serial.println("  2. Set a message slot");
      Serial.println("  3. Set display duration (seconds)");
      Serial.println("  4. Enable sequence");
      Serial.println("  5. Disable sequence");
      Serial.println("  6. Clear all slots");
      Serial.println("Enter 1-6:"); break;
    case MENU_ACTION_SEQ_SLOT:
      Serial.println();
      Serial.print("Enter slot number (1-"); Serial.print(MAX_SEQUENCE_MESSAGES); Serial.println("):"); break;
    case MENU_ACTION_SEQ_MESSAGE:
      Serial.println();
      Serial.print("Enter message for slot "); Serial.print(pendingSlot + 1); Serial.println(":"); break;
    case MENU_ACTION_SEQ_COLOR:
      Serial.println();
      Serial.print("Color for slot "); Serial.print(pendingSlot + 1); Serial.println(":");
      Serial.println("  1. Solid  2. Cycle  3. Rainbow  4. Fire  5. Stripes  6. Twinkle");
      Serial.println("Enter 1-6:"); break;
    case MENU_ACTION_SEQ_SOLID_COLOR:
      Serial.println();
      Serial.println("  1. Red    2. Green  3. Blue");
      Serial.println("  4. Yellow 5. Cyan   6. Magenta");
      Serial.println("  7. Orange 8. White  9. Pink  10. Purple");
      Serial.println("Enter 1-10:"); break;
    case MENU_ACTION_SEQ_RAINBOW_STYLE:
      Serial.println();
      Serial.println("Rainbow style:  1. Static  2. Flow");
      Serial.println("Enter 1 or 2:"); break;
    case MENU_ACTION_SEQ_DURATION:
      Serial.println();
      Serial.println("Enter display duration per message in seconds (1-3600):"); break;
    default:
      pendingAction = MENU_ACTION_NONE; break;
  }
}

void announceSerialIfReady() {
  if (!Serial || serialMenuShown) return;
  serialMenuShown = true;
  printFullMenuScreen();
}

void applyBrightness() {
  for (int r = 0; r < NUM_ROWS; r++) strips[r].setBrightness(brightness);
}

uint32_t activeColor(int row, int logicalCol) {
  if (colorMode == COLOR_MODE_SOLID)
    return strips[row].Color(solidR, solidG, solidB);

  if (colorMode == COLOR_MODE_FIRE) {
    uint32_t h = ((uint32_t)row * 73 ^ (uint32_t)logicalCol * 37) * 2654435761UL
                 ^ (uint32_t)(colorPhase >> 3) * 22695477UL;
    uint8_t flicker = (h >> 26) & 0x0F;
    uint8_t green = (uint8_t)((uint32_t)(NUM_ROWS - 1 - row) * 180 / (NUM_ROWS - 1));
    green = (green > flicker * 6) ? green - flicker * 6 : 0;
    return strips[row].Color(255, green, 0);
  }

  if (colorMode == COLOR_MODE_STRIPES) {
    uint8_t stripe = ((uint16_t)logicalCol + (colorPhase >> 9)) % 3;
    if (stripe == 0) return strips[row].Color(255,   0,   0);
    if (stripe == 1) return strips[row].Color(  0, 220,   0);
    return                  strips[row].Color(  0,  80, 255);
  }

  if (colorMode == COLOR_MODE_TWINKLE) {
    uint16_t hue = (uint32_t)logicalCol * 65535UL / NUM_COLS + colorPhase;
    uint32_t h = ((uint32_t)row * 97 ^ (uint32_t)logicalCol * 53) * 2246822519UL
                 ^ (uint32_t)(colorPhase >> 5) * 2654435761UL;
    uint8_t val = 170 + (uint8_t)(h >> 26);
    return strips[row].gamma32(strips[row].ColorHSV(hue, 255, val));
  }

  uint16_t hue = colorPhase;
  if (colorMode == COLOR_MODE_RAINBOW) {
    if (rainbowFlow) {
      hue = (uint32_t)row * 65535UL / NUM_ROWS - colorPhase;
    } else {
      hue = (uint32_t)logicalCol * 65535UL / NUM_COLS
          + (uint32_t)row * 65535UL / NUM_ROWS;
    }
  }
  return strips[row].gamma32(strips[row].ColorHSV(hue));
}

void setMessage(const String& text) {
  String t = text; t.trim();
  if (t.length() == 0) t = " ";
  t.toCharArray(messageText, MESSAGE_CAPACITY);
  scrollOffset = 0;
  saveSettings();
}

const char* colorModeNameOf(uint8_t mode, uint8_t flow) {
  switch ((ColorMode)mode) {
    case COLOR_MODE_CYCLE:   return "cycle";
    case COLOR_MODE_RAINBOW: return flow ? "rainbow flow" : "rainbow static";
    case COLOR_MODE_FIRE:    return "fire";
    case COLOR_MODE_STRIPES: return "stripes";
    case COLOR_MODE_TWINKLE: return "twinkle";
    default:                 return "solid";
  }
}

// Load a sequence slot into the active globals and reset scroll.
void loadSeqMessage(uint8_t index) {
  if (seqLength == 0) return;
  seqIndex = index % seqLength;
  strncpy(messageText, seqMessages[seqIndex], MESSAGE_CAPACITY);
  messageText[MESSAGE_CAPACITY - 1] = '\0';
  if (messageText[0] == '\0') strcpy(messageText, " ");
  colorMode   = (ColorMode)seqColorMode[seqIndex];
  solidR      = seqSolidR[seqIndex];
  solidG      = seqSolidG[seqIndex];
  solidB      = seqSolidB[seqIndex];
  rainbowFlow = seqRainbowFlow[seqIndex];
  colorPhase  = 0;
  scrollOffset = 0;
}

void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  if (line.equalsIgnoreCase("cancel")) {
    pendingAction = MENU_ACTION_NONE;
    Serial.println("Cancelled.");
    printFullMenuScreen();
    return;
  }

  if (pendingAction != MENU_ACTION_NONE) {

    if (pendingAction == MENU_ACTION_SPEED) {
      long v = line.toInt();
      if (v >= 1 && v <= 5000) {
        scrollDelayMs = (uint16_t)v; saveSettings(); pendingAction = MENU_ACTION_NONE;
        Serial.print("Speed set to "); Serial.print(scrollDelayMs); Serial.println(" ms");
        printFullMenuScreen();
      } else Serial.println("Speed must be 1-5000. Try again or type 'cancel'.");
      return;
    }

    if (pendingAction == MENU_ACTION_BRIGHTNESS) {
      long v = line.toInt();
      if (v >= 0 && v <= 255) {
        brightness = (uint8_t)v; applyBrightness(); saveSettings(); pendingAction = MENU_ACTION_NONE;
        Serial.print("Brightness set to "); Serial.println(brightness);
        printFullMenuScreen();
      } else Serial.println("Brightness must be 0-255. Try again or type 'cancel'.");
      return;
    }

    if (pendingAction == MENU_ACTION_MESSAGE) {
      setMessage(line); pendingAction = MENU_ACTION_NONE;
      Serial.print("Message set to: "); Serial.println(messageText);
      printFullMenuScreen();
      return;
    }

    if (pendingAction == MENU_ACTION_SPACING) {
      long v = line.toInt();
      if (v >= 0 && v <= 120) {
        messageGap = (uint8_t)v; saveSettings(); pendingAction = MENU_ACTION_NONE;
        Serial.print("Gap set to "); Serial.print(messageGap); Serial.println(" columns");
        printFullMenuScreen();
      } else Serial.println("Spacing must be 0-120. Try again or type 'cancel'.");
      return;
    }

    if (pendingAction == MENU_ACTION_COLOR) {
      if (line.equals("1")) { promptFor(MENU_ACTION_SOLID_COLOR); return; }
      if (line.equals("2")) { colorMode = COLOR_MODE_CYCLE;   saveSettings(); pendingAction = MENU_ACTION_NONE; Serial.println("Mode: cycle");   printFullMenuScreen(); return; }
      if (line.equals("3")) { promptFor(MENU_ACTION_RAINBOW_STYLE); return; }
      if (line.equals("4")) { colorMode = COLOR_MODE_FIRE;    saveSettings(); pendingAction = MENU_ACTION_NONE; Serial.println("Mode: fire");    printFullMenuScreen(); return; }
      if (line.equals("5")) { colorMode = COLOR_MODE_STRIPES; saveSettings(); pendingAction = MENU_ACTION_NONE; Serial.println("Mode: stripes"); printFullMenuScreen(); return; }
      if (line.equals("6")) { colorMode = COLOR_MODE_TWINKLE; saveSettings(); pendingAction = MENU_ACTION_NONE; Serial.println("Mode: twinkle"); printFullMenuScreen(); return; }
      Serial.println("Choose 1-6. Or type 'cancel'.");
      return;
    }

    if (pendingAction == MENU_ACTION_SOLID_COLOR) {
      long c = line.toInt();
      if (c >= 1 && c <= 10) {
        const ColorPreset& p = solidColorPresets[c - 1];
        solidR = p.red; solidG = p.green; solidB = p.blue;
        colorMode = COLOR_MODE_SOLID; saveSettings(); pendingAction = MENU_ACTION_NONE;
        Serial.print("Color set to "); Serial.println(p.name);
        printFullMenuScreen();
      } else Serial.println("Choose 1-10. Or type 'cancel'.");
      return;
    }

    if (pendingAction == MENU_ACTION_RAINBOW_STYLE) {
      if (line.equals("1") || line.equals("2")) {
        colorMode = COLOR_MODE_RAINBOW; rainbowFlow = line.equals("2");
        saveSettings(); pendingAction = MENU_ACTION_NONE;
        Serial.println(rainbowFlow ? "Rainbow: flow" : "Rainbow: static");
        printFullMenuScreen();
      } else Serial.println("Choose 1 or 2. Or type 'cancel'.");
      return;
    }

    if (pendingAction == MENU_ACTION_SEQUENCE) {
      if (line.equals("1")) {
        printSequence(); pendingAction = MENU_ACTION_NONE;
        return;
      }
      if (line.equals("2")) { promptFor(MENU_ACTION_SEQ_SLOT); return; }
      if (line.equals("3")) { promptFor(MENU_ACTION_SEQ_DURATION); return; }
      if (line.equals("4")) {
        seqEnabled = 1; saveSettings(); pendingAction = MENU_ACTION_NONE;
        if (seqLength > 0) { loadSeqMessage(0); stateStartMs = millis(); displayState = STATE_SCROLL; }
        Serial.println("Sequence enabled.");
        printFullMenuScreen(); return;
      }
      if (line.equals("5")) {
        seqEnabled = 0; saveSettings(); pendingAction = MENU_ACTION_NONE;
        Serial.println("Sequence disabled.");
        printFullMenuScreen(); return;
      }
      if (line.equals("6")) {
        for (int i = 0; i < MAX_SEQUENCE_MESSAGES; i++) {
          seqMessages[i][0]  = '\0';
          seqColorMode[i]    = COLOR_MODE_SOLID;
          seqSolidR[i]       = DEFAULT_TEXT_R;
          seqSolidG[i]       = DEFAULT_TEXT_G;
          seqSolidB[i]       = DEFAULT_TEXT_B;
          seqRainbowFlow[i]  = 0;
        }
        seqLength = 0; seqEnabled = 0; saveSettings(); pendingAction = MENU_ACTION_NONE;
        Serial.println("Sequence cleared.");
        printFullMenuScreen(); return;
      }
      Serial.println("Choose 1-6. Or type 'cancel'.");
      return;
    }

    if (pendingAction == MENU_ACTION_SEQ_SLOT) {
      long v = line.toInt();
      if (v >= 1 && v <= MAX_SEQUENCE_MESSAGES) {
        pendingSlot = (uint8_t)(v - 1);
        promptFor(MENU_ACTION_SEQ_MESSAGE);
      } else {
        Serial.print("Choose 1-"); Serial.print(MAX_SEQUENCE_MESSAGES); Serial.println(". Or type 'cancel'.");
      }
      return;
    }

    if (pendingAction == MENU_ACTION_SEQ_MESSAGE) {
      String t = line; t.trim();
      if (t.length() == 0) t = " ";
      t.toCharArray(seqMessages[pendingSlot], MESSAGE_CAPACITY);
      seqMessages[pendingSlot][MESSAGE_CAPACITY - 1] = '\0';
      if (pendingSlot >= seqLength) seqLength = pendingSlot + 1;
      Serial.print("Slot "); Serial.print(pendingSlot + 1); Serial.print(" message: ");
      Serial.println(seqMessages[pendingSlot]);
      promptFor(MENU_ACTION_SEQ_COLOR);  // immediately ask for color
      return;
    }

    if (pendingAction == MENU_ACTION_SEQ_COLOR) {
      if (line.equals("1")) {
        seqColorMode[pendingSlot] = COLOR_MODE_SOLID;
        promptFor(MENU_ACTION_SEQ_SOLID_COLOR); return;
      }
      if (line.equals("2")) { seqColorMode[pendingSlot] = COLOR_MODE_CYCLE;   seqRainbowFlow[pendingSlot] = 0; goto seqColorDone; }
      if (line.equals("3")) { seqColorMode[pendingSlot] = COLOR_MODE_RAINBOW; promptFor(MENU_ACTION_SEQ_RAINBOW_STYLE); return; }
      if (line.equals("4")) { seqColorMode[pendingSlot] = COLOR_MODE_FIRE;    seqRainbowFlow[pendingSlot] = 0; goto seqColorDone; }
      if (line.equals("5")) { seqColorMode[pendingSlot] = COLOR_MODE_STRIPES; seqRainbowFlow[pendingSlot] = 0; goto seqColorDone; }
      if (line.equals("6")) { seqColorMode[pendingSlot] = COLOR_MODE_TWINKLE; seqRainbowFlow[pendingSlot] = 0; goto seqColorDone; }
      Serial.println("Choose 1-6. Or type 'cancel'."); return;
      seqColorDone:
        saveSettings(); pendingAction = MENU_ACTION_NONE;
        Serial.print("Slot "); Serial.print(pendingSlot + 1); Serial.print(" color: ");
        Serial.println(colorModeNameOf(seqColorMode[pendingSlot], seqRainbowFlow[pendingSlot]));
        printFullMenuScreen(); return;
    }

    if (pendingAction == MENU_ACTION_SEQ_SOLID_COLOR) {
      long c = line.toInt();
      if (c >= 1 && c <= 10) {
        const ColorPreset& p = solidColorPresets[c - 1];
        seqSolidR[pendingSlot] = p.red;
        seqSolidG[pendingSlot] = p.green;
        seqSolidB[pendingSlot] = p.blue;
        saveSettings(); pendingAction = MENU_ACTION_NONE;
        Serial.print("Slot "); Serial.print(pendingSlot + 1); Serial.print(" color: ");
        Serial.println(p.name);
        printFullMenuScreen();
      } else Serial.println("Choose 1-10. Or type 'cancel'.");
      return;
    }

    if (pendingAction == MENU_ACTION_SEQ_RAINBOW_STYLE) {
      if (line.equals("1") || line.equals("2")) {
        seqRainbowFlow[pendingSlot] = line.equals("2") ? 1 : 0;
        saveSettings(); pendingAction = MENU_ACTION_NONE;
        Serial.print("Slot "); Serial.print(pendingSlot + 1); Serial.print(" color: ");
        Serial.println(colorModeNameOf(seqColorMode[pendingSlot], seqRainbowFlow[pendingSlot]));
        printFullMenuScreen();
      } else Serial.println("Choose 1 or 2. Or type 'cancel'.");
      return;
    }

    if (pendingAction == MENU_ACTION_SEQ_DURATION) {
      long v = line.toInt();
      if (v >= 1 && v <= 3600) {
        seqDurationSec = (uint16_t)v; saveSettings(); pendingAction = MENU_ACTION_NONE;
        Serial.print("Duration set to "); Serial.print(seqDurationSec); Serial.println(" sec");
        printFullMenuScreen();
      } else Serial.println("Enter 1-3600. Or type 'cancel'.");
      return;
    }
  }

  if (line.equalsIgnoreCase("help") || line.equals("?") || line.equals("1")) { printFullMenuScreen(); return; }
  if (line.equalsIgnoreCase("status") || line.equals("2"))                    { printFullMenuScreen(); return; }
  if (line.equals("3")) { promptFor(MENU_ACTION_SPEED);      return; }
  if (line.equals("4")) { promptFor(MENU_ACTION_BRIGHTNESS); return; }
  if (line.equals("5")) { promptFor(MENU_ACTION_MESSAGE);    return; }
  if (line.equals("6")) { promptFor(MENU_ACTION_SPACING);    return; }
  if (line.equals("7")) { promptFor(MENU_ACTION_COLOR);      return; }
  if (line.equals("8")) { promptFor(MENU_ACTION_SEQUENCE);   return; }

  Serial.println("Choose 1-8.");
  printFullMenuScreen();
}

// Returns true if the pixel at (msgCol, row) in the scrolling message is lit.
bool msgPixel(int msgCol, int row) {
  if (row < 0 || row >= NUM_ROWS) return false;
  int total = messagePixelWidth();
  msgCol = ((msgCol % total) + total) % total;

  int textLength = (int)strlen(messageText);
  int scanCol = 0;
  for (int i = 0; i < textLength; i++) {
    char c = messageText[i];
    int span = CHAR_WIDTH + CHAR_SPACING;
    if (msgCol < scanCol + span) {
      int colInChar = msgCol - scanCol;
      if (colInChar >= CHAR_WIDTH) return false;
      if (c < 32 || c > 126) c = 32;
      uint8_t colBits = pgm_read_byte(&font5x8[c - 32][colInChar]);
      return ((colBits >> row) & 1) != 0;
    }
    scanCol += span;
  }
  return false;
}

// Write one pixel, applying the physical column flip for mirrored wiring.
void writeCol(int row, int logicalCol, bool on) {
  if (logicalCol < 0 || logicalCol >= NUM_COLS) return;
  uint32_t color = on ? activeColor(row, logicalCol) : 0;
  strips[row].setPixelColor(NUM_COLS - 1 - logicalCol, color);
}

// ── Arduino entry points ──────────────────────────────────────────────────────

void setup() {
  Serial.begin(9600);
  Serial.setTimeout(10);
  loadSettings();

  unsigned long start = millis();
  while (!Serial && millis() - start < 2000) {}

  for (int r = 0; r < NUM_ROWS; r++) { strips[r].begin(); strips[r].show(); }
  applyBrightness();

  if (seqEnabled && seqLength > 0) {
    loadSeqMessage(0);
    stateStartMs = millis();
    displayState = STATE_SCROLL;
  }

  announceSerialIfReady();
}

void loop() {
  announceSerialIfReady();
  handleSerial();

  for (int r = 0; r < NUM_ROWS; r++) strips[r].clear();

  // ── Sequence state machine ─────────────────────────────────────────────────
  if (seqEnabled && seqLength > 0) {
    unsigned long now = millis();

    if (displayState == STATE_SCROLL) {
      // Render scrolling text normally.
      for (int col = 0; col < NUM_COLS; col++)
        for (int row = 0; row < NUM_ROWS; row++)
          writeCol(row, col, msgPixel(col + scrollOffset, row));

      // Time to transition?
      if (now - stateStartMs >= (unsigned long)seqDurationSec * 1000UL) {
        displayState = STATE_WASH;
        washFront    = NUM_COLS;   // start on viewer's right, sweep left
        washPhase    = 0;
        // scrollOffset stays frozen so the rainbow erases static text
      }

    } else {  // STATE_WASH
      // Rainbow sweeps right-to-left (same direction as text).
      for (int col = 0; col < NUM_COLS; col++) {
        for (int row = 0; row < NUM_ROWS; row++) {
          if (col >= washFront) {
            // Washed — full rainbow, no text.
            uint16_t hue = (uint32_t)col * 65535UL / NUM_COLS + washPhase;
            uint32_t color = strips[row].gamma32(strips[row].ColorHSV(hue));
            strips[row].setPixelColor(NUM_COLS - 1 - col, color);
          } else {
            // Not yet washed — show frozen text.
            writeCol(row, col, msgPixel(col + scrollOffset, row));
          }
        }
      }

      washFront -= 6;    // ~2 seconds to sweep 291 columns at 40 ms/frame
      washPhase += 1351; // match hue flow to front speed (6 * 65535 / NUM_COLS)

      if (washFront <= 0) {
        // Wash complete — load next message and resume scrolling.
        loadSeqMessage((seqIndex + 1) % seqLength);
        displayState = STATE_SCROLL;
        stateStartMs = millis();
      }
    }

  } else {
    // Normal single-message mode.
    for (int col = 0; col < NUM_COLS; col++)
      for (int row = 0; row < NUM_ROWS; row++)
        writeCol(row, col, msgPixel(col + scrollOffset, row));
  }

  for (int r = 0; r < NUM_ROWS; r++) strips[r].show();

  // Advance color phase (not during wash — the wash has its own washPhase).
  if (displayState != STATE_WASH) {
    if      (colorMode == COLOR_MODE_CYCLE)                       colorPhase += 512;
    else if (colorMode == COLOR_MODE_RAINBOW && rainbowFlow)      colorPhase += 256;
    else if (colorMode == COLOR_MODE_FIRE)                        colorPhase += 192;
    else if (colorMode == COLOR_MODE_STRIPES)                     colorPhase += 384;
    else if (colorMode == COLOR_MODE_TWINKLE)                     colorPhase += 256;
  }

  // Advance scroll (frozen during wash so the rainbow erases static letters).
  if (displayState == STATE_SCROLL) {
    scrollOffset = (scrollOffset + 1) % messagePixelWidth();
  }

  delay(scrollDelayMs);
}
