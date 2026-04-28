/*
  Sensor Debug Sketch - Updated for Project Specs

  Tests each module separately:
  1) OLED display
  2) BME280 temperature/humidity/pressure over I2C
  3) Waveshare/Sharp infrared dust sensor analog output

  Board: ESP32
  OLED I2C: SDA = GPIO 21, SCL = GPIO 22
  BME280 I2C: SDA = GPIO 21, SCL = GPIO 22
  Dust analog output after voltage divider: GPIO 34
  Dust LED control: GPIO 32

  IMPORTANT:
  - ESP32 ADC input must stay at or below 3.3V.
  - If dust sensor AOUT can reach 5V, use the voltage divider before GPIO34.
  - If using an NMOS low-side switch for ILED:
      Sensor ILED/control node -> NMOS drain
      NMOS source -> GND
      ESP32 GPIO32 -> NMOS gate
      Pull-up/resistor path -> 5V as required by the sensor/module
    In that case, set DUST_LED_ACTIVE_LOW to false.
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

// ---------- Pins ----------
#define I2C_SDA 21
#define I2C_SCL 22

#define DUST_AOUT 34
#define DUST_ILED 32

// If ILED is directly controlled like the original Sharp timing circuit,
// LOW usually turns the LED on.
// If GPIO32 drives an NMOS gate, HIGH usually turns the LED on.
const bool DUST_LED_ACTIVE_LOW = false;  // false = NMOS gate control, true = direct active-low ILED

// ---------- OLED ----------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ---------- BME280 ----------
Adafruit_BME280 bme;
bool bmeOK = false;
bool oledOK = false;

// ---------- Dust calibration ----------
const float ADC_REF_VOLTAGE = 3.3;
const float ADC_MAX = 4095.0;

// This is the voltage seen by ESP32 after the voltage divider.
// Your divider ratio with 1.8k + 3.3k depends on resistor placement.
// If 1.8k is on top from sensor AOUT to ADC and 3.3k is bottom from ADC to GND:
// Vadc = Vsensor * 3.3 / (1.8 + 3.3), so Vsensor = Vadc * 1.545.
const float VOLTAGE_DIVIDER_MULTIPLIER = 1.545; // set to 1.0 if no divider is installed

// Dust formulas vary by exact sensor/module.
// This preserves your original conversion, but applies it to estimated sensor output voltage.
float convertVoltageToDust(float sensorVoltage) {
  float dustDensity = 170.0 * sensorVoltage - 0.1;
  if (dustDensity < 0) dustDensity = 0;
  return dustDensity;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==================================");
  Serial.println("ESP32 SENSOR DEBUG STARTING");
  Serial.println("==================================");

  pinMode(DUST_ILED, OUTPUT);
  dustLedOff();

  analogReadResolution(12);
  analogSetPinAttenuation(DUST_AOUT, ADC_11db);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  scanI2CBus();
  testOLED();
  testBME280();

  Serial.println();
  Serial.println("Starting live readings...");
  Serial.println("Open Serial Monitor at 115200 baud.");
  Serial.println();
}

void loop() {
  float tempC = NAN;
  float humidity = NAN;
  float pressure_hPa = NAN;

  if (bmeOK) {
    tempC = bme.readTemperature();
    humidity = bme.readHumidity();
    pressure_hPa = bme.readPressure() / 100.0F;
  }

  int dustRaw = readDustRaw();
  float adcVoltage = dustRaw * (ADC_REF_VOLTAGE / ADC_MAX);
  float estimatedSensorVoltage = adcVoltage * VOLTAGE_DIVIDER_MULTIPLIER;
  float dustDensity = convertVoltageToDust(estimatedSensorVoltage);

  printSerialReadings(tempC, humidity, pressure_hPa, dustRaw, adcVoltage, estimatedSensorVoltage, dustDensity);
  updateDebugDisplay(tempC, humidity, pressure_hPa, dustRaw, adcVoltage, estimatedSensorVoltage, dustDensity);

  delay(1000);
}

void dustLedOn() {
  digitalWrite(DUST_ILED, DUST_LED_ACTIVE_LOW ? LOW : HIGH);
}

void dustLedOff() {
  digitalWrite(DUST_ILED, DUST_LED_ACTIVE_LOW ? HIGH : LOW);
}

void scanI2CBus() {
  Serial.println();
  Serial.println("I2C scan:");

  byte count = 0;
  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("  Found I2C device at 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      count++;
    }
  }

  if (count == 0) {
    Serial.println("  No I2C devices found.");
    Serial.println("  Check SDA/SCL wiring, power, and ground.");
  }
}

void testOLED() {
  Serial.println();
  Serial.println("OLED test:");

  oledOK = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);

  if (!oledOK) {
    Serial.println("  OLED FAILED at 0x3C.");
    Serial.println("  Try changing OLED_ADDR to 0x3D if needed.");
    return;
  }

  Serial.println("  OLED OK.");

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("OLED OK");
  display.println("Sensor debug");
  display.println();
  display.setTextSize(2);
  display.println("TEST");
  display.display();

  delay(1500);
}

void testBME280() {
  Serial.println();
  Serial.println("BME280 test:");

  if (bme.begin(0x76)) {
    bmeOK = true;
    Serial.println("  BME280 OK at 0x76.");
  } else if (bme.begin(0x77)) {
    bmeOK = true;
    Serial.println("  BME280 OK at 0x77.");
  } else {
    bmeOK = false;
    Serial.println("  BME280 FAILED at 0x76 and 0x77.");
    Serial.println("  Check VIN/3V3, GND, SDA, SCL.");
  }
}

int readDustRaw() {
  // Standard dust sensor timing:
  // Turn IR LED on, wait 280 us, sample analog output, then turn LED off.
  dustLedOn();
  delayMicroseconds(280);

  int voMeasured = analogRead(DUST_AOUT);

  delayMicroseconds(40);
  dustLedOff();
  delayMicroseconds(9680);

  return voMeasured;
}

void printSerialReadings(float tempC, float humidity, float pressure_hPa,
                         int dustRaw, float adcVoltage, float estimatedSensorVoltage,
                         float dustDensity) {
  Serial.println("------ Readings ------");

  if (bmeOK) {
    Serial.print("BME Temp:        ");
    Serial.print(tempC);
    Serial.println(" C");

    Serial.print("BME Humidity:    ");
    Serial.print(humidity);
    Serial.println(" %");

    Serial.print("BME Pressure:    ");
    Serial.print(pressure_hPa);
    Serial.println(" hPa");
  } else {
    Serial.println("BME:             NOT DETECTED");
  }

  Serial.print("Dust raw ADC:    ");
  Serial.println(dustRaw);

  Serial.print("ESP32 ADC volts: ");
  Serial.print(adcVoltage, 3);
  Serial.println(" V");

  Serial.print("Est sensor volt: ");
  Serial.print(estimatedSensorVoltage, 3);
  Serial.println(" V");

  Serial.print("Dust density:    ");
  Serial.print(dustDensity, 2);
  Serial.println(" ug/m3-ish");

  if (dustRaw == 0) {
    Serial.println("Dust warning: raw ADC is 0. Check AOUT -> divider -> GPIO34, sensor power, and ILED control.");
  }

  if (dustRaw >= 4090) {
    Serial.println("Dust warning: ADC is maxed. Stop and verify voltage divider before continuing.");
  }

  Serial.println();
}

void updateDebugDisplay(float tempC, float humidity, float pressure_hPa,
                        int dustRaw, float adcVoltage, float estimatedSensorVoltage,
                        float dustDensity) {
  if (!oledOK) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("OLED:OK BME:");
  display.println(bmeOK ? "OK" : "FAIL");

  display.setCursor(0, 12);
  display.print("T:");
  if (bmeOK) display.print(tempC, 1);
  else display.print("--");
  display.print("C H:");
  if (bmeOK) display.print(humidity, 0);
  else display.print("--");
  display.println("%");

  display.setCursor(0, 24);
  display.print("P:");
  if (bmeOK) display.print(pressure_hPa, 0);
  else display.print("--");
  display.println("hPa");

  display.setCursor(0, 36);
  display.print("Dust raw:");
  display.println(dustRaw);

  display.setCursor(0, 48);
  display.print("ADC:");
  display.print(adcVoltage, 2);
  display.print("V S:");
  display.print(estimatedSensorVoltage, 2);
  display.print("V");

  display.setCursor(0, 58);
  display.print("PM:");
  display.print(dustDensity, 0);

  display.display();
}
