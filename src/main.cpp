#include <Adafruit_GFX.h>      // Biblioteka graficzna
#include <Adafruit_ILI9341.h>  // Biblioteka dla ILI9341
#include <Wire.h>              // Biblioteka I2C
#include "Adafruit_AHTX0.h"    // Biblioteka dla AHT20
#include "Adafruit_BMP280.h"   // Biblioteka dla BMP280

// Definicja pinów dla TFT
#define TFT_CS   10
#define TFT_DC   2
#define TFT_MOSI 11
#define TFT_SCK  12
#define TFT_RST  -1  // RST podłączone do RESET ESP32
#define TFT_LED  8   // Pin podświetlenia

// Piny I2C
#define I2C_SDA 45
#define I2C_SCL 46

// Inicjalizacja obiektów
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCK, TFT_RST);
Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;

// Funkcja ustawiająca podświetlenie
void setBacklight(bool state) {
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, state ? HIGH : LOW);
}

void setup() {
  // Inicjalizacja I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // Inicjalizacja TFT
  tft.begin();
  setBacklight(true); // Włączenie podświetlenia
  tft.setRotation(1); // Orientacja ekranu (0-3)
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setTextSize(2);

  // Inicjalizacja AHT20
  if (!aht.begin()) {
    tft.setCursor(0, 0);
    tft.println("AHT20 not found!");
    while (1);
  }

  // Inicjalizacja BMP280
  if (!bmp.begin(0x76)) { // Adres I2C BMP280: 0x76 lub 0x77
    tft.setCursor(0, 20);
    tft.println("BMP280 not found!");
    while (1);
  }

  // Ustawienia BMP280
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     // Tryb pracy
                  Adafruit_BMP280::SAMPLING_X2,     // Oversampling temperatury
                  Adafruit_BMP280::SAMPLING_X16,    // Oversampling ciśnienia
                  Adafruit_BMP280::FILTER_X16,      // Filtr
                  Adafruit_BMP280::STANDBY_MS_500); // Czas uśpienia
}

void loop() {
  // Odczyt z AHT20
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);

  // Odczyt z BMP280
  float temperature = bmp.readTemperature();
  float pressure = bmp.readPressure() / 100.0F; // Konwersja Pa na hPa

  // Wyświetlanie danych na ekranie TFT
  tft.fillScreen(ILI9341_BLACK); // Czyszczenie ekranu
  tft.setCursor(0, 0);

  tft.println("AHT20:");
  tft.print("Temp: ");
  tft.print(temp.temperature);
  tft.println(" C");
  tft.print("Wilg: ");
  tft.print(humidity.relative_humidity);
  tft.println(" %");

  tft.println("\nBMP280:");
  tft.print("Temp: ");
  tft.print(temperature);
  tft.println(" C");
  tft.print("Cis: ");
  tft.print(pressure);
  tft.println(" hPa");

  delay(2000); // Odświeżanie co 2 sekundy
}