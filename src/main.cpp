#include <Arduino.h>
#include "time.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFiManager.h>

const char *ntpServer = "de.pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;
const int updateDelay_sec = 86400; // wait for one day to update from ntp
time_t currentTime;
time_t nextUpdateTime;

//PWM channels
#define LED_PWM_CHANNEL 0
#define ANALOGUE_DISPLAY_CHANNEL 1
#define ANALOGUE_BACKLIGHT_CHANNEL 2

//Analogue display backlight hysteresis 0-4095
#define ANALOGUE_BACKLIGHT_ON_LIMIT 1000
#define ANALOGUE_BACKLIGHT_OFF_LIMIT 1500

//Led Brightness
uint8_t LED_BRIGHTNESS = 128;
#define LED_BRIGHTNESS_MIN 32
#define LED_BRIGHTNESS_MAX 160
#define LED_BRIGHTNESS_MIN_PWM 800

//Minutes LEDs
#define LED_MIN_1_PIN 23
#define LED_MIN_2_PIN 22
#define LED_MIN_4_PIN 21
#define LED_MIN_8_PIN 19
#define LED_MIN_16_PIN 18
#define LED_MIN_32_PIN 2
#define LED_MIN_SIZE 6
const int minuteLeds[LED_MIN_SIZE] = {LED_MIN_1_PIN, LED_MIN_2_PIN, LED_MIN_4_PIN, LED_MIN_8_PIN, LED_MIN_16_PIN, LED_MIN_32_PIN};

//Hours LEDs
#define LED_HOUR_1_PIN 26
#define LED_HOUR_2_PIN 27
#define LED_HOUR_4_PIN 14
#define LED_HOUR_8_PIN 12
#define LED_HOUR_16_PIN 13
#define LED_HOUR_SIZE 5
const int hourLeds[LED_HOUR_SIZE] = {LED_HOUR_1_PIN, LED_HOUR_2_PIN, LED_HOUR_4_PIN, LED_HOUR_8_PIN, LED_HOUR_16_PIN};

//Seconds analogue display
#define ANALOGUE_SECONDS_PIN 25
#define ANALOGUE_BACKLIGHT_PIN 35
int8_t pwm = 0;
int8_t pwm_olli = 0;

//photoresistor
#define PHOTO 34

//expose functions
int8_t timeToInt(struct tm *timeinfo, const char *format);
void displaySeconds(int currentSeconds);
void assignNumToLeds(int num, const int *leds, const int s);
void showLocalTime();
void getTimeFromNtp();
void configModeCallback(WiFiManager *myWiFiManager);
void switchBacklight(int photoValue);

void setup()
{
  Serial.begin(115200);

  ledcSetup(ANALOGUE_DISPLAY_CHANNEL, 4000, 8);
  ledcSetup(ANALOGUE_BACKLIGHT_CHANNEL, 4000, 8);
  ledcSetup(LED_PWM_CHANNEL, 4000, 8);

  ledcAttachPin(ANALOGUE_SECONDS_PIN, ANALOGUE_DISPLAY_CHANNEL);
  WiFi.setHostname("Meterclock");

  getTimeFromNtp();
}

void loop()
{
  if (currentTime >= nextUpdateTime)
  {
    Serial.println("UPDATE TIME");
    getTimeFromNtp();
  }

  showLocalTime();

  Serial.println(analogRead(PHOTO));

  int photoValue = analogRead(PHOTO);

  if(photoValue < LED_BRIGHTNESS_MIN_PWM)
  {
      photoValue = LED_BRIGHTNESS_MIN_PWM;
  }

  LED_BRIGHTNESS = map(photoValue, LED_BRIGHTNESS_MIN_PWM, 4095, LED_BRIGHTNESS_MIN, LED_BRIGHTNESS_MAX);

  switchBacklight(photoValue);

  delay(250);
}

void getTimeFromNtp()
{
  WiFiManager wifiManager;
  wifiManager.autoConnect("MeterClock");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  showLocalTime();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  nextUpdateTime = currentTime + updateDelay_sec;
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
  assignNumToLeds(timeToInt(&timeinfo, "%M"), minuteLeds, LED_MIN_SIZE);
  assignNumToLeds(timeToInt(&timeinfo, "%H"), hourLeds, LED_HOUR_SIZE);

  ledcWrite(LED_PWM_CHANNEL, LED_BRIGHTNESS);

  time(&currentTime);
}

void displaySeconds(int currentSeconds)
{
  pwm = map(currentSeconds, 0, 59, 0, 255);
  if (pwm < 0)
  {
    pwm = pwm * -1;
  }

  //Ollis custom analoque display can't handle full 3v3. pushed down to 'bout 0.7V
  pwm_olli = map(pwm, 0, 127, 0, 59);

  ledcWrite(ANALOGUE_DISPLAY_CHANNEL, pwm_olli);
}

void switchBacklight(int photoValue)
{
  if (photoValue < ANALOGUE_BACKLIGHT_ON_LIMIT)
  {
    ledcAttachPin(ANALOGUE_BACKLIGHT_PIN, LED_PWM_CHANNEL);
  }

  if (photoValue > ANALOGUE_BACKLIGHT_OFF_LIMIT)
  {
    ledcDetachPin(ANALOGUE_BACKLIGHT_PIN);
  }
}

void assignNumToLeds(int num, const int *leds, const int s)
{
  for (int i = s - 1; i >= 0; i--)
    bitRead(num, i) ? ledcAttachPin(leds[i], LED_PWM_CHANNEL) : ledcDetachPin(leds[i]);
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