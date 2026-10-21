#include "tape_player.h"
#include "options.h"
#include "rpiHardware.h"
#include "interrupt.h"
#include <cstdio>
#include <cstring>
#include <string>
Options options;
void NativeFatFSFail(const std::string& operation);
static unsigned nativeClock = 0;
unsigned Pi1551NativeClock() { return nativeClock; }
static bool pins[54];
static unsigned changes[54];
static u32 gpioRise, gpioFall, gpioEvents;
static IRQHandler* gpioHandler;
static void* gpioHandlerParam;
u32 NativeTapeIRQRead(u32 address) {
    if (address == ARM_GPIO_GPREN0) return gpioRise;
    if (address == ARM_GPIO_GPFEN0) return gpioFall;
    if (address == ARM_GPIO_GPEDS0) return gpioEvents;
    return 0;
}
void NativeTapeIRQWrite(u32 address, u32 value) {
    if (address == ARM_GPIO_GPREN0) gpioRise = value;
    if (address == ARM_GPIO_GPFEN0) gpioFall = value;
    if (address == ARM_GPIO_GPEDS0) gpioEvents &= ~value;
}
extern "C" void InterruptSystemConnectIRQ(unsigned index, IRQHandler* handler, void* param) {
    if (index == ARM_IRQ_GPIO0) { gpioHandler = handler; gpioHandlerParam = param; }
}
extern "C" void InterruptSystemDisconnectIRQ(unsigned index) {
    if (index == ARM_IRQ_GPIO0) { gpioHandler = nullptr; gpioHandlerParam = nullptr; }
}
static void NativeGPIOEdge(unsigned pin, bool high, unsigned now) {
    bool previous = pins[pin];
    nativeClock = now;
    pins[pin] = high;
    if (previous == high) return;
    u32 bit = 1u << pin;
    if ((high ? gpioRise : gpioFall) & bit) {
        gpioEvents |= bit;
        if (gpioHandler) gpioHandler(gpioHandlerParam);
    }
}
extern "C" void RPI_SetGpioInputPullUp(rpi_gpio_pin_t) {}
extern "C" void RPI_SetGpioOutput(rpi_gpio_pin_t) {}
extern "C" void RPI_SetGpioHi(rpi_gpio_pin_t p) { pins[p] = true; ++changes[p]; }
extern "C" void RPI_SetGpioLo(rpi_gpio_pin_t p) { pins[p] = false; ++changes[p]; }
extern "C" rpi_gpio_value_t RPI_GetGpioValue(rpi_gpio_pin_t p) { return pins[p] ? RPI_IO_HI : RPI_IO_LO; }
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL tape:%d %s\n",__LINE__,#x); return 1; } } while(0)
int main(int argc,char** argv) {
    CHECK(argc == 2 || argc == 3);
    options.SetRuntime(oled::Setting::MotorAlways,0);
    pins[TAPE_MOTOR_GPIO] = false;
    TapePlayer tape; tape.InitializeGPIO(); tape.Tick10ms();
    CHECK(tape.GetMotorActive()); CHECK(!tape.GetTapeSenseState()); CHECK(!pins[TAPE_SENSE_GPIO]);
    FILINFO info = {}; strncpy(info.fname,argv[1],sizeof(info.fname)-1);
    FILE* file = fopen(argv[1],"rb"); CHECK(file != 0); fseek(file,0,SEEK_END); info.fsize = ftell(file); fclose(file);
    CHECK(tape.LoadTap(&info)); CHECK(tape.GetTapeSenseState()); CHECK(pins[TAPE_SENSE_GPIO]);
    TapeUIState s; tape.GetUIState(s); CHECK(s.tapeCounter == 0); CHECK(s.percentage == 0);
    if (argc == 3 && strcmp(argv[2],"gap67") == 0) {
        CHECK(s.maxTapeCounter == 67);
        for (unsigned counter : {65u,66u}) {
            CHECK(tape.SeekCounter(counter)); tape.GetUIState(s);
            CHECK(s.tapeCounter == counter);
        }
        puts("PASS go-to-counter seeks inside long TAP pulse gaps");
        return 0;
    }
    if (argc == 3) {
        CHECK(s.maxTapeCounter == 999);
        CHECK(tape.SeekCounter(999)); CHECK(!tape.AtEnd());
        puts("PASS long TAP counter range is capped at 999");
        return 0;
    }
    CHECK(s.maxTapeCounter > 0 && s.maxTapeCounter < 999);
    CHECK(s.isLoaded); CHECK(s.isPlaying);
    CHECK(tape.Stop()); tape.GetUIState(s); CHECK(!s.isPlaying);
    CHECK(!tape.GetTapeSenseState()); CHECK(!pins[TAPE_SENSE_GPIO]);
    CHECK(tape.Play()); tape.GetUIState(s); CHECK(s.isPlaying);
    CHECK(tape.GetTapeSenseState()); CHECK(pins[TAPE_SENSE_GPIO]);
    CHECK(tape.GetMotorActive());
    pins[TAPE_MOTOR_GPIO] = true; tape.Tick10ms(); CHECK(!tape.GetMotorActive()); CHECK(tape.GetTapeSenseState());
    CHECK(tape.SeekCounter(25)); tape.GetUIState(s); CHECK(s.tapeCounter == 25 && !s.isPlaying); CHECK(s.currentPulseIndex > 0);
    CHECK(tape.SeekCounter(7)); tape.GetUIState(s); CHECK(s.tapeCounter == 7);
    CHECK(tape.SeekCounter(999)); tape.GetUIState(s); CHECK(s.tapeCounter == s.maxTapeCounter);
    CHECK(tape.AtEnd()); tape.Tick10ms(); CHECK(!tape.GetTapeSenseState()); CHECK(!pins[TAPE_SENSE_GPIO]);
    tape.Reset(); tape.GetUIState(s); CHECK(s.tapeCounter == 0); CHECK(!tape.AtEnd());
    tape.SetMotorAlwaysOn(true); CHECK(tape.GetMotorActive());
    tape.SetMotorAlwaysOn(false); CHECK(!tape.GetMotorActive());
    pins[TAPE_MOTOR_GPIO] = false; tape.Tick10ms(); CHECK(tape.GetMotorActive());
    tape.Eject(); CHECK(!tape.IsLoaded()); CHECK(!tape.GetTapeSenseState()); CHECK(!pins[TAPE_SENSE_GPIO]); CHECK(tape.GetMotorActive());
    pins[TAPE_MOTOR_GPIO] = true; tape.Tick10ms(); tape.GetUIState(s);
    CHECK(!s.isLoaded); CHECK(!s.motorActive);
    pins[TAPE_MOTOR_GPIO] = false; tape.Tick10ms(); tape.GetUIState(s);
    CHECK(!s.isLoaded); CHECK(s.motorActive);
    CHECK(!tape.SeekCounter(5)); tape.GetUIState(s); CHECK(!s.isLoaded); CHECK(!s.filename[0]);
    CHECK(changes[TAPE_SENSE_GPIO] > 5);
    CHECK(!tape.LoadTap(0)); CHECK(!tape.GetTapeSenseState()); CHECK(tape.GetMotorActive());
    options.SetRuntime(oled::Setting::TapeAutoplay,0);
    CHECK(tape.LoadTap(&info)); tape.GetUIState(s);
    CHECK(s.isLoaded); CHECK(!s.isPlaying); CHECK(!tape.GetTapeSenseState());
    CHECK(tape.Play()); tape.GetUIState(s); CHECK(s.isPlaying);
    CHECK(tape.Stop()); tape.GetUIState(s); CHECK(!s.isPlaying);
    tape.Eject(); CHECK(!tape.IsLoaded());
    std::string untouched = std::string(argv[1]) + ".untouched.tap";
    pins[TAPE_MOTOR_GPIO] = true;
    CHECK(tape.CreateTap(untouched.c_str()));
    CHECK(tape.Record());
    NativeFatFSFail("write");
    CHECK(tape.Stop()); // No pulse changed, so STOP must not write the TAP.
    FILE* untouchedFile = fopen(untouched.c_str(), "rb"); CHECK(untouchedFile);
    CHECK(fseek(untouchedFile, 0, SEEK_END) == 0 && ftell(untouchedFile) == 24);
    fclose(untouchedFile);
    NativeFatFSFail("none");
    tape.Eject();
    if (!getenv("PI1551_KEEP_RECORDED_TAP")) remove(untouched.c_str());
    std::string irqRecorded = std::string(argv[1]) + ".irq.tap";
    pins[TAPE_MOTOR_GPIO] = true;
    pins[TAPE_WRITE_GPIO] = false;
    CHECK(tape.CreateTap(irqRecorded.c_str()));
    nativeClock = 1000;
    CHECK(tape.Record());
    const u32 motorBit = 1u << TAPE_MOTOR_GPIO;
    const u32 writeBit = 1u << TAPE_WRITE_GPIO;
    CHECK(gpioHandler && (gpioRise & motorBit) && (gpioFall & motorBit));
    CHECK(!(gpioRise & writeBit) && !(gpioFall & writeBit));
    NativeGPIOEdge(TAPE_WRITE_GPIO, true, 1500);
    CHECK(!(gpioEvents & writeBit));
    NativeGPIOEdge(TAPE_MOTOR_GPIO, false, 2000);
    CHECK((gpioRise & writeBit) && (gpioFall & writeBit));
    NativeGPIOEdge(TAPE_WRITE_GPIO, false, 3000);
    NativeGPIOEdge(TAPE_WRITE_GPIO, true, 4000);
    NativeGPIOEdge(TAPE_MOTOR_GPIO, true, 5000);
    CHECK(!(gpioRise & writeBit) && !(gpioFall & writeBit));
    NativeGPIOEdge(TAPE_WRITE_GPIO, false, 5500);
    CHECK(!(gpioEvents & writeBit));
    // A stopped motor in the middle of RECORD must not add 15 seconds to TAP.
    NativeGPIOEdge(TAPE_MOTOR_GPIO, false, 15005000);
    CHECK((gpioRise & writeBit) && (gpioFall & writeBit));
    NativeGPIOEdge(TAPE_WRITE_GPIO, true, 15006000);
    NativeGPIOEdge(TAPE_WRITE_GPIO, false, 15007000);
    NativeGPIOEdge(TAPE_MOTOR_GPIO, true, 15008000);
    CHECK(tape.Stop());
    CHECK(!gpioHandler && !(gpioRise & motorBit) && !(gpioFall & motorBit));
    tape.GetUIState(s);
    FILINFO irqInfo = {}; strncpy(irqInfo.fname,irqRecorded.c_str(),sizeof(irqInfo.fname)-1);
    FILE* irqFile = fopen(irqRecorded.c_str(), "rb"); CHECK(irqFile);
    fseek(irqFile,0,SEEK_END); irqInfo.fsize = ftell(irqFile); fclose(irqFile);
    CHECK(tape.LoadTap(&irqInfo)); tape.GetUIState(s);
    CHECK(s.totalTimeMs >= 6 && s.totalTimeMs <= 7);
    tape.Eject();
    if (!getenv("PI1551_KEEP_RECORDED_TAP")) remove(irqRecorded.c_str());
    pins[TAPE_MOTOR_GPIO] = false;
    std::string recorded = std::string(argv[1]) + ".record.tap";
    CHECK(tape.CreateTap(recorded.c_str()));
    CHECK(tape.IsLoaded());
    FILE* emptyFile = fopen(recorded.c_str(), "rb"); CHECK(emptyFile);
    CHECK(fseek(emptyFile,0,SEEK_END) == 0 && ftell(emptyFile) == 24);
    fclose(emptyFile);
    CHECK(tape.Record()); tape.GetUIState(s);
    CHECK(s.isRecording && s.isPlaying && tape.GetTapeSenseState());
    CHECK(!tape.Play());
    nativeClock = 1000; pins[TAPE_WRITE_GPIO] = false; tape.RecordSample(nativeClock);
    nativeClock = 1400; pins[TAPE_WRITE_GPIO] = true; tape.RecordSample(nativeClock);
    pins[TAPE_MOTOR_GPIO] = true;
    nativeClock = 2000; pins[TAPE_WRITE_GPIO] = false; tape.RecordSample(nativeClock);
    pins[TAPE_MOTOR_GPIO] = false;
    nativeClock = 3000; tape.RecordSample(nativeClock);
    nativeClock = 3600; pins[TAPE_WRITE_GPIO] = true; tape.RecordSample(nativeClock);
    nativeClock = 4000; CHECK(tape.Rewind()); tape.GetUIState(s);
    CHECK(!s.isRecording && !s.isPlaying && !tape.GetTapeSenseState());
    CHECK(s.currentTimeMs == 0 && s.tapeCounter == 0);
    FILE* recordedFile = fopen(recorded.c_str(), "rb"); CHECK(recordedFile);
    unsigned char header[20]; CHECK(fread(header,1,20,recordedFile) == 20);
    CHECK(memcmp(header,"C16-TAPE-RAW",12) == 0 && header[12] == 2 && header[13] == 2);
    CHECK(header[16] != 0); fclose(recordedFile);
    FILINFO recordedInfo = {}; strncpy(recordedInfo.fname,recorded.c_str(),sizeof(recordedInfo.fname)-1);
    recordedFile = fopen(recorded.c_str(),"rb"); CHECK(recordedFile);
    fseek(recordedFile,0,SEEK_END); recordedInfo.fsize = ftell(recordedFile); fclose(recordedFile);
    CHECK(tape.LoadTap(&recordedInfo)); tape.GetUIState(s);
    CHECK(s.isLoaded && s.totalTimeMs >= 1);
    CHECK(tape.Stop()); CHECK(tape.Record());
    nativeClock = 5000; pins[TAPE_WRITE_GPIO] = false; tape.RecordSample(nativeClock);
    nativeClock = 5300; pins[TAPE_WRITE_GPIO] = true; tape.RecordSample(nativeClock);
    nativeClock = 5400; NativeFatFSFail("write"); CHECK(!tape.SeekCounter(5));
    tape.GetUIState(s); CHECK(s.currentPulseIndex > 0);
    CHECK(tape.SeekCounter(0)); // Retry the failed flush, then seek to the start.
    tape.GetUIState(s); CHECK(s.currentTimeMs == 0 && s.tapeCounter == 0);
    tape.Eject();
    if (!getenv("PI1551_KEEP_RECORDED_TAP")) remove(recorded.c_str());
    puts("PASS TAP playback, recording, motor gating, persistence and GPIO effects");
    return 0;
}
