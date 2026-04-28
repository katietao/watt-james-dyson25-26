/*
  State Simulator Sketch - Updated for Project Specs

  Press the button to cycle through every system state:
  NORMAL -> WARNING -> SNOOZED -> SOS -> NORMAL ...

  This is for testing:
  - OLED state screens
  - Button wiring with ESP32 internal pullup
  - Piezo buzzer behavior
  - State machine UI behavior

  It intentionally does NOT use Wi-Fi, NTP, deep sleep, or webhooks.
  Those belong in the final integrated firmware, not this state/UI test.

  Board: ESP32
  OLED I2C: SDA = GPIO 21, SCL = GPIO 22
  Button: GPIO 33 to GND, uses INPUT_PULLUP
  Buzzer: GPIO 25
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- Pins ----------
#define I2C_SDA 21
#define I2C_SCL 22

#define BUTTON_PIN 33
#define BUZZER_PIN 25

// ---------- OLED ----------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ---------- States ----------
enum State { NORMAL, WARNING, SNOOZED, SOS };
State systemState = NORMAL;

// ---------- Fake sensor values for screen testing ----------
float fakeTempC = 20.0;
float fakeHumidity = 45.0;
float fakeDustDensity = 12.0;

// ---------- Button debounce ----------
bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// ---------- Display refresh ----------
unsigned long lastDisplayUpdate = 0;
const unsigned long displayInterval = 250;

// ---------- Buzzer timing ----------
unsigned long lastBeepToggle = 0;
bool buzzerOn = false;

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED init failed. Try address 0x3D if needed.");
    while (true) {
      delay(100);
    }
  }

  Serial.println();
  Serial.println("==================================");
  Serial.println("STATE SIMULATOR STARTED");
  Serial.println("Press button to cycle states.");
  Serial.println("NORMAL -> WARNING -> SNOOZED -> SOS");
  Serial.println("==================================");

  updateFakeSensorValues();
  updateDisplay();
}

void loop() {
  handleButton();

  if (millis() - lastDisplayUpdate >= displayInterval) {
    lastDisplayUpdate = millis();
    updateFakeSensorValues();
    updateDisplay();
  }

  handleBuzzer();
}

void handleButton() {
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != stableButtonState) {
      stableButtonState = reading;

      // With INPUT_PULLUP, pressed = LOW because button connects GPIO33 to GND.
      if (stableButtonState == LOW) {
        nextState();
      }
    }
  }

  lastButtonReading = reading;
}

void nextState() {
  noTone(BUZZER_PIN);
  buzzerOn = false;

  if (systemState == NORMAL) {
    systemState = WARNING;
  } else if (systemState == WARNING) {
    systemState = SNOOZED;
  } else if (systemState == SNOOZED) {
    systemState = SOS;
  } else {
    systemState = NORMAL;
  }

  Serial.print("Switched to state: ");
  Serial.println(stateName(systemState));

  updateFakeSensorValues();
  updateDisplay();
}

const char* stateName(State s) {
  switch (s) {
    case NORMAL:  return "NORMAL";
    case WARNING: return "WARNING";
    case SNOOZED: return "SNOOZED";
    case SOS:     return "SOS";
    default:      return "UNKNOWN";
  }
}

void updateFakeSensorValues() {
  // Fake values only, chosen to match your real thresholds/state behavior.
  if (systemState == NORMAL) {
    fakeTempC = 20.0;
    fakeHumidity = 45.0;
    fakeDustDensity = 12.0;
  } else if (systemState == WARNING) {
    fakeTempC = 20.5;
    fakeHumidity = 48.0;
    fakeDustDensity = 145.0;  // moderate/high test value
  } else if (systemState == SNOOZED) {
    fakeTempC = 20.2;
    fakeHumidity = 46.0;
    fakeDustDensity = 135.0;  // still bad, but alarm paused
  } else if (systemState == SOS) {
    fakeTempC = 21.0;
    fakeHumidity = 50.0;
    fakeDustDensity = 260.0;  // extreme test value
  }
}

void handleBuzzer() {
  if (systemState == NORMAL || systemState == SNOOZED) {
    noTone(BUZZER_PIN);
    buzzerOn = false;
    return;
  }

  if (systemState == WARNING) {
    // Warning: repeating chirp, representing local alarm before escalation.
    if (millis() - lastBeepToggle >= 200) {
      lastBeepToggle = millis();
      buzzerOn = !buzzerOn;

      if (buzzerOn) tone(BUZZER_PIN, 1000);
      else noTone(BUZZER_PIN);
    }
  }

  if (systemState == SOS) {
    // SOS: continuous stronger alarm.
    // Final firmware would also turn Wi-Fi on here and send the webhook.
    tone(BUZZER_PIN, 2000);
    buzzerOn = true;
  }
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (systemState == NORMAL) {
    drawNormalScreen();
  } else if (systemState == WARNING) {
    drawWarningScreen();
  } else if (systemState == SNOOZED) {
    drawSnoozedScreen();
  } else if (systemState == SOS) {
    drawSOSScreen();
  }

  display.display();
}

void drawNormalScreen() {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("STATE TEST");

  display.setTextSize(2);
  display.setCursor(20, 12);
  display.println("NORMAL");

  display.setTextSize(1);
  display.setCursor(0, 35);
  display.print("Temp: ");
  display.print(fakeTempC, 1);
  display.println(" C");

  display.setCursor(0, 45);
  display.print("Hum:  ");
  display.print(fakeHumidity, 0);
  display.println(" %");

  display.setCursor(0, 55);
  display.print("PM2.5:");
  display.print(fakeDustDensity, 0);
}

void drawWarningScreen() {
  display.setTextSize(2);
  display.setCursor(10, 8);
  display.println("WARNING!");

  display.setTextSize(1);
  display.setCursor(5, 34);
  display.println("High PM2.5 detected");

  display.setCursor(5, 50);
  display.print("PM2.5: ");
  display.print(fakeDustDensity, 0);
}

void drawSnoozedScreen() {
  display.setTextSize(2);
  display.setCursor(16, 8);
  display.println("SNOOZED");

  display.setTextSize(1);
  display.setCursor(5, 34);
  display.println("Alarm paused for test");

  display.setCursor(5, 50);
  display.print("PM2.5: ");
  display.print(fakeDustDensity, 0);
  display.print("  Zzz");
}

void drawSOSScreen() {
  display.setTextSize(2);
  display.setCursor(40, 8);
  display.println("SOS");

  display.setTextSize(1);
  display.setCursor(5, 34);
  display.println("Wi-Fi/webhook would");
  display.setCursor(5, 44);
  display.println("trigger here");

  display.setCursor(5, 56);
  display.print("PM2.5: ");
  display.print(fakeDustDensity, 0);
}
