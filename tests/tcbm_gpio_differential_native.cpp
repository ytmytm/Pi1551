// Pi1551 GPIO differential test. GPL-3.0-or-later.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <tuple>
#include <vector>
#include "tcbm_bus.h"
#include "options.h"

Options options;
using Event = std::array<u32, 3>;
static std::vector<Event> events;
static u32 latch;
static std::array<unsigned, 32> functions{}, pullups{};
extern "C" u32 Pi1551NativeRead32(unsigned address)
{
    events.push_back({0, address, latch});
    return latch;
}
extern "C" void Pi1551NativeWrite32(unsigned address, u32 value)
{
    events.push_back({1, address, value});
    if (address == ARM_GPIO_GPCLR0) latch &= ~value;
    else if (address == ARM_GPIO_GPSET0) latch |= value;
    else std::abort();
}
extern "C" void RPI_SetGpioPinFunction(rpi_gpio_pin_t pin, rpi_gpio_alt_function_t function)
{
    events.push_back({2, unsigned(pin), unsigned(function)});
    functions.at(pin) = function;
}
extern "C" void RPI_SetGpioInputPullUp(rpi_gpio_pin_t pin)
{
    events.push_back({3, unsigned(pin), 0});
    functions.at(pin) = FS_INPUT;
    pullups.at(pin) = 1;
}
#include "reference/refresh_outs1551.inc"

struct TCBMGpioDifferentialTest {
    static auto State() -> decltype(std::tie(TCBM_Bus::lastDirA, TCBM_Bus::lastDirC,
        TCBM_Bus::lastOutA, TCBM_Bus::lastOutC, TCBM_Bus::lastSet, TCBM_Bus::lastClear,
        TCBM_Bus::lastOutputLED, TCBM_Bus::lastOutputSound,
        TCBM_Bus::ledSoundOutputValid, TCBM_Bus::lastSoundGpioEnabled))
    {
        return std::tie(TCBM_Bus::lastDirA, TCBM_Bus::lastDirC,
            TCBM_Bus::lastOutA, TCBM_Bus::lastOutC, TCBM_Bus::lastSet, TCBM_Bus::lastClear,
            TCBM_Bus::lastOutputLED, TCBM_Bus::lastOutputSound,
            TCBM_Bus::ledSoundOutputValid, TCBM_Bus::lastSoundGpioEnabled);
    }
    static void ResetCache()
    {
        State() = std::make_tuple(u8(255), u8(255), u8(255), u8(255), 0u, 0u,
                                 false, false, false, false);
    }
    static unsigned long checks;
    static void Check()
    {
        // Copy tuple values, rather than retaining references to the live cache.
        const auto before = std::make_tuple(TCBM_Bus::lastDirA, TCBM_Bus::lastDirC,
            TCBM_Bus::lastOutA, TCBM_Bus::lastOutC, TCBM_Bus::lastSet, TCBM_Bus::lastClear,
            TCBM_Bus::lastOutputLED, TCBM_Bus::lastOutputSound,
            TCBM_Bus::ledSoundOutputValid, TCBM_Bus::lastSoundGpioEnabled);
        const auto latchBefore = latch;
        const auto functionsBefore = functions, pullupsBefore = pullups;
        events.clear();
        TCBM_Bus::ReferenceRefreshOuts1551();
        const auto expectedEvents = events;
        auto after = before;
        after = State();
        const auto expectedLatch = latch;
        const auto expectedFunctions = functions, expectedPullups = pullups;
        State() = before;
        latch = latchBefore; functions = functionsBefore; pullups = pullupsBefore;
        events.clear();
        TCBM_Bus::RefreshOuts1551();
        ++checks;
        if (events != expectedEvents || State() != after || latch != expectedLatch ||
            functions != expectedFunctions || pullups != expectedPullups) {
            std::fprintf(stderr, "GPIO mismatch at refresh %lu\n", checks);
            std::abort();
        }
    }
};
unsigned long TCBMGpioDifferentialTest::checks;
static u32 seed = 0x1551;
static u32 Random() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }

int main()
{
    m6523 tpi;
    auto* a = tpi.GetPortA(); auto* c = tpi.GetPortC();
    TCBM_Bus::TPI = &tpi; TCBM_Bus::port = a;
    // All-0xff is also the initial cache sentinel: the unchanged check must
    // keep its reference behavior, and the next data change must fully sync.
    TCBMGpioDifferentialTest::ResetCache();
    a->SetDirection(255); a->SetOutput(255);
    c->SetDirection(255); c->SetOutput(255);
    TCBMGpioDifferentialTest::Check();
    a->SetOutput(0); TCBMGpioDifferentialTest::Check();
    // Every byte-to-byte transition with all data pins driven, across control states.
    for (unsigned mode = 0; mode < 8; ++mode) {
        a->SetDirection(255); c->SetDirection(mode == 0 ? 255 : mode * 19);
        c->SetOutput(mode == 0 ? 255 : mode * 37);
        TCBMGpioDifferentialTest::ResetCache();
        for (unsigned from = 0; from < 256; ++from)
            for (unsigned to = 0; to < 256; ++to) {
                a->SetOutput(from); TCBMGpioDifferentialTest::Check();
                a->SetOutput(to); TCBMGpioDifferentialTest::Check();
                TCBMGpioDifferentialTest::Check(); // unchanged output
            }
    }
    // All data direction/data combinations and all control direction/data combinations.
    for (unsigned dir = 0; dir < 256; ++dir)
        for (unsigned data = 0; data < 256; ++data) {
            a->SetDirection(dir); a->SetOutput(data);
            c->SetDirection(dir ^ 0x1c); c->SetOutput(data ^ 0xa5);
            TCBMGpioDifferentialTest::Check();
            a->SetDirection(255); a->SetOutput(data ^ dir);
            c->SetDirection(dir); c->SetOutput(data);
            TCBMGpioDifferentialTest::Check();
        }
    for (unsigned i = 0; i < 200000; ++i) {
        const auto r = Random();
        if ((r & 7) == 0) a->SetDirection(Random());
        if ((r & 15) == 0) c->SetDirection(Random());
        if ((r & 3) == 0) c->SetOutput(Random());
        a->SetOutput(Random());
        TCBM_Bus::OutputLED = r & 0x10; TCBM_Bus::OutputSound = r & 0x20;
        options.SetRuntime(oled::Setting::Sound, (r >> 6) & 1);
        options.SetRuntime(oled::Setting::SoundMode, (r >> 7) & 1);
        TCBM_Bus::TPI = (r & 0xff) == 0 ? nullptr : &tpi;
        TCBM_Bus::port = (r & 0xff00) == 0 ? nullptr : a;
        if ((r & 0x3fff) == 0) TCBMGpioDifferentialTest::ResetCache();
        TCBMGpioDifferentialTest::Check();
    }
    std::printf("GPIO: %lu refreshes match frozen reference, including ordered writes and directions\n",
                TCBMGpioDifferentialTest::checks);
}
