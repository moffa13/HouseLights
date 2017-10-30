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

#define SUNSET_WARN -600
#define POWER_MIN_SEC 0
#define POWER_MAX_SEC 4000
#define USE_TIMEZONE_API
#define GOOGLE_TIMEZONE_API_KEY "***REMOVED***"
//#define UTC_OFFSET_HOUR 2
#define RELAY_PIN 0
#define LED_PIN 2

const char* wifi_ssid = "***REMOVED***";
const char* wifi_password = "***REMOVED***";
const char* api_sunrise_host = "api.sunrise-sunset.org";
const char* latitude = "***REMOVED***";
const char* longitude = "***REMOVED***";
const char* google_api_fingerprint = "49 8C B4 68 C2 9D 4F 34 04 FA 04 41 B6 EF 1D 04 60 3B 09 79";
unsigned long lastCheckTime = 0;
unsigned int loopRefreshInterval = 1200000;
short lightsState = -1; // -1 auto, 0 off, 1 on
bool autoDebug = false; // If true, auto shows printInfos method 


time_si parsedSunset;
time_si now_time;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

bool mustBeOn(time_si now, time_si sunset, bool print = false);

String getValue(String data, char separator, int index)
{
	int found = 0;
	int strIndex[] = { 0, -1 };
	int maxIndex = data.length() - 1;

	for (int i = 0; i <= maxIndex && found <= index; i++) {
		if (data.charAt(i) == separator || i == maxIndex) {
			found++;
			strIndex[0] = strIndex[1] + 1;
			strIndex[1] = (i == maxIndex) ? i + 1 : i;
		}
	}
	return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

time_si parseTimeH24(String time) {

	time_si t;
	if (time.endsWith("AM")) {
		t.hour = static_cast<byte>(atoi(getValue(time, ':', 0).substring(0, 2).c_str()));
	}
	else { // Convert to 24 hour
		t.hour = static_cast<byte>(12 + atoi(getValue(time, ':', 0).substring(0, 2).c_str()));
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

	Serial.println("Ready.");

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

	if (millis() > lastCheckTime + loopRefreshInterval || lastCheckTime == 0) {

		checkWiFi();
	
		timeClient.update();

		unsigned int timezone_offset = 0;

#ifdef USE_TIMEZONE_API
		timezone_offset = getTimezone();
#else
		timezone_offset = UTC_OFFSET_HOUR * 60 * 60;
#endif

		timeClient.setTimeOffset(timezone_offset);

		if (autoDebug) {
			Serial.println(String{ "Set Timezone to " } +timezone_offset + "s");
		}
		

		now_time.hour = timeClient.getHours();
		now_time.minute = timeClient.getMinutes();
		now_time.second = timeClient.getSeconds();

		time_safe sunset = getApiSunrise();

		if (sunset.no_error) {
			if (autoDebug)
				Serial.println("Successfully retrieved sunset hour");
			parsedSunset = sunset.result;
			parsedSunset.hour += timezone_offset / 3600; // half-hour timezone not supported
		}
		else {
			Serial.println("Didn't retrieve sunset hour");
		}

		if (autoDebug)
			printInfos();

		if (parsedSunset.hour > 23) parsedSunset.hour = 24 - parsedSunset.hour;

		if (lightsState == 1 || (lightsState == -1 && mustBeOn(now_time, parsedSunset, false))) {
			digitalWrite(RELAY_PIN, LOW);
		}
		else {
			digitalWrite(RELAY_PIN, HIGH);
		}

		lastCheckTime = millis();
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
			}else {
				Serial.println("Unknown command, available commands are : info, lights<-1, 0, 1>, debug<0,1>");
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

int getTimeSecond(time_si time) {
	return time.hour * 60 * 60 + time.minute * 60 + time.second;
}

bool mustBeOn(time_si now, time_si sunset, bool print) {

	int sunset_time = getTimeSecond(sunset);
	int now_time = getTimeSecond(now);
	int sunset_in_s = sunset_time - now_time; // if positive, sunset did not happen yet, if negative, sunset happend and next is tomorrow
	
	if (print) {
		if (sunset_in_s >= 0) {
			Serial.println(String{ "Sunset in : " } + sunset_in_s + "s");
		}
		else {
			Serial.println(String{ "Sunset happened " } + (-sunset_in_s) + "s ago");
		}
	}
		
	
	if (now_time >= POWER_MIN_SEC && now_time <= POWER_MAX_SEC) return true;

	if (sunset_in_s < SUNSET_WARN) {
		if (SUNSET_WARN > 0) {
			if (print)
				Serial.println(String{ "Warn of " } + SUNSET_WARN + "s before sunset happened");
		}else {
			if (print)
				Serial.println(String{ "Warn of " } + (-SUNSET_WARN) + "s after sunset happened");
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
	WiFiClientSecure client;

	const char* host = "maps.googleapis.com";

	if (!client.connect(host, 443)) {
		Serial.println("Could connect to google api");
		return 0;
	}

	if (!client.verify(google_api_fingerprint, "maps.googleapis.com")) {
		Serial.println("Google api wrong fingerprint, refusing connection");
		//return 0;
	}

	String url{ String{"/maps/api/timezone/json?location="} + latitude + "," + longitude + "&timestamp=" + timeClient.getEpochTime() + "&key=" + GOOGLE_TIMEZONE_API_KEY };


	client.print(String("GET ") + url + " HTTP/1.1\r\n" +
		"Host: " + host + "\r\n" +
		"User-Agent: ESP8266\r\n" +
		"Connection: close\r\n\r\n");

	bool isContent{ false };

	String line;

	while (client.connected()) {
		String tmp{ client.readStringUntil('\n') };
		if (isContent) {
			line += tmp;
		}else if (tmp == "\r") {
			isContent = true;
		}
	}
	
	StaticJsonBuffer<800> jsonBuffer;
	JsonObject &root = jsonBuffer.parseObject(line.c_str(), 10);

	if (autoDebug) {
		Serial.println(line);
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
	HTTPClient client;
	client.begin(String("http://") + api_sunrise_host + "/json?lat=" + latitude + "&lng=" + longitude + "&date=today");

	int httpCode = client.GET();
	
	String content;

	time_safe t;
	t.no_error = false;

	if (httpCode > 0) {

		if (httpCode == HTTP_CODE_OK) {
			content = client.getString();
			t.no_error = true;
		}

	}

	StaticJsonBuffer<800> jsonBuffer;
	JsonObject &root = jsonBuffer.parseObject(content.c_str(), 10);

	if (!root.success()) {
		t.no_error = false;
	}

	String sunset = root["results"]["sunset"];
	t.result = parseTimeH24(sunset);

	return t;

}
