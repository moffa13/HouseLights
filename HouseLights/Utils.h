#ifndef UTILS_H
#define UTILS_H

#include <WString.h>
#include <EEPROM.h>

namespace Utils {

	void write_to_eeprom(String const &field, uint8 pos);

	String read_from_eeprom(uint8 pos, uint8 size);

	void eeprom_reset();

	void esp_erase();

	void dlay(uint32 n);

}

#endif
