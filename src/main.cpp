#include <Arduino.h>
#include "time.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFiManager.h>

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;

//PWM channels
#define LED_PWM_CHANNEL 0
#define ANALOGUE_DISPLAY_CHANNEL 1

//Led Brightness
uint8_t LED_BRIGHTNESS = 128;

//Minutes LEDs
#define LED_MIN_1_PIN 23
#define LED_MIN_2_PIN 22
#define LED_MIN_4_PIN 21
#define LED_MIN_8_PIN 19
#define LED_MIN_16_PIN 18
#define LED_MIN_32_PIN 2
int minuteLeds[6] = {LED_MIN_1_PIN, LED_MIN_2_PIN, LED_MIN_4_PIN, LED_MIN_8_PIN, LED_MIN_16_PIN, LED_MIN_32_PIN};

//Hours LEDs
#define LED_HOUR_1_PIN 26
#define LED_HOUR_2_PIN 27
#define LED_HOUR_4_PIN 14
#define LED_HOUR_8_PIN 12
#define LED_HOUR_16_PIN 13
int hourLeds[5] = {LED_HOUR_1_PIN, LED_HOUR_2_PIN, LED_HOUR_4_PIN, LED_HOUR_8_PIN, LED_HOUR_16_PIN};

//Seconds analogue display
#define ANALOGUE_SECONDS_PIN 25
int8_t pwm = 0;
int8_t pwm_olli = 0;

//photoresistor
#define PHOTO 34

//expose functions
int8_t timeToInt(struct tm *timeinfo, const char *format);
void displaySeconds(int currentSeconds);
void assignMinutes(int currentMinutes);
void assignHours(int currentHours);
void showLocalTime();

void setup()
{
  Serial.begin(115200);

  ledcSetup(ANALOGUE_DISPLAY_CHANNEL, 4000, 8);
  ledcSetup(LED_PWM_CHANNEL, 4000, 8);

  ledcAttachPin(ANALOGUE_SECONDS_PIN, ANALOGUE_DISPLAY_CHANNEL);

  //WiFiManager
  WiFiManager wifiManager;
  wifiManager.autoConnect("MeterClock");

  //init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  showLocalTime();

  //disconnect WiFi as it's no longer needed
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void loop()
{
  delay(1000);
  showLocalTime();
}

void showLocalTime()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }

  displaySeconds(timeToInt(&timeinfo, "%S"));
  assignMinutes(timeToInt(&timeinfo, "%M"));
  assignHours(timeToInt(&timeinfo, "%H"));

  ledcWrite(LED_PWM_CHANNEL, LED_BRIGHTNESS);
}

void displaySeconds(int currentSeconds)
{
  pwm = map(currentSeconds, 0, 59, 0, 255);
  if (pwm < 0)
  {
    pwm = pwm * -1;
  }
  pwm_olli = map(pwm, 0, 127, 0, 55);

  ledcWrite(ANALOGUE_DISPLAY_CHANNEL, pwm_olli);
}

void assignMinutes(int currentMinutes)
{
  for (int i = 5; i >= 0; i--)
  {
    if (bitRead(currentMinutes, i) == 1)
    {
      ledcAttachPin(minuteLeds[i], LED_PWM_CHANNEL);
    }
    else
    {
      ledcDetachPin(minuteLeds[i]);
    }
  }
}

void assignHours(int currentHours)
{
  for (int i = 4; i >= 0; i--)
  {
    if (bitRead(currentHours, i) == 1)
    {
      ledcAttachPin(hourLeds[i], LED_PWM_CHANNEL);
    }
    else
    {
      ledcDetachPin(hourLeds[i]);
    }
  }
}

int8_t timeToInt(struct tm *timeinfo, const char *format)
{
  const char *f = format;
  if (!f)
  {
    f = "%c";
  }
  char buf[64];
  strftime(buf, 64, f, timeinfo);
  String s = buf;
  return s.toInt();
}