// Native GPIO bridge for the optional VICE cassette interoperability test.
// It runs the production recorder and playback core against a simulated clock.
#include "tape_player.h"
#include "options.h"
#include "rpiHardware.h"
#include "interrupt.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

Options options;
static std::atomic<u32> clockUs(0);
static std::atomic<bool> playbackClock(false), capturing(false);
static std::mutex edgeMutex;
static std::vector<u32> readEdges;
static bool pins[54] = {};
static u32 gpioRise, gpioFall, gpioEvents;
static IRQHandler* gpioHandler;
static void* gpioHandlerParam;

u32 NativeTapeIRQRead(u32 address)
{
    if (address == ARM_GPIO_GPREN0) return gpioRise;
    if (address == ARM_GPIO_GPFEN0) return gpioFall;
    if (address == ARM_GPIO_GPEDS0) return gpioEvents;
    return 0;
}
void NativeTapeIRQWrite(u32 address, u32 value)
{
    if (address == ARM_GPIO_GPREN0) gpioRise = value;
    if (address == ARM_GPIO_GPFEN0) gpioFall = value;
    if (address == ARM_GPIO_GPEDS0) gpioEvents &= ~value;
}
extern "C" void InterruptSystemConnectIRQ(unsigned index, IRQHandler* handler, void* param)
{
    if (index == ARM_IRQ_GPIO0) { gpioHandler = handler; gpioHandlerParam = param; }
}
extern "C" void InterruptSystemDisconnectIRQ(unsigned index)
{
    if (index == ARM_IRQ_GPIO0) { gpioHandler = nullptr; gpioHandlerParam = nullptr; }
}
static void NativeGPIOEdge(unsigned pin, bool high)
{
    bool previous = pins[pin];
    pins[pin] = high;
    if (previous == high) return;
    u32 bit = 1u << pin;
    if ((high ? gpioRise : gpioFall) & bit) {
        gpioEvents |= bit;
        if (gpioHandler) gpioHandler(gpioHandlerParam);
    }
}

unsigned Pi1551NativeClock()
{
    return playbackClock ? clockUs.fetch_add(1, std::memory_order_relaxed)
                         : clockUs.load(std::memory_order_relaxed);
}
extern "C" void RPI_SetGpioInputPullUp(rpi_gpio_pin_t) {}
extern "C" void RPI_SetGpioOutput(rpi_gpio_pin_t) {}
static void SetPin(rpi_gpio_pin_t pin, bool high)
{
    if (pins[pin] == high) return;
    pins[pin] = high;
    if (pin == TAPE_READ_GPIO && capturing) {
        std::lock_guard<std::mutex> lock(edgeMutex);
        readEdges.push_back(clockUs.load(std::memory_order_relaxed));
    }
}
extern "C" void RPI_SetGpioHi(rpi_gpio_pin_t pin) { SetPin(pin, true); }
extern "C" void RPI_SetGpioLo(rpi_gpio_pin_t pin) { SetPin(pin, false); }
extern "C" rpi_gpio_value_t RPI_GetGpioValue(rpi_gpio_pin_t pin)
{
    return pins[pin] ? RPI_IO_HI : RPI_IO_LO;
}

static bool FileInfo(const char* path, FILINFO& info)
{
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    bool ok = fseek(file, 0, SEEK_END) == 0;
    long size = ok ? ftell(file) : -1;
    fclose(file);
    if (size < 20 || strlen(path) >= sizeof(info.fname)) return false;
    memset(&info, 0, sizeof(info));
    strcpy(info.fname, path);
    info.fsize = static_cast<FSIZE_t>(size);
    return true;
}

static int RecordFromVice(const char* source, const char* target)
{
    FILE* file = fopen(source, "rb");
    if (!file) return 2;
    u8 header[20];
    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        memcmp(header, "C16-TAPE-RAW", 12) || header[12] != 1 || header[13] != 2) {
        fclose(file); return 3;
    }
    u32 dataSize = u32(header[16]) | u32(header[17]) << 8 |
                   u32(header[18]) << 16 | u32(header[19]) << 24;
    std::vector<u8> data(dataSize);
    bool complete = fread(data.data(), 1, dataSize, file) == dataSize;
    fclose(file);
    if (!complete) return 4;

    options.SetRuntime(oled::Setting::MotorAlways, 0);
    pins[TAPE_MOTOR_GPIO] = false;
    TapePlayer tape;
    tape.InitializeGPIO();
    tape.Tick10ms();
    if (!tape.CreateTap(target) || !tape.Record()) return 5;
    const double clockHz = header[14] == 1 ? 894886.0 : 886724.0;
    size_t halfWaves = 0;
    for (size_t i = 0; i < data.size();) {
        u32 cycles;
        if (data[i]) cycles = u32(data[i++]) * 8;
        else {
            if (i + 3 >= data.size()) return 6;
            cycles = u32(data[i + 1]) | u32(data[i + 2]) << 8 | u32(data[i + 3]) << 16;
            i += 4;
        }
        if (!cycles) continue;
        // VICE v1 stores full cycles. Its playback model divides each into
        // two half-waves; feed those transitions to the Pi1551 WRITE input.
        u32 fullUs = static_cast<u32>(cycles * 1000000.0 / clockHz + 0.5);
        u32 parts[2] = {fullUs / 2, fullUs - fullUs / 2};
        for (u32 part : parts) {
            clockUs += part ? part : 1;
            NativeGPIOEdge(TAPE_WRITE_GPIO, !pins[TAPE_WRITE_GPIO]);
            ++halfWaves;
        }
    }
    if (!tape.Stop()) return 7;
    TapeUIState state;
    tape.GetUIState(state);
    printf("Pi1551 recorded %zu half-waves (%u ms)\n", halfWaves, state.totalTimeMs);
    tape.Eject();
    return 0;
}

static void WritePulse(FILE* file, u32 durationUs, u32& size, double clockHz)
{
    u32 cycles = static_cast<u32>(durationUs * clockHz / 1000000.0 + 0.5);
    u32 units = (cycles + 4) / 8;
    if (units && units <= 255) {
        u8 byte = static_cast<u8>(units);
        fwrite(&byte, 1, 1, file); ++size;
    } else {
        if (cycles > 0xffffff) cycles = 0xffffff;
        u8 bytes[4] = {0, static_cast<u8>(cycles), static_cast<u8>(cycles >> 8), static_cast<u8>(cycles >> 16)};
        fwrite(bytes, 1, 4, file); size += 4;
    }
}

static int PlayToTap(const char* source, const char* target)
{
    FILINFO info;
    if (!FileInfo(source, info)) return 2;
    options.SetRuntime(oled::Setting::MotorAlways, 0);
    options.SetRuntime(oled::Setting::TapeAutoplay, 0);
    pins[TAPE_MOTOR_GPIO] = false;
    playbackClock = true;
    TapePlayer tape;
    std::thread core([&] { tape.CoreLoop(); });
    core.detach(); // CoreLoop is firmware's permanent core-2 service.
    if (!tape.LoadTap(&info)) return 3;
    TapeUIState state;
    tape.GetUIState(state);
    u32 start = clockUs.load();
    capturing = true;
    if (!tape.Play()) return 4;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!tape.AtEnd()) {
        if (std::chrono::steady_clock::now() > deadline) return 5;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    capturing = false;
    std::vector<u32> edges;
    { std::lock_guard<std::mutex> lock(edgeMutex); edges = readEdges; }
    if (edges.size() < 100) return 6;

    FILE* file = fopen(target, "wb");
    if (!file) return 7;
    u8 header[20] = {};
    memcpy(header, "C16-TAPE-RAW", 12);
    header[12] = 2; header[13] = 2;
    FILE* sourceFile = fopen(source, "rb");
    if (!sourceFile) { fclose(file); return 8; }
    u8 sourceHeader[20];
    bool headerRead = fread(sourceHeader, 1, sizeof(sourceHeader), sourceFile) == sizeof(sourceHeader);
    fclose(sourceFile);
    if (!headerRead) { fclose(file); return 9; }
    header[14] = sourceHeader[14];
    double clockHz = header[14] == 1 ? 894886.0 : 886724.0;
    fwrite(header, 1, sizeof(header), file);
    u32 bytes = 0, previous = start;
    for (u32 edge : edges) {
        u32 duration = edge - previous;
        if (duration) WritePulse(file, duration, bytes, clockHz);
        previous = edge;
    }
    if (edges.size() & 1) WritePulse(file, 1, bytes, clockHz);
    header[16] = bytes; header[17] = bytes >> 8; header[18] = bytes >> 16; header[19] = bytes >> 24;
    fseek(file, 0, SEEK_SET);
    bool ok = fwrite(header, 1, sizeof(header), file) == sizeof(header);
    ok = fclose(file) == 0 && ok;
    if (!ok) return 10;
    printf("Pi1551 played %s: %zu READ edges, %u ms\n", source, edges.size(), state.totalTimeMs);
    fflush(stdout);
    std::_Exit(0); // The firmware playback core intentionally never returns.
}

int main(int argc, char** argv)
{
    if (argc != 4) {
        fputs("usage: tape_interop_native <record|play> source.tap target.tap\n", stderr);
        return 1;
    }
    if (!strcmp(argv[1], "record")) return RecordFromVice(argv[2], argv[3]);
    if (!strcmp(argv[1], "play")) return PlayToTap(argv[2], argv[3]);
    return 1;
}
