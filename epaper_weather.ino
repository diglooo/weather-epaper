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
BUSY		GPIO5	D1		
RST			GPIO4	D2
DC			GPIO15	D8
CS			GPIO0	D3
CLK			GPIO14	D5 (HSCLK)
DIN			GPIO13	D7 (HMOSI)
GND			GND
VCC			3V3

In order to wake from deep sleep, you must connect D0 to RST on the ESP8266 board.
*/

#include <u8g2_fonts.h>
#include <Arduino.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
//#include <simpleDSTadjust.h>
#include <GxEPD2_BW.h>
//#include <GxEPD2_3C.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <JsonListener.h>
#include <OpenWeatherMapCurrent.h>
#include <OpenWeatherMapForecast.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "meteocons24pt7b.h"
#include "settings.h"


#define DISP_PWR_1 D6

//SET ESP INTERNAL ADC TO READ BATTERY VOLTAGE
ADC_MODE(ADC_VCC);

GxEPD2_BW<GxEPD2_290_T94, GxEPD2_290_T94::HEIGHT> display(GxEPD2_290_T94(/*CS*/ D3, /*DC*/ D8, /*RST*/ D2, /*BUSY*/ D1));
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

OpenWeatherMapCurrentData conditions;
OpenWeatherMapForecastData forecasts[MAX_FORECASTS];
//simpleDSTadjust dstAdjusted(StartRule, EndRule);

uint8_t foundForecasts = 0;
uint16_t VCC = 0;

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
	
}

void loop() 
{
	Serial.begin(115200);
	if (setenv("TZ", "CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00", 1) != 0) Serial.println("setenv error");
	tzset();

	u8g2Fonts.begin(display); // connect u8g2 procedures to Adafruit GFX

	pinMode(DISP_PWR_1, OUTPUT);
	digitalWrite(DISP_PWR_1, HIGH);

	delay(100);
	VCC=ESP.getVcc();
	Serial.print("Vcc="); Serial.println(VCC);
	
	boolean connected = connectWifi();
	uint8_t dataOk= updateData();

	if (connected && dataOk)
	{
		display.init();
		drawAll();
		display.hibernate();	
	}
	
	delay(50);
	digitalWrite(DISP_PWR_1, LOW);
	ESP.deepSleep(UPDATE_INTERVAL_SECS * 1000000);
};

void drawAll()
{
	display.setRotation(1);
	u8g2Fonts.setForegroundColor(GxEPD_BLACK);
	u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
	display.setTextColor(GxEPD_BLACK);

	display.setFullWindow();
	display.firstPage();
	do
	{
		drawHeader();
		drawForecast();
	} while (display.nextPage());
}

void drawHeader()
{
	char temp[64];
	time_t now = time(nullptr);
	struct tm * timeinfo = localtime(&now);
	sprintf(temp, "%02d:%02d %2.0f%c %s", timeinfo->tm_hour, timeinfo->tm_min, conditions.temp, 0xb0, conditions.description.c_str());

	uint8_t wifiSignal = getWifiQuality();
	uint8_t battLevel = getBatteryLevel(VCC);

	u8g2Fonts.setFont(u8g2_font_helvB12_tf);
	u8g2Fonts.setCursor(0, 15);
	u8g2Fonts.print(temp);

	u8g2Fonts.setCursor(287, 19);
	u8g2Fonts.setFont(u8g2_font_battery19_tn);
	u8g2Fonts.print(battLevel);

	u8g2Fonts.setCursor(285, 16);
	u8g2Fonts.setFont(u8g2_font_helvB08_tf);
	u8g2Fonts.setFontDirection(3);

	sprintf(temp, "%d/5", wifiSignal);
	u8g2Fonts.print(temp);

	display.drawLine(0, 20, 297, 20, GxEPD_BLACK);
}

void drawForecast()
{
	uint8_t vert_pos=28;
	uint8_t needPrintRain = 0;
	String printText;
	char temp[6];

	//forecasts[2].rain = 0.3;

	for (int _forecast = 0; _forecast < MAX_FORECASTS; _forecast++)
	{
		if (forecasts[_forecast].rain > 0)
		{
			needPrintRain = 1;
			vert_pos = 16;
		}
	}

	for (int _forecast = 0; _forecast < MAX_FORECASTS; _forecast++)
	{
		//int xx = ((_forecast + 1) * 74) - 37;	
		//display.drawLine(xx, 0, xx, 128, GxEPD_BLACK);

		u8g2Fonts.setFontDirection(0);

		//print forecast time
		time_t forecastTtime = forecasts[_forecast].observationTime;
		struct tm * timeinfo = localtime(&forecastTtime);
		//printText = String(timeinfo->tm_hour);
		sprintf(temp, "%02d", timeinfo->tm_hour);

		u8g2Fonts.setFont(u8g2_font_logisoso20_tr);
		int16_t tw =u8g2Fonts.getUTF8Width(temp);
		u8g2Fonts.setCursor(((_forecast+1) * 74)-37-tw/2, 32 + vert_pos);	
		u8g2Fonts.print(temp);

		//ptiny icon
		display.setFont(&meteocons24pt7b);
		int16_t x1, y1;
		uint16_t w, h;
		display.getTextBounds(forecasts[_forecast].iconMeteoCon, 0, 0,&x1, &y1, &w, &h);
		//display.setCursor((_forecast) * 80, 64 + 16 + vert_pos);
		if (w < 24) w=40;
		display.setCursor( ((_forecast + 1) * 74) - 37 - w / 2, 64 + 16 + vert_pos);
		display.print(forecasts[_forecast].iconMeteoCon);	
		
		//print rain qty
		if (forecasts[_forecast].rain > 0 && needPrintRain)
		{
			printText = String(forecasts[_forecast].rain, 1) + "mm";
			u8g2Fonts.setFont(u8g2_font_helvB12_tf);
			int16_t tw = u8g2Fonts.getUTF8Width(printText.c_str());
			u8g2Fonts.setCursor(((_forecast + 1) * 74) - 37 - tw / 2, 64 + 16 + 24 + vert_pos);
			u8g2Fonts.print(printText);
		}
	}
}

// Update the internet based information and update screen
uint8_t updateData() 
{
	//request OWM data
	OpenWeatherMapCurrent *conditionsClient = new OpenWeatherMapCurrent();
	conditionsClient->setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
	conditionsClient->setMetric(IS_METRIC);
	conditionsClient->updateCurrentById(&conditions, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID);
	delete conditionsClient;
	conditionsClient = nullptr;

	OpenWeatherMapForecast *forecastsClient = new OpenWeatherMapForecast();
	forecastsClient->setMetric(IS_METRIC);
	forecastsClient->setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
	foundForecasts = forecastsClient->updateForecastsById(forecasts, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID, MAX_FORECASTS);
	delete forecastsClient;
	forecastsClient = nullptr;

	Serial.print("observationTime=");
	Serial.println(conditions.observationTime);
	Serial.print("timeZone=");
	Serial.println(conditions.timeZone);

	//Set ESP time based on timestamp from observation
	time_t rtc = conditions.observationTime;
	timeval tv = { rtc, 0 };
	timezone tz = { UTC_OFFSET, 0 };
	settimeofday(&tv, &tz);

	char temp[64];
	time_t now = time(nullptr);
	struct tm * timeinfo = localtime(&now);
	sprintf(temp, "%02d:%02d dst:%d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_isdst);
	Serial.println(temp);
	
	return 1;
}

// converts the dBm to a range between 0 and 100%
int8_t getWifiQuality() {
	int32_t dbm = WiFi.RSSI();
	if (dbm > -60) 	return 5;
	if (dbm > -70) 	return 3;
	if (dbm > -80) 	return 2;
	if (dbm > -90) 	return 1;
	if (dbm <= -90) return 0;
}

// converts the dBm to a range between 0 and 100%
int8_t getBatteryLevel(uint16_t v) {
	return map(v, 2500, 3000, 0, 5);
}
