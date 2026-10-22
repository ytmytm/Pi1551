#ifndef PI1551_RTC_I2C_H
#define PI1551_RTC_I2C_H

// DS3231/DS1307 time registers (0..6). Probe/read only after HDMI/LCD.
namespace rtc_i2c {
struct Value { unsigned year, month, day, hour, minute, second; };
unsigned FromBcd(unsigned char value);
bool Decode(const unsigned char regs[7], Value& out);
bool Valid(const Value& value);
// Reads seven time bytes starting at register 0. Returns false on I2C or
// decode/validation failure. Does not touch the software wall clock.
bool Read(int bus, unsigned address, Value& out);
}
#endif
