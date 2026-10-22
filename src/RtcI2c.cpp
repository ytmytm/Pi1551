#include "RtcI2c.h"
extern "C" {
#include "rpi-i2c.h"
}

namespace rtc_i2c {
unsigned FromBcd(unsigned char value)
{
	return ((value >> 4) * 10u) + (value & 0x0fu);
}

unsigned ToBcd(unsigned value)
{
	return ((value / 10u) << 4) | (value % 10u);
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

bool Encode(const Value& value, unsigned char regs[7])
{
	if (!Valid(value)) return false;
	regs[0] = static_cast<unsigned char>(ToBcd(value.second)); // CH/OSF clear
	regs[1] = static_cast<unsigned char>(ToBcd(value.minute));
	regs[2] = static_cast<unsigned char>(ToBcd(value.hour)); // 24-hour
	regs[3] = 1;
	regs[4] = static_cast<unsigned char>(ToBcd(value.day));
	regs[5] = static_cast<unsigned char>(ToBcd(value.month));
	regs[6] = static_cast<unsigned char>(ToBcd(value.year % 100));
	return true;
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

bool Write(int bus, unsigned address, Type type, const Value& value)
{
	if (address == 0 || address > 0x7f) return false;
	unsigned char regs[7];
	if (!Encode(value, regs)) return false;
	unsigned char packet[8];
	packet[0] = 0;
	for (unsigned i = 0; i < 7; ++i) packet[i + 1] = regs[i];
	RPI_I2CInit(bus, 1);
	if (!RPI_I2CWrite(bus, static_cast<unsigned char>(address), packet, 8))
		return false;
	if (type == Type::DS3231) {
		// Clear oscillator-stop flag; keep control with oscillator enabled.
		unsigned char status[2] = {0x0f, 0x00};
		unsigned char control[2] = {0x0e, 0x00};
		if (!RPI_I2CWrite(bus, static_cast<unsigned char>(address), control, 2))
			return false;
		if (!RPI_I2CWrite(bus, static_cast<unsigned char>(address), status, 2))
			return false;
	}
	return true;
}
}
