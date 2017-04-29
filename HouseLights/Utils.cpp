#include "Utils.h"
#include <Arduino.h>
namespace Utils {

	void write_to_eeprom(String const &field, uint8 pos) {
		EEPROM.begin(128);

		for (uint8 i(0); i < field.length() + 1; ++i) {

			if (i == field.length()) {
				EEPROM.write(pos + i, '\0'); // Write char
			}
			else {
				EEPROM.write(pos + i, field.charAt(i)); // Write char
			}

		}

		EEPROM.commit();
		EEPROM.end();
	}

	String read_from_eeprom(uint8 pos, uint8 size) {
		EEPROM.begin(128);

		String res;

		for (uint8 i(pos); i < pos + size; ++i) {
			res += (char)EEPROM.read(i);
		}

		EEPROM.end();

		return res;
	}

	void eeprom_reset() {
		EEPROM.begin(128);
		EEPROM.write(0, 255);
		for (uint8 i(1); i < 128; ++i) {
			EEPROM.write(i, 0);
		}
		EEPROM.commit();
		EEPROM.end();
	}

	void esp_erase() {
		eeprom_reset();
		ESP.restart();
	}

	void dlay(uint32 n) {
		const ulong millisecond = millis() + n;
		while (millis() < millisecond) {
			yield();
		}
	}

}