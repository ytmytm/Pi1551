#include "tape_player.h"
#include "options.h"
#include "rpiHardware.h"
#include "interrupt.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
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
static void RecordedHalfwave(TapePlayer& tape, unsigned micros) {
    nativeClock += micros;
    pins[TAPE_WRITE_GPIO] = !pins[TAPE_WRITE_GPIO];
    tape.RecordSample(nativeClock);
}
static void RecordedPulse(TapePlayer& tape, unsigned halfwaveMicros) {
    RecordedHalfwave(tape, halfwaveMicros);
    RecordedHalfwave(tape, halfwaveMicros);
}
static void RecordedBit(TapePlayer& tape, bool one) {
    RecordedPulse(tape, one ? 480 : 240);
    RecordedPulse(tape, one ? 240 : 480);
}
static void RecordedByte(TapePlayer& tape, unsigned value) {
    RecordedPulse(tape, 960); RecordedPulse(tape, 480);
    unsigned parity = 1;
    for (unsigned bit = 0; bit < 8; ++bit) {
        unsigned one = (value >> bit) & 1;
        RecordedBit(tape, one); parity ^= one;
    }
    RecordedBit(tape, parity);
}
static void RecordedHeaderPass(TapePlayer& tape, const char* name, bool repeat) {
    for (unsigned i = 0; i < 64; ++i) RecordedPulse(tape, 240);
    for (unsigned i = 0; i < 9; ++i) RecordedByte(tape, (repeat ? 0x09 : 0x89) - i);
    RecordedByte(tape, 1);
    unsigned checksum = 1;
    unsigned char data[191]; memset(data, ' ', sizeof(data));
    for (unsigned i = 0; i < 16 && name[i]; ++i) data[i + 4] = name[i];
    for (unsigned char c : data) { RecordedByte(tape, c); checksum ^= c; }
    RecordedByte(tape, checksum);
}
static void RecordedSave(TapePlayer& tape, const char* name) {
    RecordedHeaderPass(tape, name, false);
    RecordedHeaderPass(tape, name, true);
}
static std::string ReadNativeText(const std::string& path) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) return "";
    std::string result; char buffer[256]; size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) != 0) result.append(buffer, n);
    fclose(file); return result;
}
static size_t TapPulseAtOffset(const std::string& path, u32 offset) {
    std::string tap = ReadNativeText(path);
    size_t at = 20, pulse = 0;
    while (at < offset && at < tap.size()) { at += tap[at] ? 1 : 4; ++pulse; }
    return at == offset ? pulse : (size_t)-1;
}
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
    if (argc == 3 && strcmp(argv[2], "v1idx") == 0) {
        CHECK(!tape.SeekOffset(22)); // Middle of the extended v1 record.
        CHECK(tape.SeekOffset(21)); tape.GetUIState(s);
        CHECK(s.currentPulseIndex == 2 && !s.isPlaying);
        CHECK(tape.SeekOffset(25)); tape.GetUIState(s);
        CHECK(s.currentPulseIndex == 4);
        CHECK(tape.SeekOffset(20)); tape.GetUIState(s);
        CHECK(s.currentPulseIndex == 0);
        puts("PASS v1 TAP IDX byte offsets and extended record boundaries");
        return 0;
    }
    if (argc == 3 && strcmp(argv[2], "v1record") == 0) {
        CHECK(tape.SeekOffset(26));
        nativeClock = 1000; pins[TAPE_WRITE_GPIO] = false;
        CHECK(tape.Record());
        RecordedSave(tape, "NEW FILE");
        pins[TAPE_MOTOR_GPIO] = true; tape.RecordSample(++nativeClock);
        nativeClock += 500001;
        tape.Tick10ms(); tape.GetUIState(s);
        CHECK(!s.isRecording);
        std::string indexPath = std::string(argv[1]);
        indexPath.replace(indexPath.size() - 4, 4, ".idx");
        std::vector<oled::TapeIndexEntry> index;
        CHECK(oled::ParseUltimateTapeIndex(ReadNativeText(indexPath), index));
        CHECK(index.size() == 4);
        CHECK(index[0].name == "EARLY" && index[0].offset == 20);
        CHECK(index[1].name == "MIDDLE" && index[1].offset == 22);
        CHECK(index[2].name == "LATE" && index[2].offset == 24);
        CHECK(index[3].name == "new file" && index[3].offset > index[2].offset);
        FILE* converted = fopen(argv[1], "rb"); CHECK(converted);
        unsigned char header[20]; CHECK(fread(header, 1, sizeof(header), converted) == sizeof(header));
        CHECK(header[12] == 2); fclose(converted);
        for (size_t i = 0; i < index.size(); ++i) CHECK(tape.SeekOffset(index[i].offset));
        puts("PASS v1 IDX entries remapped after TAP v2 recording");
        return 0;
    }
    CHECK(!tape.SeekOffset(19));
    CHECK(!tape.SeekOffset(21)); // Inside a four-byte extended pulse.
    CHECK(tape.SeekOffset(24)); tape.GetUIState(s);
    CHECK(s.currentPulseIndex == 1 && !s.isPlaying && !tape.AtEnd());
    CHECK(tape.SeekOffset(20)); tape.GetUIState(s);
    CHECK(s.currentPulseIndex == 0);
    CHECK(!tape.SeekOffset(20 + info.fsize));
    CHECK(tape.Play()); tape.GetUIState(s);
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
    std::string autoTap = std::string(argv[1]) + ".auto.tap";
    std::string autoIdx = std::string(argv[1]) + ".auto.idx";
    pins[TAPE_MOTOR_GPIO] = false; pins[TAPE_WRITE_GPIO] = false; nativeClock = 1000;
    CHECK(tape.CreateTap(autoTap.c_str()));
    CHECK(tape.Record());
    // Start the first header on the opposite v2 half-wave phase.
    RecordedHalfwave(tape, 240);
    RecordedHalfwave(tape, 240);
    RecordedSave(tape, "FIRST FILE");
    pins[TAPE_MOTOR_GPIO] = true; tape.RecordSample(++nativeClock);
    nativeClock += 300000;
    tape.Tick10ms(); tape.GetUIState(s); CHECK(s.isRecording);
    pins[TAPE_MOTOR_GPIO] = false; tape.RecordSample(++nativeClock);
    RecordedPulse(tape, 240);
    pins[TAPE_MOTOR_GPIO] = true; tape.RecordSample(++nativeClock);
    nativeClock += 500000;
    tape.Tick10ms(); tape.GetUIState(s); CHECK(s.isRecording);
    ++nativeClock;
    tape.Tick10ms(); tape.GetUIState(s);
    CHECK(!s.isRecording && !s.isPlaying);
    std::vector<oled::TapeIndexEntry> index;
    CHECK(oled::ParseUltimateTapeIndex(ReadNativeText(autoIdx), index));
    CHECK(index.size() == 1 && index[0].name == "first file" && index[0].offset >= 20);
    CHECK((TapPulseAtOffset(autoTap, index[0].offset) & 1) == 0);
    u32 firstOffset = index[0].offset;
    pins[TAPE_MOTOR_GPIO] = false; tape.Tick10ms();
    CHECK(tape.Record());
    RecordedSave(tape, "SECOND FILE");
    pins[TAPE_MOTOR_GPIO] = true; tape.RecordSample(++nativeClock);
    nativeClock += 500001;
    tape.Tick10ms();
    CHECK(oled::ParseUltimateTapeIndex(ReadNativeText(autoIdx), index));
    CHECK(index.size() == 2 && index[0].name == "first file" && index[1].name == "second file");
    CHECK((TapPulseAtOffset(autoTap, index[0].offset) & 1) == 0);
    CHECK((TapPulseAtOffset(autoTap, index[1].offset) & 1) == 0);
    CHECK(index[0].offset == firstOffset && index[1].offset > firstOffset);
    CHECK(tape.SeekOffset(index[1].offset));
    tape.SetMotorAlwaysOn(true);
    CHECK(tape.Record());
    RecordedSave(tape, "THIRD FILE");
    pins[TAPE_MOTOR_GPIO] = true; tape.RecordSample(++nativeClock);
    tape.Tick10ms(); tape.GetUIState(s); CHECK(s.isRecording);
    CHECK(tape.Stop());
    tape.SetMotorAlwaysOn(false);
    CHECK(oled::ParseUltimateTapeIndex(ReadNativeText(autoIdx), index));
    CHECK(index.size() == 2 && index[0].name == "first file" && index[1].name == "third file");
    CHECK((TapPulseAtOffset(autoTap, index[1].offset) & 1) == 0);
    pins[TAPE_MOTOR_GPIO] = false; CHECK(tape.Record());
    RecordedSave(tape, "FOURTH FILE");
    NativeFatFSFail("rename");
    CHECK(!tape.Stop());
    NativeFatFSFail("rename");
    CHECK(!tape.Eject() && tape.IsLoaded());
    NativeFatFSFail("none");
    CHECK(tape.Eject());
    CHECK(oled::ParseUltimateTapeIndex(ReadNativeText(autoIdx), index));
    CHECK(index.size() == 3 && index[2].name == "fourth file");
    if (!getenv("PI1551_KEEP_RECORDED_TAP")) { remove(autoTap.c_str()); remove(autoIdx.c_str()); }
    std::string mismatchTap = std::string(argv[1]) + ".mismatch.tap";
    std::string mismatchIdx = std::string(argv[1]) + ".mismatch.idx";
    pins[TAPE_MOTOR_GPIO] = false; pins[TAPE_WRITE_GPIO] = false; nativeClock = 1000;
    CHECK(tape.CreateTap(mismatchTap.c_str())); CHECK(tape.Record());
    RecordedHeaderPass(tape, "FIRST", false);
    RecordedHeaderPass(tape, "DIFFERENT", true);
    pins[TAPE_MOTOR_GPIO] = true; tape.RecordSample(++nativeClock);
    nativeClock += 500001;
    tape.Tick10ms(); tape.GetUIState(s); CHECK(!s.isRecording);
    FILE* mismatch = fopen(mismatchIdx.c_str(), "rb"); CHECK(!mismatch);
    tape.Eject();
    if (!getenv("PI1551_KEEP_RECORDED_TAP")) remove(mismatchTap.c_str());
    puts("PASS TAP playback, recording, motor gating, persistence and GPIO effects");
    return 0;
}
