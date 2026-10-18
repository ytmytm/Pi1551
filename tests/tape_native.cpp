#include "tape_player.h"
#include "options.h"
#include <cstdio>
#include <cstring>
Options options;
static bool pins[54];
static unsigned changes[54];
extern "C" void RPI_SetGpioInputPullUp(rpi_gpio_pin_t) {}
extern "C" void RPI_SetGpioOutput(rpi_gpio_pin_t) {}
extern "C" void RPI_SetGpioHi(rpi_gpio_pin_t p) { pins[p] = true; ++changes[p]; }
extern "C" void RPI_SetGpioLo(rpi_gpio_pin_t p) { pins[p] = false; ++changes[p]; }
extern "C" rpi_gpio_value_t RPI_GetGpioValue(rpi_gpio_pin_t p) { return pins[p] ? RPI_IO_HI : RPI_IO_LO; }
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL tape:%d %s\n",__LINE__,#x); return 1; } } while(0)
int main(int argc,char** argv) {
    CHECK(argc == 2);
    options.SetRuntime(oled::Setting::MotorAlways,0);
    pins[TAPE_MOTOR_GPIO] = false;
    TapePlayer tape; tape.InitializeGPIO(); tape.Tick10ms();
    CHECK(tape.GetMotorActive()); CHECK(!tape.GetTapeSenseState()); CHECK(!pins[TAPE_SENSE_GPIO]);
    FILINFO info = {}; strncpy(info.fname,argv[1],sizeof(info.fname)-1);
    FILE* file = fopen(argv[1],"rb"); CHECK(file != 0); fseek(file,0,SEEK_END); info.fsize = ftell(file); fclose(file);
    CHECK(tape.LoadTap(&info)); CHECK(tape.GetTapeSenseState()); CHECK(pins[TAPE_SENSE_GPIO]);
    TapeUIState s; tape.GetUIState(s); CHECK(s.tapeCounter == 0); CHECK(s.percentage == 0);
    CHECK(s.isLoaded); CHECK(s.isPlaying);
    CHECK(tape.Stop()); tape.GetUIState(s); CHECK(!s.isPlaying);
    CHECK(!tape.GetTapeSenseState()); CHECK(!pins[TAPE_SENSE_GPIO]);
    CHECK(tape.Play()); tape.GetUIState(s); CHECK(s.isPlaying);
    CHECK(tape.GetTapeSenseState()); CHECK(pins[TAPE_SENSE_GPIO]);
    CHECK(tape.GetMotorActive());
    pins[TAPE_MOTOR_GPIO] = true; tape.Tick10ms(); CHECK(!tape.GetMotorActive()); CHECK(tape.GetTapeSenseState());
    CHECK(tape.SeekCounter(25)); tape.GetUIState(s); CHECK(s.tapeCounter == 25); CHECK(s.currentPulseIndex > 0);
    CHECK(tape.SeekCounter(7)); tape.GetUIState(s); CHECK(s.tapeCounter == 7);
    CHECK(tape.SeekCounter(999)); CHECK(tape.AtEnd()); tape.Tick10ms(); CHECK(tape.GetTapeSenseState()); CHECK(pins[TAPE_SENSE_GPIO]);
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
    puts("PASS actual TAP parser, independent play/stop, autoplay, seek, rewind, eject and GPIO effects");
    return 0;
}
