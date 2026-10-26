#include "Pi1551.h"
#include "options.h"
#include "ROMs.h"
#include <cstdio>
#include <cstring>

Options options;
ROMs roms;
Pi1551 pi1551;
u8 s_u8Memory[0xc000];

static unsigned gpioSamples = 0;
static bool updateStatus = false;

// Keep the real CPU decoder and TIA. Substitute only the physical GPIO sample.
void TCBM_Bus::ReadEmulationMode1551(bool updateTIAStatus) {
    ++gpioSamples;
    updateStatus = updateTIAStatus;
    pi1551.TPI.GetPortA()->SetInput(0xa5);
    pi1551.TPI.GetPortC()->SetInput(0x93);
}
void TCBM_Bus::RefreshOuts1551() {}

extern u8 read6502_1551(u16 address);
#include "reference/read6502_1551.inc"

struct ReadResult {
    u8 value, portA, portB, portC, portCPU;
    unsigned samples;
    bool statusUpdated;
};

static ReadResult Read(u8 (*callback)(u16), u16 address) {
    pi1551.TPI.GetPortA()->SetInput(0x12);
    pi1551.TPI.GetPortB()->SetInput(0x34);
    pi1551.TPI.GetPortC()->SetInput(0x56);
    pi1551.TPI.GetPortCPU()->SetInput(0xf8);
    gpioSamples = 0;
    updateStatus = false;
    const u8 value = callback(address);
    ReadResult result = {value, pi1551.TPI.GetPortA()->GetInput(),
        pi1551.TPI.GetPortB()->GetInput(), pi1551.TPI.GetPortC()->GetInput(),
        pi1551.TPI.GetPortCPU()->GetInput(), gpioSamples, updateStatus};
    return result;
}

static bool Equal(const ReadResult& a, const ReadResult& b) {
    return a.value == b.value && a.portA == b.portA && a.portB == b.portB &&
        a.portC == b.portC && a.portCPU == b.portCPU && a.samples == b.samples &&
        a.statusUpdated == b.statusUpdated;
}

int main() {
    unsigned random = 0x15516502;
    for (unsigned slot = 0; slot < ROMs::MAX_ROMS; ++slot)
        for (unsigned offset = 0; offset < ROMs::ROM1551_SIZE; ++offset) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            roms.ROMImage1551[slot][offset] = random;
        }
    for (unsigned address = 0; address < sizeof(s_u8Memory); ++address) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        s_u8Memory[address] = random;
    }
    const unsigned sizes[] = {16384, 32768, 0, 12345};
    unsigned long long reads = 0;
    for (unsigned slot = 0; slot < ROMs::MAX_ROMS; ++slot) {
        roms.currentROMIndex = slot;
        for (unsigned size : sizes) {
            roms.ROM1551Sizes[slot] = size;
            for (unsigned ramBoard = 0; ramBoard < 2; ++ramBoard) {
                options.SetRuntime(oled::Setting::RAM, ramBoard);
                for (unsigned direction : {0u, 0xffu, 0x55u, 0xaau}) {
                    pi1551.TPI.GetPortA()->SetDirection(direction);
                    pi1551.TPI.GetPortB()->SetDirection(direction);
                    pi1551.TPI.GetPortC()->SetDirection(direction);
                    pi1551.TPI.GetPortCPU()->SetDirection(direction);
                    pi1551.TPI.GetPortA()->SetOutput(0x69);
                    pi1551.TPI.GetPortB()->SetOutput(0x96);
                    pi1551.TPI.GetPortC()->SetOutput(0x87);
                    pi1551.TPI.GetPortCPU()->SetOutput(0x78);
                    for (unsigned address = 0; address < 65536; ++address) {
                        ReadResult expected = Read(read6502_1551_reference, address);
                        ReadResult actual = Read(read6502_1551, address);
                        if (!Equal(expected, actual)) {
                            std::fprintf(stderr, "Mismatch: slot %u, ROM %u, RAMBOard %u, DDR %02x, address %04x\n",
                                         slot, size, ramBoard, direction, address);
                            return 1;
                        }
                        ++reads;
                    }
                }
            }
        }
    }
    std::printf("PASS %llu CPU reads: all addresses, seven ROM slots, 16/32K and unknown ROM sizes, RAMBOard and TIA side effects\n", reads);
}
