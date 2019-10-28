#pragma once
typedef struct {
	byte hour;
	byte minute;
	byte second;
} time_si;

typedef struct {
	boolean no_error;
	time_si sunset;
	time_si sunrise;
} time_safe;

typedef struct {
	boolean error;
	int error_type;
	int timezone;
} google_timezone_result;

struct rst_info {
	uint32 reason;
	uint32 exccause;
	uint32 epc1;
	uint32 epc2;
	uint32 epc3;
	uint32 excvaddr;
	uint32 depc;
};
