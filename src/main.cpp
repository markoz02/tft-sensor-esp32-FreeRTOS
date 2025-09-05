#include <Adafruit_GFX.h>     // Biblioteka graficzna
#include <Adafruit_ILI9341.h> // Biblioteka dla ILI9341
#include <Wire.h>             // Biblioteka I2C
#include "Adafruit_AHTX0.h"   // Biblioteka dla AHT20
#include "Adafruit_BMP280.h"  // Biblioteka dla BMP280
#include "RTClib.h"           // Biblioteka dla RTC DS3231
#include <string.h>
#include <freertos/FreeRTOS.h> // Biblioteka FreeRTOS
#include <freertos/task.h>     // Biblioteka dla zadań FreeRTOS
#include <freertos/queue.h>    // Biblioteka dla kolejek FreeRTOS
#include <freertos/semphr.h>   // Biblioteka dla semaforów FreeRTOS

// Definicja pinów dla TFT
#define TFT_CS 10
#define TFT_DC 2
#define TFT_MOSI 11
#define TFT_SCK 12
#define TFT_RST -1 // RST podłączone do RESET ESP32
#define TFT_LED 8  // Pin podświetlenia

// Piny I2C
#define I2C_SDA 45
#define I2C_SCL 48

// Piny przycisków
#define BUTTON_NAV 36 // Przycisk nawigacji
#define BUTTON_SET 37 // Przycisk ustawiania

// Inicjalizacja obiektów
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCK, TFT_RST);
Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;
RTC_DS3231 rtc;

// Struktura do przechowywania danych z czujników
typedef struct
{
  float ahtTemp;
  float ahtHum;
  float bmpTemp;
  float bmpPress;
  DateTime time;
  bool settingMode;
  int settingField;
} SensorData;

// Kolejka do przesyłania danych z czujników do zadania wyświetlacza
QueueHandle_t sensorDataQueue;
// Semafor do ochrony dostępu do wyświetlacza
SemaphoreHandle_t tftMutex;
// Semafor do ochrony zmiennych tekstowych
SemaphoreHandle_t textMutex;
// Semafor do ochrony dostępu do RTC
SemaphoreHandle_t rtcMutex;

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
void setBacklight(bool state)
{
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, state ? HIGH : LOW);
}

// Funkcja czyszcząca linię, jeśli nowy tekst jest krótszy
void clearIfShorter(int x, int y, const char *newText, char *prevText)
{
  xSemaphoreTake(textMutex, portMAX_DELAY);
  if (strlen(newText) < strlen(prevText))
  {
    xSemaphoreTake(tftMutex, portMAX_DELAY);
    tft.fillRect(x, y, 320, 16, ILI9341_MAROON);
    xSemaphoreGive(tftMutex);
  }
  strcpy(prevText, newText);
  xSemaphoreGive(textMutex);
}

// Funkcja wyświetlania daty i godziny
void displayDateTime(SensorData *data)
{
  char buffer[32];
  const char *daysOfWeek[] = {"Niedziela", "Poniedzialek", "Wtorek", "Sroda",
                              "Czwartek", "Piatek", "Sobota"};

  xSemaphoreTake(tftMutex, portMAX_DELAY);
  tft.setCursor(0, 108);
  tft.println("RTC DS3231:");

  strcpy(buffer, daysOfWeek[data->time.dayOfTheWeek()]);
  clearIfShorter(0, 124, buffer, prevDayOfWeek);
  tft.setCursor(0, 124);
  tft.print(buffer);
  tft.println("  ");

  sprintf(buffer, "%04d-%02d-%02d", data->time.year(), data->time.month(), data->time.day());
  clearIfShorter(0, 140, buffer, prevDate);
  tft.setCursor(0, 140);
  tft.print(buffer);
  tft.println("  ");

  sprintf(buffer, "%02d:%02d:%02d", data->time.hour(), data->time.minute(), data->time.second());
  clearIfShorter(0, 156, buffer, prevTime);
  tft.setCursor(0, 156);
  tft.print(buffer);

  if (data->settingMode)
  {
    const char *fieldNames[] = {"Rok", "Miesiac", "Dzien", "Godzina", "Minuta"};
    sprintf(buffer, " SET:%s", fieldNames[data->settingField]);
  }
  else
  {
    strcpy(buffer, "");
  }
  clearIfShorter(156, 156, buffer, prevSetMode);
  tft.setCursor(156, 156);
  tft.print(buffer);
  xSemaphoreGive(tftMutex);
}

// Zadanie do obsługi czujników
void sensorTask(void *pvParameters)
{
  while (1)
  {
    SensorData data;
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);
    data.ahtTemp = temp.temperature;
    data.ahtHum = humidity.relative_humidity;
    data.bmpTemp = bmp.readTemperature();
    data.bmpPress = bmp.readPressure() / 100.0F + 268;

    xSemaphoreTake(rtcMutex, portMAX_DELAY);
    data.time = rtc.now();
    xSemaphoreGive(rtcMutex);

    data.settingMode = false; // Ustawiane w zadaniu przycisków
    data.settingField = 0;

    xQueueOverwrite(sensorDataQueue, &data);
    vTaskDelay(pdMS_TO_TICKS(1000)); // Aktualizacja co 1 sekundę
  }
}

// Zadanie do obsługi wyświetlacza
void displayTask(void *pvParameters)
{
  SensorData data;
  char buffer[32];

  while (1)
  {
    if (xQueueReceive(sensorDataQueue, &data, portMAX_DELAY))
    {
      xSemaphoreTake(tftMutex, portMAX_DELAY);

      tft.setCursor(0, 0);
      tft.println("AHT20:");

      sprintf(buffer, "Temperatura: %.1f C", data.ahtTemp);
      clearIfShorter(0, 16, buffer, prevAhtTemp);
      tft.setCursor(0, 16);
      tft.println(buffer);

      sprintf(buffer, "Wilgotnosc: %.1f %%", data.ahtHum);
      clearIfShorter(0, 32, buffer, prevAhtHum);
      tft.setCursor(0, 32);
      tft.println(buffer);

      tft.setCursor(0, 60);
      tft.println("BMP280:");

      sprintf(buffer, "Cisnienie: %.1f hPa", data.bmpPress);
      clearIfShorter(0, 76, buffer, prevBmpPress);
      tft.setCursor(0, 76);
      tft.println(buffer);

      xSemaphoreGive(tftMutex);

      displayDateTime(&data);
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // Odświeżanie wyświetlacza
  }
}

// Zadanie do obsługi przycisków
void buttonTask(void *pvParameters)
{
  static unsigned long lastNavPress = 0;
  static unsigned long lastSetPress = 0;
  bool settingMode = false;
  int settingField = 0;

  while (1)
  {
    unsigned long currentMillis = millis();

    if (digitalRead(BUTTON_NAV) == HIGH && currentMillis - lastNavPress > 100)
    {
      lastNavPress = currentMillis;
      if (!settingMode)
      {
        settingMode = true;
        settingField = 0;
      }
      else
      {
        settingField = (settingField + 1) % 5;
      }
    }

    if (digitalRead(BUTTON_SET) == HIGH && currentMillis - lastSetPress > 100)
    {
      lastSetPress = currentMillis;
      if (settingMode)
      {
        xSemaphoreTake(rtcMutex, portMAX_DELAY);
        DateTime now = rtc.now();
        int year = now.year();
        int month = now.month();
        int day = now.day();
        int hour = now.hour();
        int minute = now.minute();

        switch (settingField)
        {
        case 0:
          year++;
          if (year > 2099)
            year = 2025;
          break;
        case 1:
          month++;
          if (month > 12)
            month = 1;
          break;
        case 2:
          day++;
          if (day > 31)
            day = 1;
          break;
        case 3:
          hour++;
          if (hour > 23)
            hour = 0;
          break;
        case 4:
          minute++;
          if (minute > 59)
            minute = 0;
          break;
        }
        rtc.adjust(DateTime(year, month, day, hour, minute, 0));
        xSemaphoreGive(rtcMutex);
      }
      else
      {
        settingMode = true;
        settingField = 0;
      }
    }

    if (settingMode && currentMillis - lastNavPress > 10000 && currentMillis - lastSetPress > 10000)
    {
      settingMode = false;
    }

    // Aktualizacja danych w kolejce
    SensorData data;
    if (xQueuePeek(sensorDataQueue, &data, 0))
    {
      data.settingMode = settingMode;
      data.settingField = settingField;
      xQueueOverwrite(sensorDataQueue, &data);
    }

    vTaskDelay(pdMS_TO_TICKS(50)); // Sprawdzanie przycisków co 50ms
  }
}

// Funkcja inicjalizująca
void setup()
{
  // Inicjalizacja I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // Inicjalizacja przycisków
  pinMode(BUTTON_NAV, INPUT);
  pinMode(BUTTON_SET, INPUT);

  // Inicjalizacja TFT
  tft.begin();
  setBacklight(true);
  tft.setRotation(1);
  tft.fillScreen(ILI9341_MAROON);
  tft.setTextColor(ILI9341_WHITE, ILI9341_MAROON);
  tft.setTextSize(2);

  // Inicjalizacja AHT20
  if (!aht.begin())
  {
    tft.setCursor(0, 0);
    tft.println("AHT20 problem!");
    while (1)
      ;
  }

  // Inicjalizacja BMP280
  if (!bmp.begin(0x77))
  {
    tft.setCursor(0, 20);
    tft.println("BMP280 problem!");
    while (1)
      ;
  }

  // Inicjalizacja RTC DS3231
  if (!rtc.begin())
  {
    tft.setCursor(0, 40);
    tft.println("RTC problem!");
    while (1)
      ;
  }

  // Ustawienie RTC na czas kompilacji, jeśli stracił zasilanie
  if (rtc.lostPower())
  {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Ustawienia BMP280
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                  Adafruit_BMP280::SAMPLING_X2,
                  Adafruit_BMP280::SAMPLING_X16,
                  Adafruit_BMP280::FILTER_X16,
                  Adafruit_BMP280::STANDBY_MS_500);

  // Inicjalizacja kolejki i semaforów
  sensorDataQueue = xQueueCreate(1, sizeof(SensorData));
  tftMutex = xSemaphoreCreateMutex();
  textMutex = xSemaphoreCreateMutex();
  rtcMutex = xSemaphoreCreateMutex();

  // Tworzenie zadań FreeRTOS
  xTaskCreate(sensorTask, "SensorTask", 4096, NULL, 2, NULL);
  xTaskCreate(displayTask, "DisplayTask", 4096, NULL, 1, NULL);
  xTaskCreate(buttonTask, "ButtonTask", 2048, NULL, 3, NULL);
}

void loop()
{
  // Pusta pętla, wszystko obsługiwane przez zadania FreeRTOS
}