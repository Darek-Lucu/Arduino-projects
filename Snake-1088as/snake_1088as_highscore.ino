/*
  Snake on a bare 1088AS 8x8 LED matrix + analog joystick
  3 tries per round, scores shown as numbers, highscore saved in EEPROM.
  Board: Arduino Uno R3. No extra libraries needed.

  Matrix pin -> Arduino pin (pin 1 = bottom-left, numbering counter-clockwise):
     1->D2   2->D3   3->D4   4->D5   5->D6   6->D7   7->D8   8->D9
     9->D10 10->D11 11->D12 12->D13 13->A2  14->A3  15->A4  16->A5
  1k resistor in series with each COLUMN pin (matrix pins 3,4,6,10,11,13,15,16).
  Joystick: VRx -> A0, VRy -> A1, +5V -> 5V, GND -> GND (SW not used)

  How it works:
   - Push the stick to start each try. Dots on the start screen = tries left.
   - After each try you see your score as a number.
   - After the 3rd try you see the best score of the round (4 pixels in the
     bottom row), then the all-time highscore (dotted pixels in the bottom row).
     A new record makes the highscore blink.
   - To reset the highscore: hold the stick pushed while powering on.
*/

#include <EEPROM.h>

// ---------- Settings (copy the values that worked for you before) ----------
#define SELF_TEST       false
#define ROWS_ARE_ANODES false  // you needed false for your matrix
#define MIRROR_COLS     false
#define FLIP_ROWS       false
#define SWAP_AXES       false
#define INVERT_X        false
#define INVERT_Y        false

const uint8_t TRIES = 3;       // tries per round (1 to 4)
const int EEPROM_ADDR = 0;     // where the highscore is stored

const int JOY_THRESHOLD = 250;
const unsigned int START_INTERVAL = 400;  // ms per move at start
const unsigned int MIN_INTERVAL   = 120;
const unsigned int SPEED_STEP     = 15;

// ---------- Pin setup ----------
const uint8_t PIN_X = A0;
const uint8_t PIN_Y = A1;

const uint8_t pinMap[16]  = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, A2, A3, A4, A5};
const uint8_t ROW_PHYS[8] = {9, 14, 8, 12, 1, 7, 2, 5};
const uint8_t COL_PHYS[8] = {13, 3, 4, 10, 6, 11, 15, 16};

uint8_t rowPin[8];
uint8_t colPin[8];

const uint8_t ROW_ON  = ROWS_ARE_ANODES ? HIGH : LOW;
const uint8_t ROW_OFF = ROWS_ARE_ANODES ? LOW  : HIGH;
const uint8_t COL_ON  = ROWS_ARE_ANODES ? LOW  : HIGH;
const uint8_t COL_OFF = ROWS_ARE_ANODES ? HIGH : LOW;

uint8_t fb[8];  // frame buffer: one byte per row, bit c = column c

// ---------- Display driver (multiplexing) ----------
void refresh() {
  static uint8_t r = 0;
  for (uint8_t i = 0; i < 8; i++) digitalWrite(rowPin[i], ROW_OFF);
  for (uint8_t c = 0; c < 8; c++) {
    digitalWrite(colPin[c], ((fb[r] >> c) & 1) ? COL_ON : COL_OFF);
  }
  digitalWrite(rowPin[r], ROW_ON);
  r = (r + 1) & 7;
}

void showFor(unsigned long ms) {
  unsigned long t = millis();
  while (millis() - t < ms) refresh();
}

void clearFb() {
  for (uint8_t r = 0; r < 8; r++) fb[r] = 0;
}

void setPixel(uint8_t x, uint8_t y) {
  uint8_t col = MIRROR_COLS ? (7 - x) : x;
  uint8_t row = FLIP_ROWS ? (7 - y) : y;
  fb[row] |= (1 << col);
}

// ---------- Number display (3x5 font) ----------
const uint8_t FONT[10][5] = {
  {7, 5, 5, 5, 7},  // 0
  {2, 6, 2, 2, 7},  // 1
  {7, 1, 7, 4, 7},  // 2
  {7, 1, 7, 1, 7},  // 3
  {5, 5, 7, 1, 1},  // 4
  {7, 4, 7, 1, 7},  // 5
  {7, 4, 7, 5, 7},  // 6
  {7, 1, 1, 1, 1},  // 7
  {7, 5, 7, 5, 7},  // 8
  {7, 5, 7, 1, 7}   // 9
};

const uint8_t MARK_NONE = 0x00;
const uint8_t MARK_BEST = 0x3C;  // 4 solid pixels in the bottom row
const uint8_t MARK_HIGH = 0x55;  // 4 dotted pixels in the bottom row

void drawDigit(uint8_t d, uint8_t x0, uint8_t y0) {
  for (uint8_t r = 0; r < 5; r++) {
    for (uint8_t k = 0; k < 3; k++) {
      if ((FONT[d][r] >> (2 - k)) & 1) setPixel(x0 + k, y0 + r);
    }
  }
}

void drawNumber(uint8_t n, uint8_t marker) {
  clearFb();
  if (n >= 10) {
    drawDigit(n / 10, 0, 1);
    drawDigit(n % 10, 4, 1);
  } else {
    drawDigit(n, 3, 1);
  }
  for (uint8_t c = 0; c < 8; c++) {
    if ((marker >> c) & 1) setPixel(c, 7);
  }
}

void showNumber(uint8_t n, uint8_t marker, unsigned long ms) {
  drawNumber(n, marker);
  showFor(ms);
}

// ---------- Game state ----------
uint8_t sx[64], sy[64];  // snake segments, index 0 = head
uint8_t len;
int8_t dx, dy;
int8_t nextDx, nextDy;
uint8_t fx, fy;
unsigned int interval;
unsigned long lastMove, lastJoy;

uint8_t tryNum = 1;      // current try (1..TRIES)
uint8_t roundBest = 0;
uint8_t highScore = 0;

void render(bool showFood) {
  clearFb();
  for (uint8_t i = 0; i < len; i++) setPixel(sx[i], sy[i]);
  if (showFood) setPixel(fx, fy);
}

void placeFood() {
  while (true) {
    fx = random(8);
    fy = random(8);
    bool free = true;
    for (uint8_t i = 0; i < len; i++) {
      if (sx[i] == fx && sy[i] == fy) { free = false; break; }
    }
    if (free) return;
  }
}

void resetGame() {
  len = 3;
  sx[0] = 4; sy[0] = 4;
  sx[1] = 3; sy[1] = 4;
  sx[2] = 2; sy[2] = 4;
  dx = 1; dy = 0;
  nextDx = 1; nextDy = 0;
  interval = START_INTERVAL;
  placeFood();
  lastMove = millis();
}

// ---------- Joystick ----------
void readAxes(int &x, int &y) {
  x = analogRead(PIN_X);
  y = analogRead(PIN_Y);
  if (SWAP_AXES) { int t = x; x = y; y = t; }
  if (INVERT_X) x = 1023 - x;
  if (INVERT_Y) y = 1023 - y;
}

bool stickPushed() {
  int x, y;
  readAxes(x, y);
  return abs(x - 512) > JOY_THRESHOLD || abs(y - 512) > JOY_THRESHOLD;
}

void readJoystick() {
  int x, y;
  readAxes(x, y);
  int ax = abs(x - 512);
  int ay = abs(y - 512);
  if (ax < JOY_THRESHOLD && ay < JOY_THRESHOLD) return;

  int8_t ndx = 0, ndy = 0;
  if (ax > ay) ndx = (x > 512) ? 1 : -1;
  else         ndy = (y > 512) ? 1 : -1;

  if (ndx == -dx && ndy == -dy) return;  // no instant 180-degree turn
  nextDx = ndx;
  nextDy = ndy;
}

void waitForPush() {
  while (stickPushed()) refresh();
  while (!stickPushed()) refresh();
  randomSeed(micros());
  while (stickPushed()) refresh();
}

// ---------- Screens ----------
// Dots in the middle = tries left in this round
void showTriesScreen() {
  clearFb();
  uint8_t remaining = TRIES - tryNum + 1;
  for (uint8_t i = 0; i < remaining; i++) {
    setPixel(1 + 2 * i, 3);
    setPixel(1 + 2 * i, 4);
  }
}

void gameOver() {
  uint8_t score = len - 3;

  // Checkerboard flash (max 4 LEDs per row keeps pin current low)
  for (uint8_t i = 0; i < 3; i++) {
    for (uint8_t r = 0; r < 8; r++) fb[r] = (r & 1) ? 0xAA : 0x55;
    showFor(200);
    for (uint8_t r = 0; r < 8; r++) fb[r] = (r & 1) ? 0x55 : 0xAA;
    showFor(200);
  }

  showNumber(score, MARK_NONE, 2000);
  if (score > roundBest) roundBest = score;

  if (tryNum >= TRIES) {
    // End of round: best of the round, then the all-time highscore
    showNumber(roundBest, MARK_BEST, 2000);

    if (roundBest > highScore) {
      highScore = roundBest;
      EEPROM.update(EEPROM_ADDR, highScore);
      for (uint8_t i = 0; i < 4; i++) {          // new record: blink
        showNumber(highScore, MARK_HIGH, 300);
        clearFb();
        showFor(200);
      }
    }
    showNumber(highScore, MARK_HIGH, 2500);

    tryNum = 1;
    roundBest = 0;
  } else {
    tryNum++;
  }

  showTriesScreen();
  waitForPush();
}

// ---------- Game logic ----------
bool step() {
  dx = nextDx;
  dy = nextDy;
  int8_t nx = (int8_t)sx[0] + dx;
  int8_t ny = (int8_t)sy[0] + dy;

  if (nx < 0 || nx > 7 || ny < 0 || ny > 7) return false;  // wall

  bool eating = (nx == fx && ny == fy);
  uint8_t checkLen = eating ? len : len - 1;
  for (uint8_t i = 0; i < checkLen; i++) {
    if (sx[i] == nx && sy[i] == ny) return false;
  }

  if (eating) len++;
  for (int8_t i = len - 1; i > 0; i--) {
    sx[i] = sx[i - 1];
    sy[i] = sy[i - 1];
  }
  sx[0] = nx;
  sy[0] = ny;

  if (eating) {
    if (len >= 64) return false;  // board full
    placeFood();
    interval = (interval > MIN_INTERVAL + SPEED_STEP) ? interval - SPEED_STEP : MIN_INTERVAL;
  }
  return true;
}

// ---------- Arduino ----------
void setup() {
  for (uint8_t i = 0; i < 8; i++) {
    rowPin[i] = pinMap[ROW_PHYS[i] - 1];
    colPin[i] = pinMap[COL_PHYS[i] - 1];
    pinMode(rowPin[i], OUTPUT);
    pinMode(colPin[i], OUTPUT);
    digitalWrite(rowPin[i], ROW_OFF);
    digitalWrite(colPin[i], COL_OFF);
  }

  if (SELF_TEST) {
    while (true) {
      for (uint8_t r = 0; r < 8; r++) {
        for (uint8_t c = 0; c < 8; c++) {
          clearFb();
          fb[r] = (1 << c);
          showFor(150);
        }
      }
    }
  }

  // Load highscore (unwritten EEPROM reads 255, so treat that as 0)
  highScore = EEPROM.read(EEPROM_ADDR);
  if (highScore > 63) highScore = 0;

  // Stick held at power-on = reset highscore
  if (stickPushed()) {
    highScore = 0;
    EEPROM.update(EEPROM_ADDR, 0);
    showNumber(0, MARK_HIGH, 1500);
    while (stickPushed()) refresh();
  }

  showNumber(highScore, MARK_HIGH, 1500);  // show highscore at startup

  showTriesScreen();
  waitForPush();
  resetGame();
}

void loop() {
  unsigned long now = millis();

  if (now - lastJoy >= 10) {
    lastJoy = now;
    readJoystick();
  }

  if (now - lastMove >= interval) {
    lastMove = now;
    if (!step()) {
      gameOver();
      resetGame();
      return;
    }
  }

  render((now / 100) % 2 == 0);  // food blinks
  refresh();
}
