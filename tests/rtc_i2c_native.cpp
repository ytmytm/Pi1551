// Weak I2C stubs for host-side native tests (OLED uses I2C; no RTC module).
extern "C" void __attribute__((weak)) RPI_I2CInit(int, int) {}
extern "C" int __attribute__((weak)) RPI_I2CScan(int, unsigned char) { return 0; }
extern "C" int __attribute__((weak)) RPI_I2CRead(int, unsigned char, void*, unsigned) { return 0; }
extern "C" int __attribute__((weak)) RPI_I2CWrite(int, unsigned char, void*, unsigned) { return 0; }
