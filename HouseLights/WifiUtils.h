#ifndef WIFI_H
#define WIFI_H

#include <string>

namespace WifiUtils {

	extern unsigned long lastTimeConnection;
	extern unsigned lastTimeout;

	struct Wifi_st {
		String essid;
		String pass;
		bool set;
	};

	struct Wifi_list_st {
		String essid;
		int32_t rssi;
		bool has_pass;
	};

	// Blocking until connected
	bool connect(String essid, String password, bool blocking = true, unsigned timeout = 15);

	bool isWifiConnected();

	bool isWifiTimedOut();

	void setEEPROMWifiAuth(Wifi_st const &st);

	Wifi_st getEEPROMWifiAuth();

	void setUserId(String const &uid);

	void setToken(String const &token);

	String getUserId();

	String getToken();

}

#endif