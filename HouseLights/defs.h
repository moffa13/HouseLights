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