#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "time.h"

const char* ssid = "UCLA_WEB";
const char* webhookURL = "https://discord.com/api/webhooks/1498741485665783921/-iLJibwVzMf-0C3PvjFMZUWTNuRSSQgZKzAkXFxkFj8H5n-_UTyz5TDoen-X7v4F7qm4";

// --- Time Settings ---
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -28800;
const int   daylightOffset_sec = 3600;

// --- Pin Definitions ---
#define DUST_AOUT 34
#define DUST_ILED 32
#define BUTTON_PIN 33
#define BUZZER_PIN 25

// --- OLED & Sensor Objects ---
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Adafruit_BME280 bme;

// --- Thresholds & Timers ---
const float MODERATE_DUST_THRESHOLD = 100.0;
const float EXTREME_DUST_THRESHOLD = 200.0;

const time_t MODERATE_TIME_LIMIT_SEC = 45 * 60;
const time_t SOS_TIME_LIMIT_SEC = 5 * 60;
const time_t SNOOZE_DURATION_SEC = 15 * 60;
const uint64_t SLEEP_DURATION_US = 60ULL * 1000000ULL;

// --- RTC MEMORY VARIABLES ---
enum State { NORMAL, WARNING, SNOOZED, SOS };
RTC_DATA_ATTR State systemState = NORMAL;
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR time_t moderateStartEpoch = 0;
RTC_DATA_ATTR time_t warningStartEpoch = 0;
RTC_DATA_ATTR time_t snoozeStartEpoch = 0;

// --- Global Variables ---
float tempC = 0.0;
float humidity = 0.0;
float dustDensity = 0.0;
bool sosSent = false;

void setup() {
  Serial.begin(115200);

  pinMode(DUST_ILED, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(DUST_ILED, HIGH);

  Wire.begin(21, 22);
  Wire.setClock(100000);
  delay(500);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed");
    while (true);
  }

  delay(100);
  display.clearDisplay();
  display.display();

  if (!bme.begin(0x76)) {
    Serial.println("BME not found at 0x76, trying 0x77...");
    if (!bme.begin(0x77)) {
      Serial.println("BME failed");
      while (true);
    }
  }

  Serial.println("BME connected!");

  if (bootCount == 0) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 10);
    display.print("Syncing Time...");
    display.display();

    Serial.println("Connecting to WiFi...");

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid);

    int wifiAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && wifiAttempts < 30) {
      delay(500);
      Serial.print(".");
      wifiAttempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());

      configTime(gmtOffset_sec, daylightOffset_sec, "time.google.com");

      Serial.println("Waiting for NTP time...");

      struct tm timeinfo;
      int timeAttempts = 0;

      while (!getLocalTime(&timeinfo) && timeAttempts < 30) {
        Serial.print(".");
        delay(500);
        timeAttempts++;
      }

      if (timeAttempts >= 30) {
        Serial.println("\nTime sync failed");

        display.clearDisplay();
        display.setCursor(0, 10);
        display.print("Time sync failed");
        display.setCursor(0, 25);
        display.print("WiFi OK");
        display.display();

        delay(3000);
      } else {
        Serial.println("\nTime synced!");
      }

      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    } else {
      Serial.println("\nWiFi failed");

      display.clearDisplay();
      display.setCursor(0, 10);
      display.print("WiFi failed");
      display.display();

      delay(3000);
    }

    bootCount++;
  }

  readSensors();
  evaluateAirQuality();
  updateDisplay();

  if (systemState == NORMAL || systemState == SNOOZED) {
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN, 0);

    Serial.println("Going to Deep Sleep...");
    esp_deep_sleep_start();
  }
}

void loop() {
  time_t currentEpoch = time(nullptr);

  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50);
    if (digitalRead(BUTTON_PIN) == LOW) {
      systemState = SNOOZED;
      snoozeStartEpoch = currentEpoch;
      noTone(BUZZER_PIN);
      updateDisplay();

      esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
      esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN, 0);
      esp_deep_sleep_start();
    }
  }

  if (systemState == WARNING) {
    tone(BUZZER_PIN, 1000);
    delay(100);
    noTone(BUZZER_PIN);
    delay(100);

    if (currentEpoch - warningStartEpoch >= SOS_TIME_LIMIT_SEC) {
      systemState = SOS;
    }
  }

  else if (systemState == SOS) {
    tone(BUZZER_PIN, 2000);

    if (!sosSent) {
      sendSOSWebhook();
      sosSent = true;
    }

    delay(200);
  }
}

void readSensors() {
  tempC = bme.readTemperature();
  humidity = bme.readHumidity();

  digitalWrite(DUST_ILED, LOW);
  delayMicroseconds(280);

  int voMeasured = analogRead(DUST_AOUT);

  delayMicroseconds(40);
  digitalWrite(DUST_ILED, HIGH);
  delayMicroseconds(9680);

  float calcVoltage = voMeasured * (3.3 / 4095.0);
  dustDensity = 170 * calcVoltage - 0.1;

  if (dustDensity < 0) {
    dustDensity = 0.0;
  }
}

void evaluateAirQuality() {
  time_t currentEpoch = time(nullptr);

  if (systemState == SNOOZED) {
    if (currentEpoch - snoozeStartEpoch >= SNOOZE_DURATION_SEC) {
      systemState = NORMAL;
    } else {
      return;
    }
  }

  if (systemState == SOS) return;

  if (dustDensity >= EXTREME_DUST_THRESHOLD) {
    if (systemState != WARNING) {
      systemState = WARNING;
      warningStartEpoch = currentEpoch;
    }
  }

  else if (dustDensity >= MODERATE_DUST_THRESHOLD) {
    if (moderateStartEpoch == 0) {
      moderateStartEpoch = currentEpoch;
    }

    if (currentEpoch - moderateStartEpoch >= MODERATE_TIME_LIMIT_SEC) {
      if (systemState != WARNING) {
        systemState = WARNING;
        warningStartEpoch = currentEpoch;
      }
    }
  }

  else {
    moderateStartEpoch = 0;
    systemState = NORMAL;
  }
}

void sendSOSWebhook() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("SOS WiFi failed");
    return;
  }

  HTTPClient http;
  http.begin(webhookURL);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"content\": \"SOS: Dangerous air quality detected!\"}";
  int responseCode = http.POST(payload);

  Serial.print("Discord response code: ");
  Serial.println(responseCode);

  http.end();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void updateDisplay() {
  display.clearDisplay();

  if (systemState == WARNING) {
    display.setTextSize(2);
    display.setCursor(10, 20);
    display.print("WARNING!");

    display.setTextSize(1);
    display.setCursor(10, 45);
    display.print("High PM2.5 detected");
  }

  else if (systemState == SOS) {
    display.setTextSize(2);
    display.setCursor(40, 20);
    display.print("SOS");

    display.setTextSize(1);
    display.setCursor(5, 45);

    if (sosSent) {
      display.print("Emergency Notified!");
    } else {
      display.print("Triggering SOS...");
    }
  }

  else {
    struct tm timeinfo;

    display.setTextSize(2);
    display.setCursor(35, 0);

    if (getLocalTime(&timeinfo)) {
      if (timeinfo.tm_hour < 10) display.print("0");
      display.print(timeinfo.tm_hour);
      display.print(":");
      if (timeinfo.tm_min < 10) display.print("0");
      display.print(timeinfo.tm_min);
    } else {
      display.setTextSize(1);
      display.setCursor(20, 0);
      display.print("No Time");
    }

    display.setTextSize(1);

    display.setCursor(0, 25);
    display.print("Temp: ");
    display.print(tempC);
    display.print(" C");

    display.setCursor(0, 35);
    display.print("Hum:  ");
    display.print(humidity);
    display.print(" %");

    display.setCursor(0, 50);
    display.print("PM2.5: ");
    display.print(dustDensity);

    if (systemState == SNOOZED) {
      display.setCursor(100, 50);
      display.print("(Zzz)");
    }
  }

  display.display();
}