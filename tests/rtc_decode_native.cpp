#include "RtcI2c.h"
#include <cassert>

int main()
{
	using namespace rtc_i2c;
	assert(FromBcd(0x59) == 59);
	Value v;
	unsigned char regs[7] = {
		0x56, // sec
		0x34, // min
		0x18, // 24h hour 18
		0x01, // dow
		0x01, // day
		0x11, // month
		0x26  // year 2026
	};
	assert(Decode(regs, v));
	assert(v.year == 2026 && v.month == 11 && v.day == 1);
	assert(v.hour == 18 && v.minute == 34 && v.second == 56);

	unsigned char ampm[7] = {0x00, 0x00, 0x62, 0x01, 0x15, 0x03, 0x26}; // 12h mode, 2 PM → 14:00
	assert(Decode(ampm, v) && v.hour == 14);
	unsigned char midnight[7] = {0x00, 0x00, 0x52, 0x01, 0x15, 0x03, 0x26}; // 12h, 12 AM → 00:00
	assert(Decode(midnight, v) && v.hour == 0);

	unsigned char bad[7] = {0x60, 0x00, 0x00, 0x01, 0x01, 0x01, 0x26};
	assert(!Decode(bad, v));
	return 0;
}
