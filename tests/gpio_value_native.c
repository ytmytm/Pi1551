#include "rpi-gpio.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    static rpi_gpio_t gpio;
    RPI_GpioBase = &gpio;

    gpio.GPLEV0[0] = 0;
    assert(RPI_GetGpioValue(RPI_GPIO26) == RPI_IO_LO);
    gpio.GPLEV0[0] = 1u << 26;
    assert(RPI_GetGpioValue(RPI_GPIO26) == RPI_IO_HI);

    gpio.GPLEV0[0] = 1u << 6;
    assert(RPI_GetGpioValue(RPI_GPIO6) == RPI_IO_HI);
    assert(RPI_GetGpioValue(RPI_GPIO26) == RPI_IO_LO);

    gpio.GPLEV0[1] = 1u << (RPI_GPIO33 - 32);
    assert(RPI_GetGpioValue(RPI_GPIO33) == RPI_IO_HI);
    assert(RPI_GetGpioValue((rpi_gpio_pin_t)64) == RPI_IO_UNKNOWN);
    puts("PASS GPIO level read on high-numbered pins");
    return 0;
}
