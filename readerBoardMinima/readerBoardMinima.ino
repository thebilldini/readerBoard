/*
 * NeoPixel Reader Board - Arduino Uno R4 Minima
 * Hardware: four 8x32 NeoPixel panels = 16 rows x 64 columns.
 *           Pin 2 feeds the top row of panels, pin 3 the bottom row. Each pin drives
 *           two panels chained left to right (left panel's DOUT -> right panel's DIN).
 * Wiring:   each string is column-major: starts top-left, runs down column 0, then
 *           column 1 runs bottom-to-top, and so on (serpentine), continuing across
 *           the chained panel (each panel starts at its own top-left pixel).
 *           Text is rendered with Adafruit GFX fonts into a 16-row bitmap and scrolled.
 *
 * Scrolls a message across the matrix. Configure via Serial at 9600 baud
 * (type 1 for the menu).
 */

#include <EEPROM.h>
#include <FastLED.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerifBold9pt7b.h>
#include <Fonts/FreeMono9pt7b.h>

// ── User config ───────────────────────────────────────────────────────────────
#define TOP_PIN        2
#define BOTTOM_PIN     3
#define NUM_COLS       64                   // 2 chained panels of 32
#define PANEL_ROWS     8                    // rows per panel
#define NUM_ROWS       (PANEL_ROWS * 2)     // total rows
#define PANEL_LEDS     (NUM_COLS * PANEL_ROWS)  // LEDs per data pin (512)

#define SCROLL_RIGHT   false   // true = text moves left to right (like the Mega board)
#define MAX_STRIP_WIDTH 2048   // widest rendered message, in pixels (2048x16 bits = 4 KB)
#define STRIP_GAP       6      // blank pixels between message repeats

#define DEFAULT_BRIGHTNESS      40  // 0-255
#define DEFAULT_SCROLL_DELAY_MS 100  // ms per scroll step
#define MESSAGE_CAPACITY        96

char     messageText[MESSAGE_CAPACITY] = "HAPPY BIRTHDAY JIMMY   ";
uint8_t  brightness    = DEFAULT_BRIGHTNESS;
uint16_t scrollDelayMs = DEFAULT_SCROLL_DELAY_MS;
uint8_t  colorIndex    = 6;  // Orange
uint8_t  fontIndex     = 1;  // Sans

CRGB topLeds[PANEL_LEDS];
CRGB bottomLeds[PANEL_LEDS];

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


// ── Fonts ─────────────────────────────────────────────────────────────────────
// gfxFont == nullptr means the built-in 5x7 font drawn at textSize.
struct FontChoice { const char* name; const GFXfont* gfxFont; uint8_t textSize; };
static const FontChoice fonts[] = {
  {"Pixel (built-in, 2x)", nullptr,             2},
  {"Sans",                 &FreeSans9pt7b,      1},
  {"Sans Bold",            &FreeSansBold9pt7b,  1},
  {"Serif",                &FreeSerif9pt7b,     1},
  {"Serif Bold",           &FreeSerifBold9pt7b, 1},
  {"Mono",                 &FreeMono9pt7b,      1},
};
#define NUM_FONTS (sizeof(fonts) / sizeof(fonts[0]))

// ── Custom characters ─────────────────────────────────────────────────────────
// Pictures drawn with custom_char_editor.py replace the character they are
// assigned to. The editor writes custom_chars.h; without it there are none.
// pix is width*16 values, column-major (top row first): 0 = off,
// CUSTOM_THEME = use the message color, otherwise a fixed 0xRRGGBB color.
#define CUSTOM_THEME 0x01000000UL
struct CustomChar { char code; uint8_t width; const uint32_t* pix; };
#if __has_include("custom_chars.h")
#include "custom_chars.h"
#else
static const CustomChar customChars[] = { { 0, 0, nullptr } };
#define NUM_CUSTOM_CHARS 0
#endif

const CustomChar* findCustomChar(char c) {
  for (uint8_t i = 0; i < NUM_CUSTOM_CHARS; i++) {
    if (customChars[i].code == c) return &customChars[i];
  }
  return nullptr;
}

// ── Message bitmap ────────────────────────────────────────────────────────────
GFXcanvas1 canvas(MAX_STRIP_WIDTH, NUM_ROWS);
int stripWidth   = 0;  // rendered message width plus gap
int scrollOffset = 0;

// Where custom pictures sit in the strip, so drawFrame() can color them.
struct Placement { int x; const CustomChar* ch; };
Placement placements[MESSAGE_CAPACITY];
uint8_t   placementCount = 0;
#define NO_PLACEMENT 255
uint8_t   colPlacement[MAX_STRIP_WIDTH];  // strip column -> index into placements[]

// Re-render messageText with the current font into the canvas.
void renderMessage() {
  const FontChoice& f = fonts[fontIndex];
  canvas.fillScreen(0);
  memset(colPlacement, NO_PLACEMENT, sizeof(colPlacement));
  placementCount = 0;
  canvas.setFont(f.gfxFont);
  canvas.setTextSize(f.textSize);
  canvas.setTextWrap(false);
  canvas.setTextColor(1);

  // Centre the actual message vertically (so all-caps text isn't pushed up to
  // leave room for descenders it doesn't have). Taller than the display: top-align.
  int16_t x1, y1;
  uint16_t w, h;
  char plain[MESSAGE_CAPACITY];  // message without custom characters, for measuring
  size_t n = 0;
  for (const char* c = messageText; *c; c++) {
    if (!findCustomChar(*c)) plain[n++] = *c;
  }
  plain[n] = '\0';
  canvas.getTextBounds(plain, 0, 0, &x1, &y1, &w, &h);
  int pad = ((int)NUM_ROWS - (int)h) / 2;
  if (pad < 0) pad = 0;
  int baseline = -y1 + pad;

  canvas.setCursor(0, baseline);
  for (const char* c = messageText; *c; c++) {
    const CustomChar* cc = findCustomChar(*c);
    if (!cc) { canvas.write(*c); continue; }
    int x = canvas.getCursorX();
    placements[placementCount] = { x, cc };
    for (uint8_t col = 0; col < cc->width; col++) {
      if (x + col < MAX_STRIP_WIDTH) colPlacement[x + col] = placementCount;
      for (int y = 0; y < NUM_ROWS; y++) {
        if (pgm_read_dword(&cc->pix[col * NUM_ROWS + y])) canvas.drawPixel(x + col, y, 1);
      }
    }
    placementCount++;
    canvas.setCursor(x + cc->width + 1, baseline);  // 1 blank column after the picture
  }
  stripWidth = canvas.getCursorX() + STRIP_GAP;
  if (stripWidth > MAX_STRIP_WIDTH) stripWidth = MAX_STRIP_WIDTH;
  scrollOffset = 0;
}

// ── Display helpers ───────────────────────────────────────────────────────────

// Serpentine, column-major within one panel: even columns run top->bottom,
// odd columns bottom->top.
uint16_t pixelIndex(uint8_t x, uint8_t y) {
  return (uint16_t)x * PANEL_ROWS + ((x & 1) ? (PANEL_ROWS - 1 - y) : y);
}

void drawFrame() {
  if (stripWidth <= STRIP_GAP) { FastLED.clear(true); return; }
  CRGB on(colors[colorIndex].r, colors[colorIndex].g, colors[colorIndex].b);
  for (int x = 0; x < NUM_COLS; x++) {
    int src = SCROLL_RIGHT ? (x - scrollOffset) : (x + scrollOffset);
    src = ((src % stripWidth) + stripWidth) % stripWidth;
    uint8_t pl = colPlacement[src];
    for (int y = 0; y < NUM_ROWS; y++) {
      CRGB color = CRGB::Black;
      if (canvas.getPixel(src, y)) {
        color = on;
        if (pl != NO_PLACEMENT) {  // custom picture: its own color unless it follows the message color
          uint32_t v = pgm_read_dword(&placements[pl].ch->pix[(src - placements[pl].x) * NUM_ROWS + y]);
          if (v != CUSTOM_THEME) color = CRGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
        }
      }
      CRGB* panel = (y < PANEL_ROWS) ? topLeds : bottomLeds;
      panel[pixelIndex(x, y % PANEL_ROWS)] = color;
    }
  }
  FastLED.show();
}

// ── Saved settings ────────────────────────────────────────────────────────────
// Stored in the Minima's EEPROM so serial-menu changes survive power-off.
// Bump SETTINGS_MAGIC if this struct ever changes, so old data is ignored.
#define SETTINGS_MAGIC 0x52424D31UL  // "RBM1"
struct Settings {
  uint32_t magic;
  char     message[MESSAGE_CAPACITY];
  uint16_t scrollDelayMs;
  uint8_t  brightness;
  uint8_t  colorIndex;
  uint8_t  fontIndex;
};

void saveSettings() {
  Settings s;
  s.magic = SETTINGS_MAGIC;
  memcpy(s.message, messageText, MESSAGE_CAPACITY);
  s.scrollDelayMs = scrollDelayMs;
  s.brightness    = brightness;
  s.colorIndex    = colorIndex;
  s.fontIndex     = fontIndex;
  EEPROM.put(0, s);
}

// Loads saved settings if present and sane; otherwise keeps the built-in defaults.
void loadSettings() {
  Settings s;
  EEPROM.get(0, s);
  if (s.magic != SETTINGS_MAGIC) return;
  s.message[MESSAGE_CAPACITY - 1] = '\0';
  if (s.message[0] == '\0' || s.scrollDelayMs < 1 || s.scrollDelayMs > 5000 ||
      s.colorIndex >= NUM_COLORS || s.fontIndex >= NUM_FONTS) return;
  memcpy(messageText, s.message, MESSAGE_CAPACITY);
  scrollDelayMs = s.scrollDelayMs;
  brightness    = s.brightness;
  colorIndex    = s.colorIndex;
  fontIndex     = s.fontIndex;
}

// ── Serial menu ───────────────────────────────────────────────────────────────
enum MenuAction { MENU_NONE, MENU_SPEED, MENU_BRIGHTNESS, MENU_MESSAGE, MENU_COLOR, MENU_FONT };
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
  Serial.println("  7. Change font");
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
  Serial.print("font: ");       Serial.println(fonts[fontIndex].name);
}

void handleLine(char* line) {
  if (pending != MENU_MESSAGE) while (*line == ' ') line++;  // message text keeps its spaces
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
      case 7: pending = MENU_FONT;
              Serial.println("Font choices:");
              for (uint8_t i = 0; i < NUM_FONTS; i++) {
                Serial.print("  "); Serial.print(i + 1); Serial.print(". "); Serial.println(fonts[i].name);
              }
              Serial.print("Enter 1-"); Serial.print(NUM_FONTS); Serial.println(":"); break;
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
      FastLED.setBrightness(brightness);
      Serial.print("Brightness set to "); Serial.println(brightness);
      break;
    case MENU_MESSAGE:
      if (line[0] == '\0') { Serial.println("Message cannot be empty."); return; }
      strncpy(messageText, line, MESSAGE_CAPACITY - 1);
      messageText[MESSAGE_CAPACITY - 1] = '\0';
      renderMessage();
      Serial.print("Message set to: "); Serial.println(messageText);
      break;
    case MENU_COLOR:
      if (n < 1 || n > (long)NUM_COLORS) { Serial.println("Invalid color number."); return; }
      colorIndex = (uint8_t)(n - 1);
      Serial.print("Color set to "); Serial.println(colors[colorIndex].name);
      break;
    case MENU_FONT:
      if (n < 1 || n > (long)NUM_FONTS) { Serial.println("Invalid font number."); return; }
      fontIndex = (uint8_t)(n - 1);
      renderMessage();
      Serial.print("Font set to "); Serial.println(fonts[fontIndex].name);
      break;
    default: break;
  }
  saveSettings();  // only reached after a successful change
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
  loadSettings();
  FastLED.addLeds<WS2812B, TOP_PIN,    GRB>(topLeds,    PANEL_LEDS);
  FastLED.addLeds<WS2812B, BOTTOM_PIN, GRB>(bottomLeds, PANEL_LEDS);
  FastLED.setBrightness(brightness);
  FastLED.clear(true);
  renderMessage();
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
    if (stripWidth > 0) scrollOffset = (scrollOffset + 1) % stripWidth;
  }
}
