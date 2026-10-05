/*
 * NeoPixel Reader Board - Arduino Uno R4 Minima
 * Hardware: 8x32 NeoPixel matrix, data on pin 2.
 * Wiring:   one serial string, column-major: starts top-left, runs down column 0,
 *           then column 1 runs bottom-to-top, and so on (serpentine).
 *
 * Scrolls a message across the matrix. Configure via Serial at 9600 baud
 * (type 1 for the menu).
 */

#include <Adafruit_NeoPixel.h>

// ── User config ───────────────────────────────────────────────────────────────
#define LED_PIN        2
#define NUM_COLS       32
#define NUM_ROWS       8
#define NUM_LEDS       (NUM_COLS * NUM_ROWS)

#define SCROLL_RIGHT   true   // true = text moves left to right (like the Mega board)
#define MIRROR_LETTERS true   // true = each letter flipped left-to-right (like the Mega board)

#define DEFAULT_BRIGHTNESS      40   // 0-255
#define DEFAULT_SCROLL_DELAY_MS 100  // ms per scroll step
#define MESSAGE_CAPACITY        96

char     messageText[MESSAGE_CAPACITY] = "HAPPY BIRTHDAY JIMMY   ";
uint8_t  brightness    = DEFAULT_BRIGHTNESS;
uint16_t scrollDelayMs = DEFAULT_SCROLL_DELAY_MS;
uint8_t  colorIndex    = 6;  // Orange

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

struct NamedColor { const char* name; uint8_t r, g, b; };
static const NamedColor colors[] = {
  {"Red",     255,   0,   0},
  {"Green",     0, 255,   0},
  {"Blue",      0,   0, 255},
  {"Yellow",  255, 200,   0},
  {"Cyan",      0, 255, 255},
  {"Magenta", 255,   0, 255},
  {"Orange",  255,  80,   0},
  {"White",   255, 255, 255},
  {"Pink",    255,  40, 120},
  {"Purple",  140,   0, 255},
};
#define NUM_COLORS (sizeof(colors) / sizeof(colors[0]))

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

#define CHAR_WIDTH   5
#define CHAR_SPACING 1

// ── Display helpers ───────────────────────────────────────────────────────────

// Serpentine, column-major: even columns run top->bottom, odd columns bottom->top.
uint16_t pixelIndex(uint8_t x, uint8_t y) {
  return (uint16_t)x * NUM_ROWS + ((x & 1) ? (NUM_ROWS - 1 - y) : y);
}

// Column `col` (0-based) of the rendered message strip; bit 0 = top row.
uint8_t messageColumn(int col) {
  int cell = col / (CHAR_WIDTH + CHAR_SPACING);
  int inCell = col % (CHAR_WIDTH + CHAR_SPACING);
  if (inCell >= CHAR_WIDTH) return 0;  // gap between letters
  char c = messageText[cell];
  if (c < 32 || c > 126) c = 32;
  int fc = MIRROR_LETTERS ? (CHAR_WIDTH - 1 - inCell) : inCell;
  return pgm_read_byte(&font5x8[c - 32][fc]);
}

int messagePixelWidth() {
  return (int)strlen(messageText) * (CHAR_WIDTH + CHAR_SPACING);
}

int scrollOffset = 0;

void drawFrame() {
  int w = messagePixelWidth();
  if (w == 0) { strip.clear(); strip.show(); return; }
  uint32_t on = strip.Color(colors[colorIndex].r, colors[colorIndex].g, colors[colorIndex].b);
  for (int x = 0; x < NUM_COLS; x++) {
    int src = SCROLL_RIGHT ? (x - scrollOffset) : (x + scrollOffset);
    src = ((src % w) + w) % w;
    uint8_t bits = messageColumn(src);
    for (int y = 0; y < NUM_ROWS; y++) {
      strip.setPixelColor(pixelIndex(x, y), (bits >> y) & 1 ? on : 0);
    }
  }
  strip.show();
}

// ── Serial menu ───────────────────────────────────────────────────────────────
enum MenuAction { MENU_NONE, MENU_SPEED, MENU_BRIGHTNESS, MENU_MESSAGE, MENU_COLOR };
MenuAction pending = MENU_NONE;

char    lineBuf[MESSAGE_CAPACITY];
uint8_t lineLen = 0;

void printMenu() {
  Serial.println();
  Serial.println("========== Reader Board Menu ==========");
  Serial.println("  1. Show this menu");
  Serial.println("  2. Show current settings");
  Serial.println("  3. Change scroll speed");
  Serial.println("  4. Change brightness");
  Serial.println("  5. Change message");
  Serial.println("  6. Change color");
  Serial.println("Type the menu number and press Enter.");
  Serial.println("Type 'cancel' to back out of a prompt.");
  Serial.println("=======================================");
}

void printSettings() {
  Serial.println("Current settings:");
  Serial.print("message: ");    Serial.println(messageText);
  Serial.print("speed: ");      Serial.print(scrollDelayMs); Serial.println(" ms");
  Serial.print("brightness: "); Serial.println(brightness);
  Serial.print("color: ");      Serial.println(colors[colorIndex].name);
}

void handleLine(char* line) {
  while (*line == ' ') line++;
  if (strcasecmp(line, "cancel") == 0) {
    pending = MENU_NONE;
    Serial.println("Cancelled.");
    return;
  }

  if (pending == MENU_NONE) {
    switch (atoi(line)) {
      case 1: printMenu(); break;
      case 2: printSettings(); break;
      case 3: pending = MENU_SPEED;
              Serial.println("Enter new speed in milliseconds (1-5000):"); break;
      case 4: pending = MENU_BRIGHTNESS;
              Serial.println("Enter brightness (0-255):"); break;
      case 5: pending = MENU_MESSAGE;
              Serial.println("Enter the new scrolling message:"); break;
      case 6: pending = MENU_COLOR;
              Serial.println("Color choices:");
              for (uint8_t i = 0; i < NUM_COLORS; i++) {
                Serial.print("  "); Serial.print(i + 1); Serial.print(". "); Serial.println(colors[i].name);
              }
              Serial.print("Enter 1-"); Serial.print(NUM_COLORS); Serial.println(":"); break;
      default: Serial.println("Unknown option. Type 1 for the menu."); break;
    }
    return;
  }

  MenuAction action = pending;
  pending = MENU_NONE;
  long n = atol(line);
  switch (action) {
    case MENU_SPEED:
      if (n < 1 || n > 5000) { Serial.println("Speed must be 1-5000."); return; }
      scrollDelayMs = (uint16_t)n;
      Serial.print("Speed set to "); Serial.print(scrollDelayMs); Serial.println(" ms");
      break;
    case MENU_BRIGHTNESS:
      if (n < 0 || n > 255) { Serial.println("Brightness must be 0-255."); return; }
      brightness = (uint8_t)n;
      strip.setBrightness(brightness);
      Serial.print("Brightness set to "); Serial.println(brightness);
      break;
    case MENU_MESSAGE:
      if (line[0] == '\0') { Serial.println("Message cannot be empty."); return; }
      strncpy(messageText, line, MESSAGE_CAPACITY - 1);
      messageText[MESSAGE_CAPACITY - 1] = '\0';
      scrollOffset = 0;
      Serial.print("Message set to: "); Serial.println(messageText);
      break;
    case MENU_COLOR:
      if (n < 1 || n > (long)NUM_COLORS) { Serial.println("Invalid color number."); return; }
      colorIndex = (uint8_t)(n - 1);
      Serial.print("Color set to "); Serial.println(colors[colorIndex].name);
      break;
    default: break;
  }
}

void pollSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (lineLen > 0) {
        lineBuf[lineLen] = '\0';
        lineLen = 0;
        handleLine(lineBuf);
      }
    } else if (lineLen < MESSAGE_CAPACITY - 1) {
      lineBuf[lineLen++] = c;
    }
  }
}

// ── Arduino entry points ──────────────────────────────────────────────────────
unsigned long lastStepMs = 0;

void setup() {
  Serial.begin(9600);
  strip.begin();
  strip.setBrightness(brightness);
  strip.clear();
  strip.show();
  Serial.println();
  Serial.println("Reader board online");
  printMenu();
}

void loop() {
  pollSerial();
  unsigned long now = millis();
  if (now - lastStepMs >= scrollDelayMs) {
    lastStepMs = now;
    drawFrame();
    int w = messagePixelWidth();
    if (w > 0) scrollOffset = (scrollOffset + 1) % w;
  }
}
