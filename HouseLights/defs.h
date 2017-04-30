#pragma once
typedef struct {
	byte hour;
	byte minute;
	byte second;
} time_si;

typedef struct {
	boolean no_error;
	time_si result;
} time_safe;
