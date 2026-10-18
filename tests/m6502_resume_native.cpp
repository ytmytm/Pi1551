#include "m6502.h"
#include <stdio.h>
#include <string.h>

static u8 memory[65536];
static unsigned alternateReads;

static u8 ReadOriginal(u16 address) { return memory[address]; }
static u8 ReadAlternate(u16 address) { ++alternateReads; return memory[address]; }
static void WriteOriginal(u16 address, const u8 value) { memory[address] = value; }
static void WriteAlternate(u16 address, const u8 value) { memory[address] = value; }

static bool SameState(M6502& a, M6502& b)
{
    u16 pcA, pcB;
    u8 spA, accA, xA, yA, statusA, spB, accB, xB, yB, statusB;
    a.GetRegs(pcA, spA, accA, xA, yA, statusA);
    b.GetRegs(pcB, spB, accB, xB, yB, statusB);
    return pcA == pcB && spA == spB && accA == accB &&
        xA == xB && yA == yB && statusA == statusB && a.SYNC() == b.SYNC();
}

int main()
{
    memset(memory, 0xea, sizeof(memory)); // continuous NOPs make a CPU restart visible in PC
    memory[0x0200] = 0xa2; memory[0x0201] = 0x10; // LDX #$10
    memory[0x0202] = 0xa0; memory[0x0203] = 0x20; // LDY #$20
    memory[0x0204] = 0xa9; memory[0x0205] = 0x30; // LDA #$30
    memory[0xfffc] = 0x00;
    memory[0xfffd] = 0x02;
    M6502 uninterrupted, resumed, resetByOldSetter;
    uninterrupted.SetPC(0);
    resumed.SetPC(0);
    resetByOldSetter.SetPC(0);
    uninterrupted.SetBusFunctions(ReadOriginal, WriteOriginal);
    resumed.SetBusFunctions(ReadOriginal, WriteOriginal);
    resetByOldSetter.SetBusFunctions(ReadOriginal, WriteOriginal);
    for (unsigned i = 0; i < 31; ++i) {
        uninterrupted.Step();
        resumed.Step();
        resetByOldSetter.Step();
    }
    if (!SameState(uninterrupted, resumed) || !SameState(uninterrupted, resetByOldSetter) || resumed.SYNC()) return 1;
    resumed.SetBusFunctionsPreservingState(ReadAlternate, WriteAlternate);
    resetByOldSetter.SetBusFunctions(ReadAlternate, WriteAlternate);
    if (!SameState(uninterrupted, resumed)) return 2;
    for (unsigned i = 0; i < 13; ++i) {
        uninterrupted.Step();
        resumed.Step();
        resetByOldSetter.Step();
        if (!SameState(uninterrupted, resumed)) return 3;
    }
    if (!alternateReads || SameState(uninterrupted, resetByOldSetter)) return 4;
    puts("PASS 1551 CPU resumes at the same cycle after bus callback rebinding");
    return 0;
}
