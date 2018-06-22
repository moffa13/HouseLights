﻿#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <WiFiServer.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
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

static unsigned long lastCheckTime = 0;
static unsigned long lastTimezoneCheckTime = 0;
static unsigned int loopRefreshInterval = 1200000;
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

/* Time to set the lights on before the "official" sunset
Set it to 0 to power on the lights exactly when the sunset happens
*/
static int sunset_warn = 0;
static char * google_api_key;

static time_si parsedSunset;
static time_si now_time;
static WiFiUDP ntpUDP;
static NTPClient timeClient(ntpUDP);

/* Change these infos to fit your needs*/
static const IPAddress dns(8, 8, 8, 8);
static const IPAddress localIP(192, 168, 0, 227);
static const IPAddress gateway(192, 168, 0, 1);
static const IPAddress subnet(255, 255, 255, 0);
static ESP8266WebServer apiServer(8086);

bool mustBeOn(time_si const& now, time_si const& sunset, bool print, String *str = nullptr);
void printWifiSignalStrength(String *str = nullptr);
void printInfos(bool toSerial = true, String *str = nullptr);

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
		bool processOK;
		google_timezone_result timezone_result;
		do {
			bool successUpdate;
			checkWiFi();
			successUpdate = timeClient.forceUpdate();
			
			if (successUpdate) {
				timezone_result = getTimezone();
			}
			processOK = successUpdate && timezone_result.error == false;
			if (!processOK) {
				if(timezone_result.error)
					google_timezone_retries++;
				Serial.println("Failed retrieving timezone, retrying in 1 second");
				delay(1000);
			}
		} while (!processOK && google_timezone_retries != max_google_timezone_retries); // Retry if ntp did not update correctly or google timezone returned an error
#endif
		if (google_timezone_retries != max_google_timezone_retries) {
			if (timezone_offset != timezone_result.timezone) {
				timeClient.setTimeOffset(timezone_result.timezone);
				Serial.println(String{ "Set Timezone to " } + timezone_result.timezone + "s");
				timezone_offset = timezone_result.timezone;
				return true;
			}
			else {
				Serial.println(String{ "Timezone did not change (" } +timezone_offset + "s)");
				return false;
			}
		}
		google_timezone_retries = 0;
		lastTimezoneCheckTime = millis();
	}
	else if (google_api_key[0] == 0) {
		Serial.println("Warning, google_api_key not set");
	}
}

void setup() {

	Serial.begin(9600);
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
	digitalWrite(RELAY_PIN, HIGH);
	digitalWrite(LED_PIN, HIGH);

	WiFi.mode(WIFI_STA);

	checkWiFi();

	timeClient.begin();

	setTimezone();

	registerApiServerRequests();

	apiServer.begin();

	Serial.println("Ready.");
}

void readMemory() {
	// power_min_sec 0 -> 31
	// power_max_sec 32 -> 63
	// sunset_warn 64 -> 95
	// google_api_key 96 -> 134
	EEPROM.get(0, power_min_sec);
	EEPROM.get(32, power_max_sec);
	EEPROM.get(64, sunset_warn);
	for (uint16_t i = 0; i < 39; ++i) {
		google_api_key[i] = EEPROM.read(96 + i);
	}
}

void wipeMemory() {
	power_min_sec = 0;
	power_max_sec = 0;
	sunset_warn = 0;
	memset(google_api_key, 0, 40);
	for (uint16_t i = 0; i < 135; ++i)
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
				lastCheckTime = 0;
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

	apiServer.on("/wipe", HTTPMethod::HTTP_DELETE, []() {
		wipeMemory();
		lastCheckTime = 0;
		lastTimezoneCheckTime = 0;
		apiServer.send(200, "text/html", "OK");
	});
}

String http_call(String host, String url, ushort port = 0, bool ssl = true) {

	WiFiClient* client;

	if (ssl) {
		client = new WiFiClientSecure;
	}
	else {
		client = new WiFiClient;
	}

	ushort _port{ port == 0 ? (ssl ? 443 : 80) : port };

	if (!client->connect(host.c_str(), _port)) {
		Serial.println(String{ "Could not connect to host" } + host);
		return "";
	}

	/*if (!client->verify(fingerprint, host)) {
	Serial.println("Wrong ssl cert fingerprint, refusing connection");
	return "";
	}*/

	client->print(String("GET ") + url + " HTTP/1.0\r\n" +
		"Host: " + host + "\r\n" +
		"User-Agent: ESP8266\r\n" +
		"Connection: close\r\n\r\n");

	bool isContent{ false };

	String line;

	while (client->connected()) {
		String tmp{ client->readStringUntil('\n') };
		if (isContent) {
			line += tmp;
		}
		else if (tmp == "\r") {
			isContent = true;
		}
	}

	delete client;

	return line;

}

// Returns only when connected to WiFi
// Enable the LED_PIN when connected and disable it while connecting
void checkWiFi() {
	if (WiFi.status() == WL_CONNECTED) return;
	digitalWrite(LED_PIN, HIGH);
	bool connected{ false };
	do {
		connected = WifiUtils::connect(wifi_ssid, wifi_password, true, 15, &localIP, &gateway, &subnet, &dns);
		if (!connected) {
			Serial.println("Could not connnect to WiFi, retrying.");
		}
	} while (!connected);

	digitalWrite(LED_PIN, LOW); // Obvious

	Serial.println("Connected to WiFi.");
}

void update_time_from_ntp() {
	timeClient.update();
	now_time.hour = timeClient.getHours();
	now_time.minute = timeClient.getMinutes();
	now_time.second = timeClient.getSeconds();
}

String currentLine;

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

			if (autoDebug)
				Serial.println("Successfully retrieved sunset hour");
			parsedSunset = sunset.result;

			float timezoneHourf{ timezone_offset / 3600.0f };
			byte timezoneHour{ static_cast<byte>(timezoneHourf) };
			byte timezoneMinute{ static_cast<byte>(fmod(timezoneHourf, 1) * 60)};
			parsedSunset.hour += timezoneHour;
			parsedSunset.minute += timezoneMinute;

			if (parsedSunset.minute > 59) {
				parsedSunset.minute -= 60;
				parsedSunset.hour += 1;
			}

			if (parsedSunset.hour > 23) parsedSunset.hour = parsedSunset.hour - 24;

			// Update last check time only if it succeeds
			lastCheckTime = millis();
		}else {
			Serial.println("Didn't retrieve sunset hour");
		}

		if (autoDebug)
			printInfos();

	}

	apiServer.handleClient();

	if (sunset_ok || lightsState != -1) {
		if (lightsState == 1 || (lightsState == -1 && mustBeOn(now_time, parsedSunset, false))) {
			digitalWrite(RELAY_PIN, LOW);
		}
		else {
			digitalWrite(RELAY_PIN, HIGH);
		}
	}
	
	if (Serial.available() > 0) { // Wrote something
		int serialToRead = Serial.available();
		char* buffer = static_cast<char*>(malloc(serialToRead));
		memset(buffer, '\0', serialToRead);
		Serial.readBytes(reinterpret_cast<uint8_t*>(buffer), static_cast<size_t>(serialToRead));

		currentLine += buffer;

		if (currentLine.endsWith("\r")) {

			currentLine = currentLine.substring(0, currentLine.length() - 1);
			Serial.print("\n");

			// Process
			if (currentLine.equalsIgnoreCase("info")) {
				Serial.println("\n");
				printInfos();
			}else if (currentLine.equalsIgnoreCase("lights -1")) {
				lightsState = -1;
				lastCheckTime = 0;
			}else if (currentLine.equalsIgnoreCase("lights 0")) {
				lightsState = 0;
				lastCheckTime = 0;
			}else if (currentLine.equalsIgnoreCase("lights 1")) {
				lightsState = 1;
				lastCheckTime = 0;
			}else if (currentLine.equalsIgnoreCase("debug 0")) {
				autoDebug = false;
			}else if (currentLine.equalsIgnoreCase("debug 1")) {
				autoDebug = true;
			}else if (currentLine.startsWith("interval ")) {
				loopRefreshInterval = atoi(currentLine.substring(9, currentLine.length()).c_str());
			}else if (currentLine.startsWith("refresh")) {
				lastCheckTime = 0;
			}else if (currentLine.startsWith("rfshtmz")) {
				setTimezone();
			}
			else if (currentLine.startsWith("reboot")) {
				ESP.restart();
			}
			else if (currentLine.startsWith("coreinf")) {
				debug_esp_infos();
			}else {
				Serial.println("Unknown command, available commands are : info, lights<-1, 0, 1>, debug<0,1>, interval <interval>, refresh, rfshtmz, coreinfo, reboot");
			}

			currentLine = String{};
		}
		Serial.print(buffer);
		free(buffer);
	}

	yield();

}

void debug_esp_infos() {
	Serial.printf("ESP.getFreeHeap()              : %d\r\n", ESP.getFreeHeap());   //  returns the free heap size.
	Serial.printf("ESP.getChipId()                : 0x%X\r\n", ESP.getChipId());   //  returns the ESP8266 chip ID as a 32-bit integer.
	Serial.printf("ESP.getSdkVersion()            : %d\r\n", ESP.getSdkVersion());
	Serial.printf("ESP.getBootVersion()           : %d\r\n", ESP.getBootVersion());
	Serial.printf("ESP.getBootMode()              : %d\r\n", ESP.getBootMode());
	Serial.printf("ESP.getCpuFreqMHz()            : %d\r\n", ESP.getCpuFreqMHz());
	Serial.printf("ESP.getFlashChipId()           : 0x%X\r\n", ESP.getFlashChipId());
	Serial.printf("ESP.getFlashChipRealSize()     : %d\r\n", ESP.getFlashChipRealSize());
	Serial.printf("ESP.getFlashChipSize()         : %d\r\n", ESP.getFlashChipSize());  //returns the flash chip size, in bytes, as seen by the SDK (may be less than actual size).
	Serial.printf("ESP.getFlashChipSpeed()        : %d\r\n", ESP.getFlashChipSpeed()); // returns the flash chip frequency, in Hz.
	Serial.printf("ESP.getFlashChipMode()         : %d\r\n", ESP.getFlashChipMode());
	Serial.printf("ESP.getFlashChipSizeByChipId() : 0x%X\r\n", ESP.getFlashChipSizeByChipId());
	Serial.printf("ESP.getSketchSize()            : %d\r\n", ESP.getSketchSize());
	Serial.printf("ESP.getFreeSketchSpace()       : %d\r\n", ESP.getFreeSketchSpace());
	Serial.printf("ESP.getCycleCount()            : %d\r\n", ESP.getCycleCount()); // returns the cpu instruction cycle count since start as an unsigned 32-bit. This is useful for accurate timing of very short actions like bit banging.

	rst_info *xyz;
	Serial.printf("ESP.getResetInfoPtr()\r\n");
	xyz = ESP.getResetInfoPtr();
	Serial.println((*xyz).reason);
	Serial.println((*xyz).exccause);
	Serial.println((*xyz).epc1);
	Serial.println((*xyz).epc2);
	Serial.println((*xyz).epc3);
	Serial.println((*xyz).excvaddr);
	Serial.println((*xyz).depc);
}

String IpAddress2String(const IPAddress& ipAddress){
	return String(ipAddress[0]) + "." + \
		ipAddress[1] + "." + \
		ipAddress[2] + "." + \
		ipAddress[3];
}

void printWifiSignalStrength(String *str) {
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

void printInfos(bool toSerial, String *str) {
	String infos;
	printWifiSignalStrength(&infos);
	infos += "Module MAC is : " + WiFi.macAddress() + "\n";
	infos += String{ "Timezone : " } + timezone_offset + "s\n";
	infos += String{ "Current time : " } + timeClient.getFormattedTime() + "\n";
	infos += String{ "Sunset is at : " } + parsedSunset.hour + ":" + parsedSunset.minute + ":" + parsedSunset.second + "\n";
	infos += String{ "Warn defined to : " } + sunset_warn + "s" + "\n";
	infos += String{ "Auto on between " } + power_min_sec + "s and " + power_max_sec + "s\n";
	infos += String{ "API key is " } + google_api_key + "\n";
	if (lightsState != -1)
		infos += String{ "Warning : Lights are not set to automatic (" } + static_cast<int>(lightsState) + ") !\n";

	mustBeOn(now_time, parsedSunset, true, &infos);
	if (toSerial) {
		Serial.println(infos);
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

	return String{ hours } + "h " + minutes + "min " + seconds + "sec";
}

bool mustBeOn(time_si const& now, time_si const& sunset, bool print, String *str) {

	int sunset_time = getTimeSecond(sunset);
	int now_time = getTimeSecond(now);
	int sunset_in_s = sunset_time - now_time; // if positive, sunset did not happen yet, if negative, sunset happend and next is tomorrow
	String infos;
	
	if (print) {
		if (sunset_in_s >= 0) {
			infos += String{ "Sunset in : " } + getSecondToTime(sunset_in_s) + "\n";
		}
		else {
			infos += String{ "Sunset happened " } + getSecondToTime(-sunset_in_s) + " ago\n";
		}
	}
	
	if (now_time >= power_min_sec && now_time <= power_max_sec) return true;

	if (sunset_in_s < sunset_warn) {
		if (sunset_warn > 0) {
			if (print)
				infos += String{ "Warn of " } + getSecondToTime(sunset_warn) + " before sunset happened\n";
		}else {
			if (print)
				infos += String{ "Warn of " } + getSecondToTime(-sunset_warn) + " after sunset happened\n";
		}
		if (print) {
			if (str != nullptr) {
				(*str) += infos;
			}
			else {
				Serial.println(infos);
			}
		}
		return true;
	}
	else { 
		if (print) {
			infos += "No Warn\n";
			if (str != nullptr) {
				(*str) += infos;
			}
			else {
				Serial.println(infos);
			}
		}
		return false;
	}
}

google_timezone_result getTimezone() {

	google_timezone_result res{ false, 0, 1 };

	const char* host = "maps.googleapis.com";

	String url{ String{"/maps/api/timezone/json?location="} + latitude + "," + longitude + "&timestamp=" + timeClient.getEpochTime() + "&key=" + google_api_key };

	String content{ http_call(host, url) };

	StaticJsonBuffer<800> jsonBuffer;
	JsonObject &root = jsonBuffer.parseObject(content.c_str(), 10);

	if (autoDebug) {
		Serial.println(url);
		Serial.println(content);
	}

	if (!root.success() || strcmp(root["status"], "OK") != 0) {
		Serial.println("Error parsing Google api JSON");
		Serial.println(root["status"].as<String>());
		res.error = true;
		if (root["status"] == "INVALID_REQUEST") {
			res.error_type = 1;
		}
		else if (root["status"] == "OVER_DAILY_LIMIT") {
			res.error_type = 2;
		}
		else if (root["status"] == "OVER_QUERY_LIMIT") {
			res.error_type = 3;
		}
		else if (root["status"] == "REQUEST_DENIED") {
			res.error_type = 4;
		}
		else if (root["status"] == "UNKNOWN_ERROR") {
			res.error_type = 5;
		}
		else if (root["status"] == "ZERO_RESULTS") {
			res.error_type = 6;
		}
		
	}
	else if(autoDebug) {
		Serial.println("Successfully received from Google Maps API");
	}

	res.timezone = root["dstOffset"].as<int>() + root["rawOffset"].as<int>();

	return res;
}

time_safe getApiSunrise() {
	String url{ String{"/json?lat="} + latitude + "&lng=" + longitude + "&date=today" };

	String content{ http_call(api_sunrise_host, url) };

	if (autoDebug) {
		Serial.println(content);
	}

	StaticJsonBuffer<800> jsonBuffer;
	JsonObject &root = jsonBuffer.parseObject(content.c_str(), 10);

	time_safe t;
	t.no_error = false;

	if (root.success() && strcmp(root["status"], "OK") == 0) {
		t.no_error = true;
		String sunset = root["results"]["sunset"];
		t.result = parseTimeH24(sunset);
	}

	return t;
}