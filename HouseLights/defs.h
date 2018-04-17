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

struct rst_info {
	uint32 reason;
	uint32 exccause;
	uint32 epc1;
	uint32 epc2;
	uint32 epc3;
	uint32 excvaddr;
	uint32 depc;
};
