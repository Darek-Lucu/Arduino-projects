#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

const byte sensorPin = 2;

volatile unsigned long crossingCount = 0;
volatile unsigned long startTime = 0;
volatile unsigned long endTime = 0;
volatile bool measurementDone = false;
volatile bool measurementEnabled = false;

void crossingDetected() {

  if (!measurementEnabled) return;

  static unsigned long lastTrigger = 0;
  unsigned long now = micros();

  // Ignore noise (<50 ms)
  if (now - lastTrigger < 50000) return;
  lastTrigger = now;

  crossingCount++;

  // First crossing starts timing
  if (crossingCount == 1) {
    startTime = now;
  }

  // 20 crossings = 10 oscillations
  if (crossingCount == 20) {
    endTime = now;
    measurementDone = true;
  }
}

void setup() {

  pinMode(sensorPin, INPUT);

  lcd.init();
  lcd.backlight();

  attachInterrupt(
    digitalPinToInterrupt(sensorPin),
    crossingDetected,
    RISING      // Change to FALLING if your sensor works inverted
  );

  // 3-second countdown
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Position IR");

  for (int i = 3; i > 0; i--) {
    lcd.setCursor(0, 1);
    lcd.print("Start in ");
    lcd.print(i);
    lcd.print(" ");
    delay(1000);
  }

  measurementEnabled = true;

  lcd.clear();
  lcd.print("Waiting...");
}

void loop() {

  if (measurementDone) {

    noInterrupts();
    unsigned long totalTime = endTime - startTime;
    interrupts();

    float time10 = totalTime / 1000000.0;
    float period = time10 / 10.0;
    float frequency = 1.0 / period;

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("T=");
    lcd.print(period, 3);
    lcd.print("s");

    lcd.setCursor(0, 1);
    lcd.print("f=");
    lcd.print(frequency, 2);

    delay(10000);

    // Reset for a new measurement
    noInterrupts();
    crossingCount = 0;
    measurementDone = false;
    interrupts();

    lcd.clear();
    lcd.print("Ready Again");
  }
}