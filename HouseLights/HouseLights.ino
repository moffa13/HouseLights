#include <NTPClient.h>
#include <WiFiUdp.h>
#include <WiFiServer.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include "defs.h"
#include "WifiUtils.h"

/* Time to set the lights on before the "official" sunset 
Set it to 0 to power on the lights exactly when the sunset happens
*/
#define SUNSET_WARN -600

/* Interval where the lights are always on. 
Normally, when we reach the next day, the sun is not supposed to be down
so the lights shut off */
#define POWER_MIN_SEC 0
// Change this to tell for how many seconds the lights should keep being on after midnight
#define POWER_MAX_SEC 5400 // 1:30 am

/* Comment this to set a manual timezone & set timezone_offset to whatever timezone used in your country
Otherwise, set up a free Google Time Zone API here https://developers.google.com/maps/documentation/timezone/get-api-key
and put the key in GOOGLE_TIMEZONE_API_KEY
*/
#define USE_TIMEZONE_API
#define GOOGLE_TIMEZONE_API_KEY "YOUR_GOOGLE_TIMEZONE_API_KEY"

// Change these parameters to fit with your iot device
#define RELAY_PIN 0
#define LED_PIN 2

const char* const wifi_ssid = "YOUR_SSID";
const char* const wifi_password = "YOUR_PASSWORD";
const char* const latitude = "YOUR_LATITUDE";
const char* const longitude = "YOUR_LONGITUDE";
const char* const api_sunrise_host = "api.sunrise-sunset.org";
unsigned int timezone_offset = 0;
unsigned long lastCheckTime = 0;
unsigned int loopRefreshInterval = 1200000;
short lightsState = -1; // -1 auto, 0 off, 1 on
bool sunset_ok = false;
bool autoDebug = false; // If true, auto shows printInfos method 

time_si parsedSunset;
time_si now_time;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

bool mustBeOn(time_si const& now, time_si const& sunset, bool print = false);

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

void setup() {

	Serial.begin(9600);
	while (!Serial) {
		;; // Wait for serial working
	}

	WiFi.setAutoReconnect(true);

	pinMode(RELAY_PIN, OUTPUT);
	pinMode(LED_PIN, OUTPUT);
	digitalWrite(RELAY_PIN, HIGH);
	digitalWrite(LED_PIN, HIGH);

	WiFi.mode(WIFI_STA);

	checkWiFi();

	timeClient.begin();

#ifdef USE_TIMEZONE_API	
	bool successUpdate = false;
	do {
		successUpdate = timeClient.forceUpdate();
	} while (!successUpdate);

	timezone_offset = getTimezone();
#endif

	timeClient.setTimeOffset(timezone_offset);

	Serial.println(String{ "Set Timezone to " } + timezone_offset + "s");

	Serial.println("Ready.");
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
		Serial.println("Could connect to google api");
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

void checkWiFi() {
	if (WiFi.status() == WL_CONNECTED) return;
	digitalWrite(LED_PIN, HIGH);
	bool connected{ false };
	do {
		connected = WifiUtils::connect(wifi_ssid, wifi_password, true, 15);
		if (!connected) {
			Serial.println("Could not connnect to WiFi, retrying.");
		}
	} while (!connected);

	if (connected)digitalWrite(LED_PIN, LOW); // Obvious

	Serial.println("Connected to WiFi.");
}

String currentLine;

void loop() {

	timeClient.update();
	now_time.hour = timeClient.getHours();
	now_time.minute = timeClient.getMinutes();
	now_time.second = timeClient.getSeconds();

	// Check the new sunset every <loopRefreshInterval>ms
	if (millis() > lastCheckTime + loopRefreshInterval || lastCheckTime == 0) {

		checkWiFi();
		
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

	if (sunset_ok) {
		if (lightsState == 1 || (lightsState == -1 && mustBeOn(now_time, parsedSunset, false))) {
			digitalWrite(RELAY_PIN, LOW);
		}
		else {
			digitalWrite(RELAY_PIN, HIGH);
		}
	}
	
	if (Serial.available() > 0) { // Wrote something
		char* buffer = static_cast<char*>(malloc(Serial.available()));
		Serial.readBytes(reinterpret_cast<uint8_t*>(buffer), static_cast<size_t>(Serial.available()));

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
			}else {
				Serial.println("Unknown command, available commands are : info, lights<-1, 0, 1>, debug<0,1>, interval <interval>, refresh");
			}

			currentLine = String{};
		}
		Serial.print(buffer);
		free(buffer);
	}
	
	yield();

}

String IpAddress2String(const IPAddress& ipAddress){
	return String(ipAddress[0]) + "." + \
		ipAddress[1] + "." + \
		ipAddress[2] + "." + \
		ipAddress[3];
}

void printInfos() {
	printWifiSignalStrength();
	Serial.println("Module MAC is : " + WiFi.macAddress());
	Serial.println(String{ "Timezone : " } + timezone_offset + "s");
	Serial.println(String{ "Current time : " } + timeClient.getFormattedTime());
	Serial.println(String{ "Sunset is at : " } + parsedSunset.hour + ":" + parsedSunset.minute + ":" + parsedSunset.second);
	if (lightsState != -1)
		Serial.println(String{ "Warning : Lights are not set to automatic (" } + static_cast<int>(lightsState) + ") !" );
	mustBeOn(now_time, parsedSunset, true);
}

void printWifiSignalStrength() {
	if (WiFi.status() == WL_CONNECTED) {
		Serial.println("WiFi is connected");
		int32_t rssi{ WiFi.RSSI() };
		Serial.println(String{ "WiFi signal strength : " } + rssi + "dBm");
		Serial.println("Local IP address is : " + IpAddress2String(WiFi.localIP()));
	}
	else {
		Serial.println("No WiFi connection");
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

bool mustBeOn(time_si const& now, time_si const& sunset, bool print) {

	int sunset_time = getTimeSecond(sunset);
	int now_time = getTimeSecond(now);
	int sunset_in_s = sunset_time - now_time; // if positive, sunset did not happen yet, if negative, sunset happend and next is tomorrow
	
	if (print) {
		if (sunset_in_s >= 0) {
			Serial.println(String{ "Sunset in : " } + getSecondToTime(sunset_in_s));
		}
		else {
			Serial.println(String{ "Sunset happened " } + getSecondToTime(-sunset_in_s) + " ago");
		}
	}
	
	if (now_time >= POWER_MIN_SEC && now_time <= POWER_MAX_SEC) return true;

	if (sunset_in_s < SUNSET_WARN) {
		if (SUNSET_WARN > 0) {
			if (print)
				Serial.println(String{ "Warn of " } + getSecondToTime(SUNSET_WARN) + " before sunset happened");
		}else {
			if (print)
				Serial.println(String{ "Warn of " } + getSecondToTime(-SUNSET_WARN) + " after sunset happened");
		}
		return true;
	}
	else { 
		if (print)
			Serial.println(String{ "No warn" });
		return false;
	}
}

unsigned int getTimezone() {

	const char* host = "maps.googleapis.com";

	String url{ String{"/maps/api/timezone/json?location="} + latitude + "," + longitude + "&timestamp=" + timeClient.getEpochTime() + "&key=" + GOOGLE_TIMEZONE_API_KEY };

	String content{ http_call(host, url) };
	
	StaticJsonBuffer<800> jsonBuffer;
	JsonObject &root = jsonBuffer.parseObject(content.c_str(), 10);

	if (autoDebug) {
		Serial.println(content);
	}

	if (!root.success() || strcmp(root["status"], "OK") != 0) {
		Serial.println("Error parsing Google api JSON");
		return 0;
	}
	else if(autoDebug) {
		Serial.println("Successfully received from Google Maps API");
	}

	return root["dstOffset"].as<unsigned int>() + root["rawOffset"].as<unsigned int>();
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