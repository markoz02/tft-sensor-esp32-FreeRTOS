#include <Adafruit_GFX.h>      // Biblioteka graficzna
#include <Adafruit_ILI9341.h>  // Biblioteka dla ILI9341
#include <Wire.h>              // Biblioteka I2C
#include "Adafruit_AHTX0.h"    // Biblioteka dla AHT20
#include "Adafruit_BMP280.h"   // Biblioteka dla BMP280
#include "RTClib.h"            // Biblioteka dla RTC DS3231
#include <string.h>

// Definicja pinów dla TFT
#define TFT_CS   10
#define TFT_DC   2
#define TFT_MOSI 11
#define TFT_SCK  12
#define TFT_RST  -1  // RST podłączone do RESET ESP32
#define TFT_LED  8   // Pin podświetlenia

// Piny I2C
#define I2C_SDA 45
#define I2C_SCL 48

// Piny przycisków
#define BUTTON_NAV 36  // Przycisk nawigacji
#define BUTTON_SET 37  // Przycisk ustawiania

// Inicjalizacja obiektów
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCK, TFT_RST);
Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;
RTC_DS3231 rtc;

// Zmienne dla obsługi ustawiania czasu
bool settingMode = false;
int settingField = 0; // 0: rok, 1: miesiąc, 2: dzień, 3: godzina, 4: minuta
DateTime now;

// Poprzednie wartości tekstowe do porównania długości
char prevAhtTemp[48] = "";
char prevAhtHum[48] = "";
char prevBmpTemp[48] = "";
char prevBmpPress[48] = "";
char prevDayOfWeek[48] = "";
char prevDate[48] = "";
char prevTime[48] = "";
char prevSetMode[48] = "";

// Funkcja ustawiająca podświetlenie
void setBacklight(bool state) {
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, state ? HIGH : LOW);
}

// Funkcja czyszcząca linię, jeśli nowy tekst jest krótszy
void clearIfShorter(int x, int y, const char* newText, char* prevText) {
  if (strlen(newText) < strlen(prevText)) {
    // Czyszczenie obszaru linii (zakładamy szerokość 320 pikseli, wysokość 16 pikseli dla textSize=2)
    tft.fillRect(x, y, 320, 16, ILI9341_MAROON);
  }
  strcpy(prevText, newText); // Aktualizacja poprzedniego tekstu
}

// Funkcja wyświetlania daty i godziny
void displayDateTime() {
  now = rtc.now();
  char buffer[32];
  
  // Dzień tygodnia
  const char* daysOfWeek[] = {"Niedziela", "Poniedzialek", "Wtorek", "Sroda", 
                             "Czwartek", "Piatek", "Sobota"};
  tft.setCursor(0, 108);
  tft.println("RTC DS3231:");
  
  strcpy(buffer, daysOfWeek[now.dayOfTheWeek()]);
  clearIfShorter(0, 124, buffer, prevDayOfWeek);
  tft.setCursor(0, 124);
  tft.print(buffer);
  tft.println("  ");
  
  // Data
  sprintf(buffer, "%04d-%02d-%02d", now.year(), now.month(), now.day());
  clearIfShorter(0, 140, buffer, prevDate);
  tft.setCursor(0, 140);
  tft.print(buffer);
  tft.println("  ");
  
  // Godzina
  sprintf(buffer, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  clearIfShorter(0, 156, buffer, prevTime);
  tft.setCursor(0, 156);
  tft.print(buffer);
  
  // Wskazanie trybu ustawiania
  if (settingMode) {
    const char* fieldNames[] = {"Rok", "Miesiac", "Dzien", "Godzina", "Minuta"};
    sprintf(buffer, " SET:%s", fieldNames[settingField]);
  } else {
    strcpy(buffer, "");
  }
  clearIfShorter(156, 156, buffer, prevSetMode);
  tft.setCursor(156, 156);
  tft.print(buffer);
}

void setup() {
  // Inicjalizacja I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // Inicjalizacja przycisków
  pinMode(BUTTON_NAV, INPUT);
  pinMode(BUTTON_SET, INPUT);

  // Inicjalizacja TFT
  tft.begin();
  setBacklight(true); // Włączenie podświetlenia
  tft.setRotation(1); // Orientacja ekranu (0-3)
  tft.fillScreen(ILI9341_MAROON); // Początkowe czyszczenie ekranu
  tft.setTextColor(ILI9341_WHITE, ILI9341_MAROON); // Kolor tekstu i tła
  tft.setTextSize(2);

  // Inicjalizacja AHT20
  if (!aht.begin()) {
    tft.setCursor(0, 0);
    tft.println("AHT20 problem!");
    while (1);
  }

  // Inicjalizacja BMP280
  if (!bmp.begin(0x77)) {
    tft.setCursor(0, 20);
    tft.println("BMP280 problem!");
    while (1);
  }

  // Inicjalizacja RTC DS3231
  if (!rtc.begin()) {
    tft.setCursor(0, 40);
    tft.println("RTC problem!");
    while (1);
  }

  rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // Ustawienie RTC na czas kompilacji

  // Ustawienia BMP280
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     // Tryb pracy
                  Adafruit_BMP280::SAMPLING_X2,     // Oversampling temperatury
                  Adafruit_BMP280::SAMPLING_X16,    // Oversampling ciśnienia
                  Adafruit_BMP280::FILTER_X16,      // Filtr
                  Adafruit_BMP280::STANDBY_MS_500); // Czas uśpienia
}

void loop() {
  static unsigned long lastUpdate = 0;
  static unsigned long lastNavPress = 0;
  static unsigned long lastSetPress = 0;
  unsigned long currentMillis = millis();

  // Obsługa przycisku nawigacji (przełączanie pól lub trybu)
  if (digitalRead(BUTTON_NAV) == HIGH && currentMillis - lastNavPress > 100) {
    lastNavPress = currentMillis;
    if (!settingMode) {
      settingMode = true;
      settingField = 0;
    } else {
      settingField = (settingField + 1) % 5; // Przełącz między polami
    }
  }

  // Obsługa przycisku ustawiania (zmiana wartości)
  if (digitalRead(BUTTON_SET) == HIGH && currentMillis - lastSetPress > 100) {
    lastSetPress = currentMillis;
    if (settingMode) {
      now = rtc.now();
      int year = now.year();
      int month = now.month();
      int day = now.day();
      int hour = now.hour();
      int minute = now.minute();

      switch (settingField) {
        case 0: year++; if (year > 2099) year = 2025; break;
        case 1: month++; if (month > 12) month = 1; break;
        case 2: day++; if (day > 31) day = 1; break; // Uproszczona logika
        case 3: hour++; if (hour > 23) hour = 0; break;
        case 4: minute++; if (minute > 59) minute = 0; break;
      }
      rtc.adjust(DateTime(year, month, day, hour, minute, 0));
    } else {
      settingMode = true;
      settingField = 0;
    }
  }

  // Wyjście z trybu ustawiania po 10 sekundach bezczynności
  if (settingMode && currentMillis - lastNavPress > 10000 && currentMillis - lastSetPress > 10000) {
    settingMode = false;
  }

  // Aktualizacja ekranu co 200 milisekund
  if (currentMillis - lastUpdate >= 200) {
    lastUpdate = currentMillis;
    char buffer[32];

    // Odczyt z AHT20
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);

    // Wyświetlanie danych AHT20
    tft.setCursor(0, 0);
    tft.println("AHT20:");
    
    sprintf(buffer, "Temperatura: %.1f C", temp.temperature);
    clearIfShorter(0, 16, buffer, prevAhtTemp);
    tft.setCursor(0, 16);
    tft.println(buffer);
    
    sprintf(buffer, "Wilgotnosc: %.1f %%", humidity.relative_humidity);
    clearIfShorter(0, 32, buffer, prevAhtHum);
    tft.setCursor(0, 32);
    tft.println(buffer);

    // Odczyt z BMP280
    float temperature = bmp.readTemperature();
    float pressure = bmp.readPressure() / 100.0F; // Konwersja Pa na hPa

    // Wyświetlanie danych BMP280
    tft.setCursor(0, 60);
    tft.println("BMP280:");
    /*
    sprintf(buffer, "Temp: %.1f C", temperature);
    clearIfShorter(0, 76, buffer, prevBmpTemp);
    tft.setCursor(0, 76);
    tft.println(buffer);
    */
    sprintf(buffer, "Cisnienie: %.1f hPa", pressure);
    clearIfShorter(0, 76, buffer, prevBmpPress);
    tft.setCursor(0, 76);
    tft.println(buffer);

    // Wyświetlanie danych RTC
    displayDateTime();
  }
}