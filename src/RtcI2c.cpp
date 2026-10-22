#include "RtcI2c.h"
extern "C" {
#include "rpi-i2c.h"
}

namespace rtc_i2c {
unsigned FromBcd(unsigned char value)
{
	return ((value >> 4) * 10u) + (value & 0x0fu);
}

static bool Leap(unsigned year)
{
	return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static unsigned DaysInMonth(unsigned year, unsigned month)
{
	static const unsigned days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
	return month >= 1 && month <= 12 ? days[month] + (month == 2 && Leap(year)) : 0;
}

bool Valid(const Value& value)
{
	return value.year >= 2000 && value.year <= 2099 &&
		value.month >= 1 && value.month <= 12 &&
		value.day >= 1 && value.day <= DaysInMonth(value.year, value.month) &&
		value.hour < 24 && value.minute < 60 && value.second < 60;
}

bool Decode(const unsigned char regs[7], Value& out)
{
	out.second = FromBcd(regs[0] & 0x7f);
	out.minute = FromBcd(regs[1] & 0x7f);
	unsigned hourReg = regs[2];
	if (hourReg & 0x40) {
		// 12-hour mode: bit 5 is PM
		unsigned hour12 = FromBcd(hourReg & 0x1f);
		if (hour12 < 1 || hour12 > 12) return false;
		out.hour = (hour12 % 12) + ((hourReg & 0x20) ? 12 : 0);
	} else {
		out.hour = FromBcd(hourReg & 0x3f);
	}
	out.day = FromBcd(regs[4] & 0x3f);
	out.month = FromBcd(regs[5] & 0x1f);
	out.year = 2000 + FromBcd(regs[6]);
	return Valid(out);
}

bool Read(int bus, unsigned address, Value& out)
{
	if (address == 0 || address > 0x7f) return false;
	unsigned char reg = 0;
	unsigned char data[7];
	RPI_I2CInit(bus, 1);
	if (!RPI_I2CWrite(bus, static_cast<unsigned char>(address), &reg, 1))
		return false;
	if (!RPI_I2CRead(bus, static_cast<unsigned char>(address), data, 7))
		return false;
	return Decode(data, out);
}
}
