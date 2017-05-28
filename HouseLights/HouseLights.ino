#include <NTPClient.h>
#include <TimeLib.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "WiFiUtils.h"
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include "defs.h"

#define SUNSET_WARN -600
#define POWER_MIN_SEC 0
#define POWER_MAX_SEC 4000
#define UTC_OFFSET_HOUR 2

const char* wifi_ssid = "***REMOVED***";
const char* wifi_password = "***REMOVED***";
const char* api_sunrise_host = "api.sunrise-sunset.org";
const char* latitude = "***REMOVED***";
const char* longitude = "***REMOVED***";
unsigned long lastCheckTime = 0;
unsigned int loopRefreshInterval = 60000;
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

	Serial.begin(115200);
	while (!Serial) {
		;; // Wait for serial working
	}

	WiFi.setAutoReconnect(true);

	pinMode(4, OUTPUT);
	pinMode(2, OUTPUT);
	digitalWrite(4, HIGH);
	digitalWrite(2, HIGH);

	WiFi.mode(WIFI_STA);

	checkWiFi();
	
	Serial.println("Connected to WiFi.");

	Serial.println("Ready.");

}

void checkWiFi() {
	if (WiFi.status() == WL_CONNECTED) return;
	digitalWrite(2, HIGH);
	bool connected{ false };
	do {
		connected = WifiUtils::connect(wifi_ssid, wifi_password, true, 15);
		if (!connected) {
			Serial.println("Could not connnect to WiFi, retrying.");
		}
	} while (!connected);

	if (connected)digitalWrite(2, LOW); // Obvious

	Serial.println("Connected to WiFi.");
}

String currentLine;

void loop() {

	if (millis() > lastCheckTime + loopRefreshInterval || lastCheckTime == 0) {

		checkWiFi();
	
		timeClient.update();
		timeClient.setTimeOffset(UTC_OFFSET_HOUR * 60 * 60);

		now_time.hour = timeClient.getHours();
		now_time.minute = timeClient.getMinutes();
		now_time.second = timeClient.getSeconds();

		time_safe sunset = getApiSunrise();

		if (sunset.no_error) {
			if (autoDebug)
				Serial.println("Successfully retrieved sunset hour");
			parsedSunset = sunset.result;
			parsedSunset.hour += UTC_OFFSET_HOUR; // UTC Belgium
		}
		else {
			Serial.println("Didn't retrieve sunset hour");
		}

		if (autoDebug)
			printInfos();

		if (parsedSunset.hour > 23) parsedSunset.hour = 24 - parsedSunset.hour;

		if (lightsState == 1 || (lightsState == -1 && mustBeOn(now_time, parsedSunset, false))) {
			digitalWrite(4, LOW);
		}
		else {
			digitalWrite(4, HIGH);
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
