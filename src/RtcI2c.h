#ifndef PI1551_RTC_I2C_H
#define PI1551_RTC_I2C_H

// DS3231/DS1307 time registers (0..6). Use only after HDMI/LCD.
namespace rtc_i2c {
enum class Type { DS3231, DS1307 };
struct Value { unsigned year, month, day, hour, minute, second; };
unsigned FromBcd(unsigned char value);
unsigned ToBcd(unsigned value);
bool Decode(const unsigned char regs[7], Value& out);
bool Encode(const Value& value, unsigned char regs[7]);
bool Valid(const Value& value);
bool Read(int bus, unsigned address, Value& out);
bool Write(int bus, unsigned address, Type type, const Value& value);
}
#endif
