#include <Arduino.h>
#include <time.h>

#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFiManager.h>

const bool continuousWifi = true;
const char *ntpServer = "de.pool.ntp.org";
const char *TZ_INFO = "CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00";
const int updateDelay_sec = 28000;
struct tm timeinfo;
time_t currentTime;
time_t nextUpdateTime;

//PWM channels
#define LED_PWM_CHANNEL 0
#define ANALOGUE_DISPLAY_CHANNEL 1
#define ANALOGUE_BACKLIGHT_CHANNEL 2

//Analogue display backlight hysteresis 0-4095
#define ANALOGUE_BACKLIGHT_ON_LIMIT 2000
#define ANALOGUE_BACKLIGHT_OFF_LIMIT 2500

//Led Brightness
uint8_t LED_BRIGHTNESS = 96;
#define LED_BRIGHTNESS_MIN 8
#define LED_BRIGHTNESS_MAX 180
#define LED_BRIGHTNESS_MIN_PWM 950

//Minutes LEDs
#define LED_MIN_1_PIN 23
#define LED_MIN_2_PIN 22
#define LED_MIN_4_PIN 21
#define LED_MIN_8_PIN 19
#define LED_MIN_16_PIN 18
#define LED_MIN_32_PIN 2
#define LED_MIN_SIZE 6
const int minuteLeds[LED_MIN_SIZE] = {LED_MIN_1_PIN, LED_MIN_2_PIN, LED_MIN_4_PIN, LED_MIN_8_PIN, LED_MIN_16_PIN, LED_MIN_32_PIN};
//const int minuteLeds[LED_MIN_SIZE] = {LED_MIN_32_PIN, LED_MIN_16_PIN, LED_MIN_8_PIN, LED_MIN_4_PIN, LED_MIN_2_PIN, LED_MIN_1_PIN};

//Hours LEDs
#define LED_HOUR_1_PIN 26
#define LED_HOUR_2_PIN 27
#define LED_HOUR_4_PIN 14
#define LED_HOUR_8_PIN 12
#define LED_HOUR_16_PIN 13
#define LED_HOUR_SIZE 5
const int hourLeds[LED_HOUR_SIZE] = {LED_HOUR_1_PIN, LED_HOUR_2_PIN, LED_HOUR_4_PIN, LED_HOUR_8_PIN, LED_HOUR_16_PIN};
//const int hourLeds[LED_HOUR_SIZE] = {LED_HOUR_16_PIN, LED_HOUR_8_PIN, LED_HOUR_4_PIN, LED_HOUR_2_PIN, LED_HOUR_1_PIN};

//Seconds analogue display
#define ANALOGUE_SECONDS_PIN 25
#define ANALOGUE_BACKLIGHT_PIN 4
int8_t pwm = 0;
int8_t pwm_olli = 0;

//photoresistor
#define PHOTO 34

//webserver
WebServer server(80);
uint16_t lastUpdateTime = 0;

//flags
bool updateTimeFlag;
bool updateDisplayFlag;

//tasks
TaskHandle_t syncRTCTask;
TaskHandle_t displayTask;
TaskHandle_t brightnessTask;

//queues
const TickType_t QueueDelay = 100 / portTICK_PERIOD_MS;
QueueHandle_t updateTimeQueue;
QueueHandle_t updateDisplayQueue;

//expose to compiler
void syncRTCLoop(void *parameter);
void displayLoop(void *parameter);
void brightnessLoop(void *parameter);

//helpers
int8_t timeToInt(struct tm *timeinfo, const char *format);
void displaySeconds(int currentSeconds);
void assignNumToLeds(int num, const int *leds, const int s);
void syncTime();
void showWebTime();
void showLastUpdate();

void setup()
{
  Serial.begin(115200);
  WiFi.setHostname("Meterclock");

  if (continuousWifi)
  {
    WiFiManager wifiManager;
    wifiManager.autoConnect("MeterClock");

    server.begin();
    Serial.println("HTTP Server gestartet (80)");
    server.on("/", showWebTime);
    server.on("/lastupdate", showLastUpdate);
  }

  syncTime();

  //setup pwm channels
  ledcSetup(LED_PWM_CHANNEL, 4000, 8);
  ledcSetup(ANALOGUE_DISPLAY_CHANNEL, 4000, 8);
  ledcSetup(ANALOGUE_BACKLIGHT_CHANNEL, 4000, 8);
  ledcAttachPin(ANALOGUE_SECONDS_PIN, ANALOGUE_DISPLAY_CHANNEL);

  //init queues
  updateTimeQueue = xQueueCreate(1, sizeof(bool));
  updateDisplayQueue = xQueueCreate(1, sizeof(bool));

  //init tasks
  xTaskCreatePinnedToCore(syncRTCLoop, "syncRTC", 2560, NULL, 0, &syncRTCTask, 1);
  xTaskCreatePinnedToCore(displayLoop, "display", 2048, NULL, 0, &displayTask, 1);
  xTaskCreatePinnedToCore(brightnessLoop, "brightness", 1024, NULL, 0, &brightnessTask, 1);
}

void loop()
{
  updateTimeFlag = true;
  updateDisplayFlag = true;

  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
  }
  time(&currentTime);

  //rtc sync cycle
  if (currentTime >= nextUpdateTime)
  {
    //send update time flag
    xQueueSend(updateTimeQueue, &updateTimeFlag, QueueDelay);
  }
  //send update display flag
  xQueueSend(updateDisplayQueue, &updateDisplayFlag, QueueDelay);

  server.handleClient();
  delay(1000);
}

void syncRTCLoop(void *parameter)
{
  bool updateTimeFlag = false;

  for (;;)
  {
    //wait for update time flag
    if (xQueueReceive(updateTimeQueue, &updateTimeFlag, portMAX_DELAY))
    {
      syncTime();
    }

    delay(100);
  }
}

void displayLoop(void *parameter)
{
  bool updateDisplayFlag = false;

  for (;;)
  {
    if (xQueueReceive(updateDisplayQueue, &updateDisplayFlag, portMAX_DELAY))
    {
      displaySeconds(timeToInt(&timeinfo, "%S"));
      assignNumToLeds(timeToInt(&timeinfo, "%M"), minuteLeds, LED_MIN_SIZE);
      assignNumToLeds(timeToInt(&timeinfo, "%H"), hourLeds, LED_HOUR_SIZE);
    }
  }
}

void syncTime()
{
  Serial.print("Time before sync: ");
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  Serial.println("synchronizing rtc to ntp ...");
  if (!continuousWifi)
  {
    WiFiManager wifiManager;
    wifiManager.autoConnect("MeterClock");
  }

  configTzTime(TZ_INFO, ntpServer);

  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
  }
  else
  {
    time(&currentTime);
    lastUpdateTime = millis();
  }

  if (!continuousWifi)
  {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  Serial.println("time synchronized.");

  nextUpdateTime = currentTime + updateDelay_sec;

  Serial.print("Time after sync: ");
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

void brightnessLoop(void *parameter)
{
  int photoValue;
  for (;;)
  {
    //read photo resitor
    photoValue = analogRead(PHOTO);

    //led brightness
    if (photoValue < LED_BRIGHTNESS_MIN_PWM)
    {
      photoValue = LED_BRIGHTNESS_MIN_PWM;
    }
    LED_BRIGHTNESS = map(photoValue, LED_BRIGHTNESS_MIN_PWM, 4095, LED_BRIGHTNESS_MIN, LED_BRIGHTNESS_MAX);
    ledcWrite(LED_PWM_CHANNEL, LED_BRIGHTNESS);

    //analogue backlight
    ledcWrite(ANALOGUE_BACKLIGHT_CHANNEL, 255);
    if (photoValue < ANALOGUE_BACKLIGHT_ON_LIMIT)
    {
      ledcAttachPin(ANALOGUE_BACKLIGHT_PIN, ANALOGUE_BACKLIGHT_CHANNEL);
    }
    if (photoValue > ANALOGUE_BACKLIGHT_OFF_LIMIT)
    {
      ledcDetachPin(ANALOGUE_BACKLIGHT_PIN);
    }

    delay(100);
  }
}

void showWebTime()
{
  char currentTimeString[50];
  strftime(currentTimeString, sizeof(currentTimeString), "%A, %B %d %Y %H:%M:%S", &timeinfo);
  server.send(200, "text/plain", currentTimeString);
}

void showLastUpdate(){
  String lastUpdate = String((millis()- lastUpdateTime)/60000, DEC);
  server.send(200, "text/plain", lastUpdate);
}

void handleNotFound()
{
  server.send(404, "text/plain", "File Not Found\n\n");
}

void displaySeconds(int currentSeconds)
{
  pwm = map(currentSeconds, 0, 59, 0, 255);
  if (pwm < 0)
  {
    pwm = pwm * -1;
  }

  //Ollis custom analoque display can't handle full 3v3. cut down to 'bout 0.7V
  pwm_olli = map(pwm, 0, 127, 0, 59);

  ledcWrite(ANALOGUE_DISPLAY_CHANNEL, pwm_olli);
  Serial.printf("%d = %d\n", pwm_olli, currentSeconds);
}

void assignNumToLeds(int num, const int *leds, const int s)
{
  for (int i = s - 1; i >= 0; i--)
  {
    if (bitRead(num, i) == 1)
    {
      ledcAttachPin(leds[i], LED_PWM_CHANNEL);
      Serial.print("1");
    }
    else
    {
      ledcDetachPin(leds[i]);
      Serial.print("0");
    }
  }

  Serial.print(" = ");
  Serial.println(num);
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