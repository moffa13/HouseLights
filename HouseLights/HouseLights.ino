﻿#include <GDBStub.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include "defs.h"
#include "WifiUtils.h"

/* Comment this to set a manual timezone & set timezone_offset to whatever timezone used in your country
Otherwise, set up a free Google Time Zone API here https://developers.google.com/maps/documentation/timezone/get-api-key
and change the key of google_api_key
*/
#define USE_TIMEZONE_API
static int timezone_offset = 1; // Offset to apply from NTP's time, 1 is unset, correct offset is multiple of 3600

// Change these parameters to fit with your iot device
#define RELAY_PIN 0
#define LED_PIN 2

static const char* const wifi_ssid = "YOUR_SSID";
static const char* const wifi_password = "YOUR_PASSWORD";
static const char* const latitude = "YOUR_LATITUDE";
static const char* const longitude = "YOUR_LONGITUDE";
static const char* const api_sunrise_host = "api.sunrise-sunset.org";
static const uint8_t api_sunrise_fingerprint[20] = { 0x21, 0x86, 0x08, 0x8F, 0x39, 0xC3, 0x12, 0x0E, 0xEB, 0x0F, 0x76, 0x5E, 0x07, 0x36, 0xF1, 0x0D, 0x4B, 0x27, 0x8A, 0x3B };
static const uint8_t google_api_fingerprint[20] = { 0x43, 0x2F, 0x75, 0x94, 0xFB, 0x9C, 0x00, 0xAB, 0xEE, 0x26, 0x22, 0x61, 0x57, 0x50, 0xBF, 0xB4, 0xD2, 0x05, 0x85, 0xC7 };

static unsigned long lastCheckTime = 0;
static unsigned long lastTimezoneCheckTime = 0;
static unsigned int loopRefreshInterval = 3600000;
static const uint16_t max_google_timezone_retries = 5;
static uint16_t google_timezone_retries = 0;
static short lightsState = -1; // -1 auto, 0 off, 1 on
static bool sunset_ok = false;
static bool autoDebug = false; // If true, auto shows printInfos method 

/* Interval where the lights are always on.
Normally, when we reach the next day, the sun is not supposed to be down
so the lights shut off */
static unsigned int power_min_sec = 0;
// Tell for how many seconds the lights should keep being on after midnight
static unsigned int power_max_sec = 0;
// When the sunrise did not happen yet, min second when to power on the lights until sunrise
static unsigned int power_morning_min = 0;

/* Time to set the lights on before the "official" sunset
Set it to 0 to power on the lights exactly when the sunset happens
*/
static int sunset_warn = 0;
static char* google_api_key;

static time_si parsedSunset;
static time_si parsedSunrise;
static time_si now_time;
static WiFiUDP ntpUDP;
static NTPClient timeClient(ntpUDP);

/* Change these infos to fit your needs*/
static const IPAddress dns(8, 8, 8, 8);
static const IPAddress localIP(192, 168, 0, 20);
static const IPAddress gateway(192, 168, 0, 1);
static const IPAddress subnet(255, 255, 255, 0);
static ESP8266WebServer apiServer(125);

bool mustBeOn(time_si const& now, time_si const& sunset, bool print, String* str = nullptr);
void printWifiSignalStrength(String* str = nullptr);
void printInfos(bool toSerial = true, String* str = nullptr);

double fmod(double x, double y) {
	return x - y * floor(x / y);
}

String getValue(String const& data, char separator, int index) {

	int currentIndex{ 0 };
	int found{ 0 };

	for (int i{ 0 }; i < data.length(); ++i) {
		if (data.charAt(i) == separator) {
			if (index == found) {
				return data.substring(currentIndex, i);
			}
			currentIndex = i + 1;
			found++;
		}
	}

	return data.substring(currentIndex, data.length());
}

time_si parseTimeH24(String const& time) {

	time_si t;
	t.hour = static_cast<byte>(atoi(getValue(time, ':', 0).substring(0, 2).c_str()));

	if (time.endsWith("PM") && t.hour != 12) { // Convert to 24 hour
		t.hour += 12;
	}
	else if (time.endsWith("AM") && t.hour == 12) {
		t.hour = 0;
	}

	t.minute = static_cast<byte>(atoi(getValue(time, ':', 1).c_str()));
	t.second = static_cast<byte>(atoi(getValue(time, ':', 2).c_str()));

	return t;
}

bool setTimezone() {
	if (google_api_key[0] != 0 && millis() > lastTimezoneCheckTime + loopRefreshInterval || lastTimezoneCheckTime == 0) {
#ifdef USE_TIMEZONE_API	
		google_timezone_result timezone_result;
		do {
			checkWiFi();
			timezone_result = getTimezone();

			if (timezone_result.error) {
				google_timezone_retries++;
#ifndef _DEBUG
				Serial.println("Failed retrieving timezone, retrying in 1 second");
#endif
				delay(1000);
			}
		} while (timezone_result.error && google_timezone_retries != max_google_timezone_retries); // Retry if google timezone returned an error
#endif
		if (!timezone_result.error) {
			Serial.println("2");
			if (timezone_offset != timezone_result.timezone) {
				timeClient.setTimeOffset(timezone_result.timezone);
#ifndef _DEBUG
				Serial.println(String{ "Set Timezone to " } +timezone_result.timezone + "s");
#endif
				timezone_offset = timezone_result.timezone;
				return true;
			}
			else {
#ifndef _DEBUG
				Serial.println(String{ "Timezone did not change (" } +timezone_offset + "s)");
#endif
				return false;
			}
		}
		google_timezone_retries = 0;
		lastTimezoneCheckTime = millis();
	}
	else if (google_api_key[0] == 0) {
#ifndef _DEBUG
		Serial.println("Warning, google_api_key not set");
#endif
	}
}

void setup() {

	gdbstub_init();

#ifndef _DEBUG
	Serial.begin(9600);
#endif

	while (!Serial) {
		;; // Wait for serial working
	}

	EEPROM.begin(4096);

	google_api_key = static_cast<char*>(malloc(40));
	memset(google_api_key, 0, 40);

	readMemory();

	WiFi.setAutoReconnect(true);

	pinMode(RELAY_PIN, OUTPUT);
	pinMode(LED_PIN, OUTPUT);
	digitalWrite(RELAY_PIN, LOW);
	digitalWrite(LED_PIN, HIGH);

	WiFi.mode(WIFI_STA);

	checkWiFi();

	timeClient.begin();

	setTimezone();

	registerApiServerRequests();

	apiServer.begin();

#ifndef _DEBUG
	Serial.println("Ready.");
#endif
}

void readMemory() {
	// power_min_sec 0 -> 31
	// power_max_sec 32 -> 63
	// sunset_warn 64 -> 95
	// google_api_key 96 -> 134
	// power_morning_min 135 -> 166
	EEPROM.get(0, power_min_sec);
	EEPROM.get(32, power_max_sec);
	EEPROM.get(64, sunset_warn);
	for (uint16_t i = 0; i < 39; ++i) {
		google_api_key[i] = EEPROM.read(96 + i);
	}
	EEPROM.get(135, power_morning_min);
}

void wipeMemory() {
	power_min_sec = 0;
	power_max_sec = 0;
	power_morning_min = 0;
	sunset_warn = 0;
	memset(google_api_key, 0, 40);
	for (uint16_t i = 0; i < 166; ++i)
		EEPROM.write(i, 0);
	EEPROM.commit();
}

void updatePowerMinSec(unsigned int value) {
	if (value == power_min_sec) return;
	power_min_sec = value;
	EEPROM.put(0, power_min_sec);
	EEPROM.commit();
}

void updatePowerMaxSec(unsigned int value) {
	if (value == power_max_sec) return;
	power_max_sec = value;
	EEPROM.put(32, power_max_sec);
	EEPROM.commit();
}

void updatePowerMorningMinSec(unsigned int value) {
	if (value == power_morning_min) return;
	power_morning_min = value;
	EEPROM.put(135, power_morning_min);
	EEPROM.commit();
}

void updateSunsetWarn(int value) {
	if (value == sunset_warn) return;
	sunset_warn = value;
	EEPROM.put(64, sunset_warn);
	EEPROM.commit();
}

void updateGoogleApiKey(const char* const value) {
	if (memcmp(value, google_api_key, 39) == 0) return;
	for (uint16_t i = 0; i < 39; ++i) {
		google_api_key[i] = value[i];
		EEPROM.write(96 + i, value[i]);
	}
	EEPROM.commit();
}

void registerApiServerRequests() {

	apiServer.onNotFound([]() {
		apiServer.send(404, "text/html", "REQUEST DOES NOT EXIST");
		});

	apiServer.on("/set", HTTPMethod::HTTP_POST, []() {
		bool error = false;
		if (apiServer.hasArg("value")) {
			int8_t value = atoi(apiServer.arg("value").c_str());
			switch (value) {
			case -1:
			case 0:
			case 1:
				break;
			default:
				error = true;
			}
			if (!error) {
				lightsState = value;
				updateLightsState();
				apiServer.send(200, "text/html", "OK");
			}
			else {
				apiServer.send(400, "text/html", "ERROR BAD VALUE PROVIDED");
			}
		}
		else {
			apiServer.send(400, "text/html", "ERROR NO VALUE PROVIDED");
		}
		});

	apiServer.on("/get", []() {
		apiServer.send(200, "text/html", String(lightsState));
		});

	apiServer.on("/get_real", []() {
		if (lightsState != -1)
			apiServer.send(200, "text/html", String(lightsState));
		else
			apiServer.send(200, "text/html", mustBeOn(now_time, parsedSunset, false) ? "1" : "0");

		});

	apiServer.on("/param/set", HTTPMethod::HTTP_POST, []() {
		bool error = false;
		if (apiServer.hasArg("param_name") && apiServer.hasArg("param_value")) {
			String const param_name = apiServer.arg("param_name");
			if (param_name == "power_min") {
				updatePowerMinSec(atoi(apiServer.arg("param_value").c_str()));
			}
			else if (param_name == "power_max") {
				updatePowerMaxSec(atoi(apiServer.arg("param_value").c_str()));
			}
			else if (param_name == "power_morning_min") {
				updatePowerMorningMinSec(atoi(apiServer.arg("param_value").c_str()));
			}
			else if (param_name == "sunset_warn") {
				updateSunsetWarn(atoi(apiServer.arg("param_value").c_str()));
			}
			else if (param_name == "google_api_key") {
				updateGoogleApiKey(apiServer.arg("param_value").c_str());
				lastTimezoneCheckTime = 0;
			}
			else {
				error = true;
			}
		}
		else {
			error = true;
		}

		if (!error) {
			apiServer.send(200, "text/html", "OK");
		}
		else {
			apiServer.send(400, "text/html", "ERROR BAD PARAM NAME PROVIDED");
		}
		});

	apiServer.on("/param/get", []() {
		bool error = false;
		if (apiServer.hasArg("param_name")) {
			String const param_name = apiServer.arg("param_name");
			if (param_name == "power_min") {
				apiServer.send(200, "text/html", String(power_min_sec));
			}
			else if (param_name == "power_max") {
				apiServer.send(200, "text/html", String(power_max_sec));
			}
			else if (param_name == "power_morning_min") {
				apiServer.send(200, "text/html", String(power_morning_min));
			}
			else if (param_name == "sunset_warn") {
				apiServer.send(200, "text/html", String(sunset_warn));
			}
			else if (param_name == "google_api_key") {
				apiServer.send(200, "text/html", google_api_key);
			}
			else {
				error = true;
			}
			if (error) {
				apiServer.send(400, "text/html", "ERROR UNKNOWN PARAMETER");
			}
		}
		else {
			apiServer.send(400, "text/html", "ERROR");
		}
		});

	apiServer.on("/infos", []() {
		String coreinfos;
		printInfos(false, &coreinfos);
		apiServer.send(200, "text/html", coreinfos);
		});

	apiServer.on("/reboot", []() {
		apiServer.send(200, "text/html", "REBOOTING...");
		delay(1000);
		ESP.restart();
		});

	apiServer.on("/refresh", []() {
		lastCheckTime = 0;
		apiServer.send(200, "text/html", "refreshed sunset check...");
		});

	apiServer.on("/reset_timezone", []() {
		lastTimezoneCheckTime = 0;
		apiServer.send(200, "text/html", "refreshed timezone check...");
		});

	apiServer.on("/refresh_all", []() {
		lastTimezoneCheckTime = 0;
		lastCheckTime = 0;
		apiServer.send(200, "text/html", "refreshed timezone & sunset check...");
		});

	apiServer.on("/wipe", HTTPMethod::HTTP_DELETE, []() {
		wipeMemory();
		lastCheckTime = 0;
		lastTimezoneCheckTime = 0;
		apiServer.send(200, "text/html", "OK");
		});
}

String http_call(String host, String url, ushort port = 0, bool ssl = true, const uint8_t* fingerprint = nullptr) {

	BearSSL::WiFiClientSecure client;

	if (ssl) {
		client.setFingerprint(fingerprint);
	}

	ushort _port{ port == 0 ? (ssl ? 443 : 80) : port };

	client.connect(host.c_str(), _port);

	if (!client.connected()) {
#ifndef _DEBUG
		Serial.println(String{ "Could not connect to host " } +host + ":" + _port);
#endif
		return "";
	}

	client.print(String("GET ") + url + " HTTP/1.0\r\n" +
		"Host: " + host + "\r\n" +
		"User-Agent: ESP8266\r\n" +
		"Connection: close\r\n\r\n");

	bool isContent{ false };

	String line;

	while (client.available()) {
		String tmp{ client.readStringUntil('\n') };
		if (isContent) {
			line += tmp;
		}
		else if (tmp == "\r") {
			isContent = true;
		}
	}

	return line;

}

// Returns only when connected to WiFi
// Enable the LED_PIN when connected and disable it while connecting
void checkWiFi() {
	if (WifiUtils::isWifiConnected()) return;
	digitalWrite(LED_PIN, HIGH);
	bool connected{ false };
	do {
		connected = WifiUtils::connect(wifi_ssid, wifi_password, true, 15, &localIP, &gateway, &subnet, &dns);
		if (!connected) {
#ifndef _DEBUG
			Serial.println("Could not connnect to WiFi, retrying.");
#endif
		}
	} while (!connected);

	digitalWrite(LED_PIN, LOW); // Obvious
#ifndef _DEBUG
	Serial.println("Connected to WiFi.");
#endif
}

void update_time_from_ntp() {
	timeClient.update();
	now_time.hour = timeClient.getHours();
	now_time.minute = timeClient.getMinutes();
	now_time.second = timeClient.getSeconds();
}

/*
* Change the time to work with the timezone
*/
void adaptTimezone(time_si& time) {
	float timezoneHourf{ timezone_offset / 3600.0f };
	byte timezoneHour{ static_cast<byte>(timezoneHourf) };
	byte timezoneMinute{ static_cast<byte>(fmod(timezoneHourf, 1) * 60) };
	time.hour += timezoneHour;
	time.minute += timezoneMinute;

	if (time.minute > 59) {
		time.minute -= 60;
		time.hour += 1;
	}

	if (time.hour > 23) time.hour = time.hour - 24;
}

void loop() {

	update_time_from_ntp();

	// Check the new sunset every <loopRefreshInterval>ms
	if (millis() > lastCheckTime + loopRefreshInterval || lastCheckTime == 0) {

		checkWiFi();

		// If timezone's changed, update the time
		if (setTimezone()) {
			update_time_from_ntp();
		}
		time_safe const sunset = getApiSunrise();

		if (sunset.no_error) {

			sunset_ok = true;

#ifndef _DEBUG
			if (autoDebug)
				Serial.println("Successfully retrieved sunset hour");
#endif
			parsedSunset = sunset.sunset;
			parsedSunrise = sunset.sunrise;

			adaptTimezone(parsedSunset);
			adaptTimezone(parsedSunrise);



			// Update last check time only if it succeeds
			lastCheckTime = millis();
		}
		else {
#ifndef _DEBUG
			Serial.println("Didn't retrieve sunset hour");
#endif
		}

		if (autoDebug)
			printInfos();

	}

	apiServer.handleClient();

	updateLightsState();

	yield();

}

void updateLightsState() {
	if (sunset_ok || lightsState != -1) {
		if (lightsState == 1 || (lightsState == -1 && mustBeOn(now_time, parsedSunset, false))) {
			digitalWrite(RELAY_PIN, LOW);
		}
		else {
			digitalWrite(RELAY_PIN, HIGH);
		}
	}
}

String IpAddress2String(const IPAddress& ipAddress) {
	return String(ipAddress[0]) + "." + \
		ipAddress[1] + "." + \
		ipAddress[2] + "." + \
		ipAddress[3];
}

void printWifiSignalStrength(String* str) {
	String infos;
	if (WiFi.status() == WL_CONNECTED) {
		infos += "WiFi is connected\n";
		int32_t rssi{ WiFi.RSSI() };
		infos += String("WiFi signal strength : ") + rssi + "dBm\n";
		infos += String("Local IP address is : ") + IpAddress2String(WiFi.localIP()) + "\n";
	}
	else {
		infos += "No WiFi connection";
	}

	if (str != nullptr) {
		(*str) += infos;
	}
}

void printInfos(bool toSerial, String* str) {
	String infos;
	printWifiSignalStrength(&infos);
	infos += "Module MAC is : " + WiFi.macAddress() + "\n";
	infos += String{ "Timezone : " } +timezone_offset + "s\n";
	infos += String{ "Current time : " } +timeClient.getFormattedTime() + "\n";
	infos += String{ "Sunset is at : " } +parsedSunset.hour + ":" + parsedSunset.minute + ":" + parsedSunset.second + "\n";
	infos += String{ "Sunrise is at : " } +parsedSunrise.hour + ":" + parsedSunrise.minute + ":" + parsedSunrise.second + "\n";
	infos += String{ "Warn defined to : " } +sunset_warn + "s" + "\n";
	infos += String{ "Auto on between " } +power_min_sec + "s and " + power_max_sec + "s\n";
	infos += String{ "API key is " } +google_api_key + "\n";
	infos += String{ "Free heap : " } +ESP.getFreeHeap() + "\n";
	if (lightsState != -1)
		infos += String{ "Warning : Lights are not set to automatic (" } +static_cast<int>(lightsState) + ") !\n";

	mustBeOn(now_time, parsedSunset, true, &infos);
	if (toSerial) {
#ifndef _DEBUG
		Serial.println(infos);
#endif
	}
	else if (str != nullptr) {
		(*str) += infos;
	}

}

int getTimeSecond(time_si const& time) {
	return time.hour * 60 * 60 + time.minute * 60 + time.second;
}

String getSecondToTime(unsigned int totalSeconds) {
	const unsigned int hours{ totalSeconds / 3600 };
	const uint8_t minutes{ static_cast<uint8_t>((totalSeconds - hours * 3600) / 60) };
	const uint8_t seconds{ static_cast<uint8_t>(totalSeconds - hours * 3600 - minutes * 60) };

	return String{ hours } +"h " + minutes + "min " + seconds + "sec";
}

bool mustBeOn(time_si const& now, time_si const& sunset, bool print, String* str) {

	int sunset_time = getTimeSecond(sunset);
	int now_time = getTimeSecond(now);
	int sunset_in_s = sunset_time - now_time; // if positive, sunset did not happen yet, if negative, sunset happend and next is tomorrow
	String infos;

	if (print) {
		if (sunset_in_s >= 0) {
			infos += String{ "Sunset in : " } +getSecondToTime(sunset_in_s) + "\n";
		}
		else {
			infos += String{ "Sunset happened " } +getSecondToTime(-sunset_in_s) + " ago\n";
		}
	}

	if (now_time >= power_min_sec && now_time <= power_max_sec) return true;

	if (sunset_in_s < sunset_warn) {
		if (sunset_warn > 0) {
			if (print)
				infos += String{ "Warn of " } +getSecondToTime(sunset_warn) + " before sunset happened\n";
		}
		else {
			if (print)
				infos += String{ "Warn of " } +getSecondToTime(-sunset_warn) + " after sunset happened\n";
		}
		if (print) {
			if (str != nullptr) {
				(*str) += infos;
			}
			else {
#ifndef _DEBUG
				Serial.println(infos);
#endif
			}
		}
		return true;
	}
	else if (power_morning_min != 0 && now_time >= power_morning_min && now_time < getTimeSecond(parsedSunrise)) {
		infos += String{ "Morning mode before sunrise\n" };
		return true;
	}
	else {
		if (print) {
			infos += "No Warn\n";
			if (str != nullptr) {
				(*str) += infos;
			}
			else {
#ifndef _DEBUG
				Serial.println(infos);
#endif
			}
		}
		return false;
	}
}

google_timezone_result getTimezone() {

	google_timezone_result res{ false, 0, 1 };

	const char* host = "maps.googleapis.com";

	String url{ String{"/maps/api/timezone/json?location="} +latitude + "," + longitude + "&timestamp=" + timeClient.getEpochTime() + "&key=" + google_api_key };

	String content{ http_call(host, url, 0, true, google_api_fingerprint) };

	StaticJsonDocument<800> jsonDoc;
	auto error = deserializeJson(jsonDoc, content.c_str(), DeserializationOption::NestingLimit(10));

#ifndef _DEBUG
	if (autoDebug) {
		Serial.println(url);
		Serial.println(content);
	}
#endif

	if (error || strcmp(jsonDoc["status"], "OK") != 0) {
#ifndef _DEBUG
		Serial.println("Error parsing Google api JSON");
		Serial.println(jsonDoc["status"].as<String>());
#endif
		res.error = true;
		if (jsonDoc["status"] == "INVALID_REQUEST") {
			res.error_type = 1;
		}
		else if (jsonDoc["status"] == "OVER_DAILY_LIMIT") {
			res.error_type = 2;
		}
		else if (jsonDoc["status"] == "OVER_QUERY_LIMIT") {
			res.error_type = 3;
		}
		else if (jsonDoc["status"] == "REQUEST_DENIED") {
			res.error_type = 4;
		}
		else if (jsonDoc["status"] == "UNKNOWN_ERROR") {
			res.error_type = 5;
		}
		else if (jsonDoc["status"] == "ZERO_RESULTS") {
			res.error_type = 6;
		}

	}
	else if (autoDebug) {
#ifndef _DEBUG
		Serial.println("Successfully received from Google Maps API");
#endif
	}

	res.timezone = jsonDoc["dstOffset"].as<int>() + jsonDoc["rawOffset"].as<int>();

	return res;
}

time_safe getApiSunrise() {
	String url{ String{"/json?lat="} +latitude + "&lng=" + longitude + "&date=today" };

	String content{ http_call(api_sunrise_host, url, 0, true, api_sunrise_fingerprint) };

#ifndef _DEBUG
	if (autoDebug) {
		Serial.println(content);
	}
#endif

	StaticJsonDocument<800> jsonDoc;
	auto error = deserializeJson(jsonDoc, content.c_str(), DeserializationOption::NestingLimit(10));

	time_safe t;
	t.no_error = false;

	if (!error && strcmp(jsonDoc["status"], "OK") == 0) {
		t.no_error = true;
		String sunset = jsonDoc["results"]["sunset"];
		String sunrise = jsonDoc["results"]["sunrise"];
		t.sunset = parseTimeH24(sunset);
		t.sunrise = parseTimeH24(sunrise);
	}

	return t;
}