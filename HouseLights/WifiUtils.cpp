#include <string>
#include <ESP8266WiFi.h>
#include "WifiUtils.h"
#include <EEPROM.h>
#include "Utils.h"

using Utils::dlay;

namespace WifiUtils {

	unsigned long lastTimeConnection = 0;
	unsigned lastTimeout = 0;

	bool connect(String essid, String password, bool blocking, unsigned timeout, IPAddress const * const ip, IPAddress const * const gateway, IPAddress const * const subnet, IPAddress const * const dns) {
		lastTimeConnection = millis();
		lastTimeout = timeout;
			
		WiFi.begin(essid.c_str(), password.c_str());

		if (ip != nullptr) {
			WiFi.config(*ip, *gateway, *subnet, *dns);
		}
		if (blocking) {
			while (true) {
				if (WiFi.status() == WL_CONNECTED) {
					return true;
				}
				else if ((millis() - lastTimeConnection >= lastTimeout * 1000)) { // timeout
					return false;
				}
				else {
					dlay(50);
				}
			}
		}
		return true;
	}

	bool isWifiConnected() {
		return WiFi.waitForConnectResult() == WL_CONNECTED;
	}

	bool isWifiTimedOut() {
		if (millis() - lastTimeConnection >= lastTimeout * 1000) {
			return true;
		}
		return false;
	}

	void setUserId(String const &uid) {
		return Utils::write_to_eeprom(uid, 78);
	}

	void setToken(String const &token) {
		return Utils::write_to_eeprom(token, 103);
	}

	String getUserId() {
		return Utils::read_from_eeprom(78, 24);
	}

	String getToken() {
		return Utils::read_from_eeprom(103, 24);
	}

	void setEEPROMWifiAuth(Wifi_st const &st) {
		Utils::write_to_eeprom(st.essid, 0); // 0 to 32
		Utils::write_to_eeprom(st.pass, 32); // 33 to 79
	}

	Wifi_st getEEPROMWifiAuth() {
		EEPROM.begin(128);

		char f = reinterpret_cast<unsigned char>(EEPROM.read(0));

		Wifi_st wifi;
		wifi.set = 0;

		if (f != 255) {

			String essid = Utils::read_from_eeprom(0, 32);
			String pass = Utils::read_from_eeprom(32, 45);
			wifi.essid = essid;
			wifi.pass = pass;
			wifi.set = 1;
			
		}

		EEPROM.end();

		return wifi;
		

	}

}