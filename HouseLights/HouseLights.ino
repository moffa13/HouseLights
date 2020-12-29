#include <GDBStub.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include "defs.h"
#include "WifiUtils.h"

/* Comment this to set a manual timezone & set timezone_offset to whatever timezone used in your country
Otherwise, set up a free Google Time Zone API here https://developers.google.com/maps/documentation/timezone/get-api-key
and change the key of google_api_key
*/
#define USE_TIMEZONE_API
static int timezone_offset = 1; // Offset to apply from NTP's time, 1 is unset, correct offset is multiple of 3600

// Change these parameters to fit with your iot device
#define RELAY_PIN 4
#define LED_PIN 2

// Whether or not the relay used needs 0V to output power
#define OUTPUT_REVERSED false

#define HTTP_REQUEST_TIMEOUT_MS 5000

//#define STATIC_IP

static const char* const wifi_ssid = "YOUR_SSID";
static const char* const wifi_password = "YOUR_PASSWORD";
static const char* const latitude = "YOUR_LATITUDE";
static const char* const longitude = "YOUR_LONGITUDE";
static const char* const device_name = "IOT_HOSTNAME";

static const char* const api_sunrise_host = "api.sunrise-sunset.org";

static unsigned long lastCheckTime = 0;
static unsigned int loopRefreshInterval = 3600000;
static const uint16_t max_google_timezone_retries = 2;
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
static char google_api_key[40] = { 0 };

static time_si parsedSunset;
static time_si parsedSunrise;
static time_si now_time;
static WiFiUDP ntpUDP;
static NTPClient timeClient(ntpUDP);

/* Change these infos to fit your needs, if using static IP, set #define STATIC_IP*/
#ifdef STATIC_IP
static const IPAddress dns(8, 8, 8, 8);
static const IPAddress localIP(192, 168, 0, 232);
static const IPAddress gateway(192, 168, 0, 1);
static const IPAddress subnet(255, 255, 255, 0);
#endif
static ESP8266WebServer apiServer(4445);

bool mustBeOn(time_si const& now, time_si const& sunset, bool print, String* str = nullptr);
void printWifiSignalStrength(String* str = nullptr);
void printInfos(bool toSerial = true, String* str = nullptr);
void checkWifi(bool connectAnyway = false);

static const char letsencryptCA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIEkjCCA3qgAwIBAgIQCgFBQgAAAVOFc2oLheynCDANBgkqhkiG9w0BAQsFADA/
MSQwIgYDVQQKExtEaWdpdGFsIFNpZ25hdHVyZSBUcnVzdCBDby4xFzAVBgNVBAMT
DkRTVCBSb290IENBIFgzMB4XDTE2MDMxNzE2NDA0NloXDTIxMDMxNzE2NDA0Nlow
SjELMAkGA1UEBhMCVVMxFjAUBgNVBAoTDUxldCdzIEVuY3J5cHQxIzAhBgNVBAMT
GkxldCdzIEVuY3J5cHQgQXV0aG9yaXR5IFgzMIIBIjANBgkqhkiG9w0BAQEFAAOC
AQ8AMIIBCgKCAQEAnNMM8FrlLke3cl03g7NoYzDq1zUmGSXhvb418XCSL7e4S0EF
q6meNQhY7LEqxGiHC6PjdeTm86dicbp5gWAf15Gan/PQeGdxyGkOlZHP/uaZ6WA8
SMx+yk13EiSdRxta67nsHjcAHJyse6cF6s5K671B5TaYucv9bTyWaN8jKkKQDIZ0
Z8h/pZq4UmEUEz9l6YKHy9v6Dlb2honzhT+Xhq+w3Brvaw2VFn3EK6BlspkENnWA
a6xK8xuQSXgvopZPKiAlKQTGdMDQMc2PMTiVFrqoM7hD8bEfwzB/onkxEz0tNvjj
/PIzark5McWvxI0NHWQWM6r6hCm21AvA2H3DkwIDAQABo4IBfTCCAXkwEgYDVR0T
AQH/BAgwBgEB/wIBADAOBgNVHQ8BAf8EBAMCAYYwfwYIKwYBBQUHAQEEczBxMDIG
CCsGAQUFBzABhiZodHRwOi8vaXNyZy50cnVzdGlkLm9jc3AuaWRlbnRydXN0LmNv
bTA7BggrBgEFBQcwAoYvaHR0cDovL2FwcHMuaWRlbnRydXN0LmNvbS9yb290cy9k
c3Ryb290Y2F4My5wN2MwHwYDVR0jBBgwFoAUxKexpHsscfrb4UuQdf/EFWCFiRAw
VAYDVR0gBE0wSzAIBgZngQwBAgEwPwYLKwYBBAGC3xMBAQEwMDAuBggrBgEFBQcC
ARYiaHR0cDovL2Nwcy5yb290LXgxLmxldHNlbmNyeXB0Lm9yZzA8BgNVHR8ENTAz
MDGgL6AthitodHRwOi8vY3JsLmlkZW50cnVzdC5jb20vRFNUUk9PVENBWDNDUkwu
Y3JsMB0GA1UdDgQWBBSoSmpjBH3duubRObemRWXv86jsoTANBgkqhkiG9w0BAQsF
AAOCAQEA3TPXEfNjWDjdGBX7CVW+dla5cEilaUcne8IkCJLxWh9KEik3JHRRHGJo
uM2VcGfl96S8TihRzZvoroed6ti6WqEBmtzw3Wodatg+VyOeph4EYpr/1wXKtx8/
wApIvJSwtmVi4MFU5aMqrSDE6ea73Mj2tcMyo5jMd6jmeWUHK8so/joWUoHOUgwu
X4Po1QYz+3dszkDqMp4fklxBwXRsW10KXzPMTZ+sOPAveyxindmjkW8lGy+QsRlG
PfZ+G6Z6h7mjem0Y+iWlkYcV4PIWL1iwBi8saCbGS5jN2p8M+X+Q7UNKEkROb3N6
KOqkqm57TH2H3eDJAkSnh6/DNFu0Qg==
-----END CERTIFICATE-----
)EOF";

static const char googleCA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIESjCCAzKgAwIBAgINAeO0mqGNiqmBJWlQuDANBgkqhkiG9w0BAQsFADBMMSAw
HgYDVQQLExdHbG9iYWxTaWduIFJvb3QgQ0EgLSBSMjETMBEGA1UEChMKR2xvYmFs
U2lnbjETMBEGA1UEAxMKR2xvYmFsU2lnbjAeFw0xNzA2MTUwMDAwNDJaFw0yMTEy
MTUwMDAwNDJaMEIxCzAJBgNVBAYTAlVTMR4wHAYDVQQKExVHb29nbGUgVHJ1c3Qg
U2VydmljZXMxEzARBgNVBAMTCkdUUyBDQSAxTzEwggEiMA0GCSqGSIb3DQEBAQUA
A4IBDwAwggEKAoIBAQDQGM9F1IvN05zkQO9+tN1pIRvJzzyOTHW5DzEZhD2ePCnv
UA0Qk28FgICfKqC9EksC4T2fWBYk/jCfC3R3VZMdS/dN4ZKCEPZRrAzDsiKUDzRr
mBBJ5wudgzndIMYcLe/RGGFl5yODIKgjEv/SJH/UL+dEaltN11BmsK+eQmMF++Ac
xGNhr59qM/9il71I2dN8FGfcddwuaej4bXhp0LcQBbjxMcI7JP0aM3T4I+DsaxmK
FsbjzaTNC9uzpFlgOIg7rR25xoynUxv8vNmkq7zdPGHXkxWY7oG9j+JkRyBABk7X
rJfoucBZEqFJJSPk7XA0LKW0Y3z5oz2D0c1tJKwHAgMBAAGjggEzMIIBLzAOBgNV
HQ8BAf8EBAMCAYYwHQYDVR0lBBYwFAYIKwYBBQUHAwEGCCsGAQUFBwMCMBIGA1Ud
EwEB/wQIMAYBAf8CAQAwHQYDVR0OBBYEFJjR+G4Q68+b7GCfGJAboOt9Cf0rMB8G
A1UdIwQYMBaAFJviB1dnHB7AagbeWbSaLd/cGYYuMDUGCCsGAQUFBwEBBCkwJzAl
BggrBgEFBQcwAYYZaHR0cDovL29jc3AucGtpLmdvb2cvZ3NyMjAyBgNVHR8EKzAp
MCegJaAjhiFodHRwOi8vY3JsLnBraS5nb29nL2dzcjIvZ3NyMi5jcmwwPwYDVR0g
BDgwNjA0BgZngQwBAgIwKjAoBggrBgEFBQcCARYcaHR0cHM6Ly9wa2kuZ29vZy9y
ZXBvc2l0b3J5LzANBgkqhkiG9w0BAQsFAAOCAQEAGoA+Nnn78y6pRjd9XlQWNa7H
TgiZ/r3RNGkmUmYHPQq6Scti9PEajvwRT2iWTHQr02fesqOqBY2ETUwgZQ+lltoN
FvhsO9tvBCOIazpswWC9aJ9xju4tWDQH8NVU6YZZ/XteDSGU9YzJqPjY8q3MDxrz
mqepBCf5o8mw/wJ4a2G6xzUr6Fb6T8McDO22PLRL6u3M4Tzs3A2M1j6bykJYi8wW
IRdAvKLWZu/axBVbzYmqmwkm5zLSDW5nIAJbELCQCZwMH56t2Dvqofxs6BBcCFIZ
USpxu6x6td0V7SvJCCosirSmIatj/9dSSVDQibet8q/7UK4v4ZUN80atnZz1yg==
-----END CERTIFICATE-----

)EOF";

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
#ifdef USE_TIMEZONE_API	
	if (google_api_key[0] != 0) { // Has a google api key configured and it is time to update
		uint16_t google_timezone_retries = 0;
		google_timezone_result timezone_result;
		do {
			timezone_result = getTimezone();

			if (timezone_result.error) {
				google_timezone_retries++;
#ifndef _DEBUG
				Serial.println("Failed retrieving timezone, retrying in 1/4th of a second");
#endif
				delay(250);
			}
		} while (timezone_result.error && google_timezone_retries < max_google_timezone_retries); // Retry if google timezone returned an error

		if (!timezone_result.error) {
			if (timezone_offset != timezone_result.timezone) {
				timeClient.setTimeOffset(timezone_result.timezone);
#ifndef _DEBUG
				Serial.println(String{ "Set Timezone to " } +timezone_result.timezone + "s");
#endif
				timezone_offset = timezone_result.timezone;
				return true;
			}
#ifndef _DEBUG
				Serial.println(String{ "Timezone did not change (" } +timezone_offset + "s)");
#endif
				return false;
		}
		return false;
		google_timezone_retries = 0;
	}

#ifndef _DEBUG
		Serial.println("Warning, google_api_key not set");
#endif
	return false;
#endif
}

void setup() {

	pinMode(RELAY_PIN, OUTPUT);
	pinMode(LED_PIN, OUTPUT);
	digitalWrite(RELAY_PIN, OUTPUT_REVERSED ? HIGH : LOW);
	digitalWrite(LED_PIN, HIGH);

#ifdef _DEBUG
	gdbstub_init();
#else
	Serial.begin(9600);
	while (!Serial) {
		;; // Wait for serial to work
	}
#endif

	EEPROM.begin(256);

	readMemory();

	WiFi.mode(WIFI_STA);

#ifdef STATIC_IP
	WiFi.config(localIP, gateway, subnet, dns);
#else
	WiFi.hostname(device_name);
#endif

	apiServer.begin();

	registerApiServerRequests();

#ifndef _DEBUG
	Serial.println("Ready.");
#endif

}

void loop() {

	static bool firstCheck = true;
	if (firstCheck) {
		checkWifi(true);
#ifndef _DEBUG
		Serial.println(String("Local ip is : " + IpAddress2String(WiFi.localIP())));
#endif
		ArduinoOTA.begin();
		timeClient.begin();
		timeClient.forceUpdate();

		firstCheck = false;
	}
	else {
		checkWifi(); // Regular wifi check
	}

	update_time_from_ntp();


	// Check the new sunset every <loopRefreshInterval>ms
	if (millis() > lastCheckTime + loopRefreshInterval || lastCheckTime == 0) {

		
		setTimezone();
		
		time_safe sunset;
		sunset.no_error = false;
		if (timezone_offset%3600 == 0) { // We have utc time and timezone, we can get sunset time
			sunset = getApiSunrise();
		}

		if (sunset.no_error) {

			sunset_ok = true;

#ifndef _DEBUG
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
			lastCheckTime = millis() - loopRefreshInterval + 30000; // Retry after 30s
#endif
		}

		if (autoDebug)
			printInfos();

	}

	ArduinoOTA.handle();

	apiServer.handleClient();

	updateLightsState();

	yield();

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
				lastCheckTime = 0;
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

	apiServer.on("/wipe", HTTPMethod::HTTP_DELETE, []() {
		wipeMemory();
		lastCheckTime = 0;
		apiServer.send(200, "text/html", "OK");
		});
}

String http_call(String host, String url, ushort port = 0, bool ssl = true, const char* trustedCert = nullptr) {

	BearSSL::WiFiClientSecure client;
	BearSSL::X509List cert;

	if (ssl) {
		//client.setCertificate(reinterpret_cast<const uint8_t*>(trustedCert), certSize);
		cert.append(trustedCert);
		client.setTrustAnchors(&cert);
		client.setX509Time(timeClient.getEpochTime());
	}
	
	ushort _port{ port == 0 ? (ssl ? 443 : 80) : port };

	client.connect(host.c_str(), _port);

	if (!client.connected()) {
#ifndef _DEBUG
		Serial.println(String{ "Could not connect to host " } + host + ":" + _port);
#endif
		return "";
	}

	client.print(String("GET ") + url + " HTTP/1.0\r\n" +
		"Host: " + host + "\r\n" +
		"User-Agent: ESP8266\r\n" +
		"Connection: close\r\n\r\n");

	unsigned long timeout = millis();
	while (client.available() == 0) {
		if (millis() - timeout > HTTP_REQUEST_TIMEOUT_MS)
		{
			Serial.println("Client Timed out");
			client.stop();
			return "";
		}
		yield();
	}

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
void checkWifi(bool connectAnyway){
	if (!connectAnyway && WifiUtils::isWifiConnected()) return;
	digitalWrite(LED_PIN, HIGH);
	bool connected{ false };
	do {
		connected = WifiUtils::connect(wifi_ssid, wifi_password, true, 15);
		if (!connected) {
#ifndef _DEBUG
			Serial.println("Could not connnect to WiFi, retrying.");
#endif
		}
	} while (!connected);

	delay(100);

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

void updateLightsState() {
	if (sunset_ok || lightsState != -1) {
		if (lightsState == 1 || (lightsState == -1 && mustBeOn(now_time, parsedSunset, false))) {
			digitalWrite(RELAY_PIN, OUTPUT_REVERSED ? LOW : HIGH);
		}
		else {
			digitalWrite(RELAY_PIN, OUTPUT_REVERSED ? HIGH : LOW);
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
	int sunrise_time = getTimeSecond(parsedSunrise);
	int now_time = getTimeSecond(now);
	int sunset_in_s = sunset_time - now_time; // if positive, sunset did not happen yet, if negative, sunset happend and next is tomorrow
	int sunset_time_corrected = sunset_time - sunset_warn; // Corrected sunset value.

	String infos;
	bool ret;

	if (sunset_in_s >= 0) {
		infos += String{ "Sunset in : " } +getSecondToTime(sunset_in_s) + "\n";
	}
	else {
		infos += String{ "Sunset happened " } +getSecondToTime(-sunset_in_s) + " ago\n";
	}

	if (now_time >= sunrise_time && now_time <= sunset_time_corrected) { // between sunrise and sunset there should be no light
		infos += "Off\n";
		ret = false;
	}
	else if (now_time < sunrise_time) { // Before sunrise
		if (now_time >= power_morning_min) {
			infos += String{ "Morning mode before sunrise\n" };
			ret = true;
		}
		else if (power_max_sec < sunrise_time && now_time >= power_min_sec && now_time <= power_max_sec) {
			infos += String{ "On Period after sunset enabled\n" };
			ret = true;
		}
		else {
			infos += String{ "Off\n" };
			ret = false;
		}
	}
	else if (now_time > sunset_time_corrected) { // After sunset

		// If current time is not past the max and the max is within the correct timing
		if (now_time >= power_min_sec && (power_max_sec < sunrise_time || now_time < power_max_sec)) { 
			infos += String{ "On Period after sunset enabled\n" };
			ret = true;
		}
		else {
			infos += String{ "Off\n" };
			ret = false;
		}
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

	return ret;

}



google_timezone_result getTimezone() {

	google_timezone_result res{ false, 0, 1 };

	const char* host = "maps.googleapis.com";

	String url{ String{"/maps/api/timezone/json?location="} +latitude + "," + longitude + "&timestamp=" + timeClient.getEpochTime() + "&key=" + google_api_key };

	String content{ http_call(host, url, 0, true, googleCA) };

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
		Serial.println(jsonDoc["errorMessage"].as<String>());
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
#ifndef _DEBUG
	else {
		Serial.println("Successfully received from Google Maps API");
	}
#endif

	res.timezone = jsonDoc["dstOffset"].as<int>() + jsonDoc["rawOffset"].as<int>();

	return res;
}

time_safe getApiSunrise() {
	String url{ String{"/json?lat="} +latitude + "&lng=" + longitude + "&date=today" };

	String content{ http_call(api_sunrise_host, url, 0, true, letsencryptCA) };

#ifndef _DEBUG
	if (autoDebug) {
		Serial.println(content);
	}
#endif
	
	StaticJsonDocument<1000> jsonDoc;
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