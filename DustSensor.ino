#include <Wire.h>

#include <Adafruit_GFX.h>

#include <Adafruit_SSD1306.h>



// OLED Configuration

#define SCREEN_WIDTH 128

#define SCREEN_HEIGHT 64

#define OLED_RESET    -1 

// Address is usually 0x3C for these 0.96" displays

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);



// Dust Sensor Constants

#define COV_RATIO         0.2            // ug/m3 / mv

#define NO_DUST_VOLTAGE   400            // mv

#define SYS_VOLTAGE       5000           



/* I/O Define */

const int iled = 7;                      // Drive the LED of sensor

const int vout = A0;                     // Analog input from sensor Vout



/* Variables */

float density, voltage;

int adcvalue;



/* Moving Average Filter to smooth out jitter */

int Filter(int m) {

  static int flag_first = 0, _buff[10], sum;

  const int _buff_max = 10;

  if(flag_first == 0) {

    flag_first = 1;

    for(int i = 0; i < _buff_max; i++) {

      _buff[i] = m;

      sum += _buff[i];

    }

    return m;

  } else {

    sum -= _buff[0];

    for(int i = 0; i < (_buff_max - 1); i++) {

      _buff[i] = _buff[i + 1];

    }

    _buff[9] = m;

    sum += _buff[9];

    return sum / 10;

  }

}



void setup() {

  pinMode(iled, OUTPUT);

  digitalWrite(iled, LOW); 

  

  Serial.begin(9600);



  // Initialize OLED (Standard I2C uses A4/A5 on Nano)

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 

    Serial.println(F("OLED not found. Check wiring on A4/A5."));

    for(;;); // Don't proceed if display fails

  }

  

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);

  display.display();

}



void loop() {
  // 1. READ SENSOR
  digitalWrite(iled, HIGH);
  delayMicroseconds(280);
  adcvalue = analogRead(vout);
  delayMicroseconds(40);
  digitalWrite(iled, LOW);
  
  // Convert to density
  voltage = (5000 / 1024.0) * Filter(adcvalue) * 11;
  density = (voltage > 400) ? (voltage - 400) * 0.2 : 0;

  // 2. WAIT A MOMENT
  // Let the power stabilize after the LED pulse before talking to the OLED
  delay(100); 

  // 3. UPDATE OLED (Minimalist)
  display.setCursor(0, 20);
  display.setTextColor(SSD1306_WHITE, SSD1306_BLACK); // The second color avoids flashing!
  display.setTextSize(2);
  display.print(density, 1);
  display.print(" ug/m3  "); // Spaces clear out old digits
  display.display();
  
  delay(900); 
}