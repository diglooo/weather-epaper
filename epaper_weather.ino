/*
    Name:       epaper_weather.ino
    Created:	02/01/2022 19:01:37
    Author:     DIGLO-PC\diglo
	Based on the work of Daniel Eichhorn
	See more at http://blog.squix.ch
*/

/*
README!!!

Libraries to install:
- GxEPD2 by Jean-Marc Zingg
- ESP8266 WeatherStation by ThingPulse
- Json Streaming Parser by Daniel Eichhorn
- simpleDSTadjust by neptune2

Electrical connections:
Waveshare 2.9 inch display 290x128 version 2
DISPLAY		ESP8266
BUSY		D1
RST			D2
DC			D8
CS			D3
CLK			D6 (HW SPI)
DIN			D7 (HW SPI)
GND			GND
VCC			D4 (yes, the epaper display is powered by a pin of the ESP8266)

In order to wake from deep sleep, you must connect D0 to RST on the ESP8266 board.
*/

#include <Arduino.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <simpleDSTadjust.h>
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <JsonListener.h>
#include <OpenWeatherMapCurrent.h>
#include <OpenWeatherMapForecast.h>

#include "meteocons24pt7b.h"
#include "settings.h"

#define DISP_PWR_1 D4

GxEPD2_BW<GxEPD2_290_T94, GxEPD2_290_T94::HEIGHT> display(GxEPD2_290_T94(/*CS*/ D3, /*DC*/ D8, /*RST*/ D2, /*BUSY*/ D1));
OpenWeatherMapCurrentData conditions;
OpenWeatherMapForecastData forecasts[MAX_FORECASTS];
simpleDSTadjust dstAdjusted(StartRule, EndRule);

uint32_t dstOffset = 0;
uint8_t foundForecasts = 0;
long lastDownloadUpdate = millis();
String moonAgeImage = "";

boolean connectWifi() {
	if (WiFi.status() == WL_CONNECTED) return true;
	//Manual Wifi
	Serial.print("[");
	Serial.print(WIFI_SSID.c_str());
	Serial.print("]");
	Serial.print("[");
	Serial.print(WIFI_PASS.c_str());
	Serial.print("]");
	WiFi.begin(WIFI_SSID.c_str(), WIFI_PASS.c_str());
	int i = 0;
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		i++;
		if (i > 20) {
			Serial.println("Could not connect to WiFi");
			return false;
		}
		Serial.print(".");
	}
	return true;
}

void setup()
{
	Serial.begin(115200);
}

void loop() 
{
	pinMode(DISP_PWR_1, OUTPUT);
	digitalWrite(DISP_PWR_1, LOW);

	boolean connected = connectWifi();
	uint8_t dataOk= updateData();

	digitalWrite(DISP_PWR_1, HIGH);
	delay(100);

	if (connected && dataOk)
	{
		display.init();
		drawAll();
		display.hibernate();	
		Serial.println("Going to sleep");
	}
	else
	{
		display.init();
		display.setRotation(1);
		display.setFont(&FreeSansBold9pt7b);
		display.setTextColor(GxEPD_BLACK);

		display.setFullWindow();
		display.firstPage();
		do
		{
			display.fillScreen(GxEPD_WHITE);
			display.setCursor(0, 12);
			display.print("Qualcosa non funziona :(");

		} while (display.nextPage());
		display.hibernate();
	}
	delay(100);
	digitalWrite(DISP_PWR_1, LOW);
	ESP.deepSleep(UPDATE_INTERVAL_SECS * 1000000);
	Serial.println("WAKE!");
};

void drawAll()
{
	display.setRotation(1);
	display.setFont(&FreeSansBold9pt7b);
	display.setTextColor(GxEPD_BLACK);

	display.setFullWindow();
	display.firstPage();
	do
	{
		display.fillScreen(GxEPD_WHITE);
		drawHeader();
		drawForecast();

	} while (display.nextPage());
}

void drawHeader()
{
	char *dstAbbrev;
	char temp[30];
	time_t now = dstAdjusted.time(&dstAbbrev);
	struct tm * timeinfo = localtime(&now);
	String date = ctime(&now);
	
	date = date.substring(0, 11) + String(1900 + timeinfo->tm_year);
	sprintf(temp, "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);

	display.setCursor(0, 12);
	display.print(String(temp));

	display.setCursor(55, 12);
	display.drawCircle(77, 2, 2, GxEPD_BLACK);
	sprintf(temp, "%2.0f C", conditions.temp);
	display.print(temp);

	display.setCursor(105, 12);
	display.print(conditions.description);

	display.drawLine(0, 19, 297, 19, GxEPD_BLACK);
}

void drawForecast()
{
	uint8_t vert_pos=16;
	time_t now = dstAdjusted.time(nullptr);
	struct tm * timeinfo = localtime(&now);

	unsigned int curHour = timeinfo->tm_hour;
	if (timeinfo->tm_min > 29) curHour = hourAddWrap(curHour, 1);

	for (int _forecast = 0; _forecast < MAX_FORECASTS; _forecast++)
	{
		time_t observation = forecasts[_forecast].observationTime + dstOffset;
		struct tm* observationTm = localtime(&observation);

		display.setCursor((_forecast) * 80, 32+ vert_pos);
		display.setFont(&FreeSansBold9pt7b);
		display.print(String(observationTm->tm_hour) + ":00");

		display.setCursor((_forecast)* 80, 64+16+ vert_pos);
		display.setFont(&meteocons24pt7b);
		display.print(forecasts[_forecast].iconMeteoCon);	

		display.setCursor((_forecast) * 80 + 5, 64 + 16+ 20+ vert_pos);
		display.setFont(&FreeSansBold9pt7b);
		display.print(String(forecasts[_forecast].rain*100,0)+ "%");
	}
}

unsigned int hourAddWrap(unsigned int hour, unsigned int add) {
	hour += add;
	if (hour > 23) hour -= 24;
	return hour;
}

// Update the internet based information and update screen
uint8_t updateData() 
{
	configTime(UTC_OFFSET * 3600, 0, NTP_SERVERS);
	OpenWeatherMapCurrent *conditionsClient = new OpenWeatherMapCurrent();
	conditionsClient->setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
	conditionsClient->setMetric(IS_METRIC);
	Serial.println("\nAbout to call OpenWeatherMap to fetch station's current data...");
	conditionsClient->updateCurrentById(&conditions, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID);
	delete conditionsClient;
	conditionsClient = nullptr;

	OpenWeatherMapForecast *forecastsClient = new OpenWeatherMapForecast();
	forecastsClient->setMetric(IS_METRIC);
	forecastsClient->setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
	foundForecasts = forecastsClient->updateForecastsById(forecasts, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID, MAX_FORECASTS);
	delete forecastsClient;
	forecastsClient = nullptr;

	// Wait max. 3 seconds to make sure the time has been sync'ed
	Serial.println("\nWaiting for time");
	time_t now;
	uint32_t startTime = millis();
	uint16_t ntpTimeoutMillis = NTP_SYNC_TIMEOUT_SECONDS * 1000;
	while ((now = time(nullptr)) < NTP_MIN_VALID_EPOCH) {
		uint32_t runtimeMillis = millis() - startTime;
		if (runtimeMillis > ntpTimeoutMillis) {
			Serial.printf("\nFailed to sync time through NTP. Giving up after %dms.\n", runtimeMillis);
			return 0;
			break;
		}
		Serial.println(".");
		delay(300);
	}

	dstOffset = UTC_OFFSET * 3600 + dstAdjusted.time(nullptr) - now;
	Serial.printf("Current time: %d\n", now);
	Serial.printf("UTC offset: %d\n\n", dstOffset);
	return 1;
}
