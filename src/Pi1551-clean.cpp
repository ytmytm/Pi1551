// Pi1551 - Clean-room 1551 system wrapper (modeled after 1541 wrapper)

#include "Pi1551-clean.h"
#include "debug.h"
#include "options.h"
#include "ROMs.h"

extern Options options;
extern Pi1551 pi1551;
extern u8 s_u8Memory[0xc000];
extern ROMs roms;

// 6502 bus functions for 1551

u8 read6502_1551(u16 address)
{
    if (address & 0x8000)
    {
        switch (address & 0xe000)
        {
            case 0x8000:
                if (options.GetRAMBOard()) {
                    return s_u8Memory[address];
                }
            case 0xa000:
            case 0xc000:
            case 0xe000:
                return roms.Read(address);
        }
    }
    else if (address == 0) {
        return pi1551.TPI.GetPortCPU()->GetDirection();
    }
    else if (address == 1) {
        return pi1551.TPI.ReadCPUPort();
    }
    else if (address >= 0x4000 && address < 0x8000) {
        return pi1551.TPI.Read(address);
    }
    return s_u8Memory[address & 0x7ff];
}

u8 read6502ExtraRAM_1551(u16 address)
{
    if (address & 0x8000)
    {
        return roms.Read(address);
    }
    else if (address == 0) {
        return pi1551.TPI.GetPortCPU()->GetDirection();
    }
    else if (address == 1) {
        return pi1551.TPI.ReadCPUPort();
    }
    else if (address >= 0x4000 && address < 0x4008) {
        return pi1551.TPI.Read(address);
    }
    return s_u8Memory[address & 0x7fff];
}

u8 peek6502_1551(u16 address)
{
    if (address & 0x8000)
    {
        return roms.Read(address);
    }
    else if (address == 0) {
        return pi1551.TPI.GetPortCPU()->GetDirection();
    }
    else if (address == 1) {
        return pi1551.TPI.PeekCPUPort();
    }
    else if (address >= 0x4000 && address < 0x8000) {
        return pi1551.TPI.Peek(address);
    }
    return s_u8Memory[address & 0x7ff];
}

void write6502_1551(u16 address, const u8 value)
{
    if (address & 0x8000)
    {
        switch (address & 0xe000)
        {
            case 0x8000:
                if (options.GetRAMBOard()) {
                    s_u8Memory[address] = value;
                    return;
                }
            case 0xa000:
            case 0xc000:
            case 0xe000:
                return;
        }
    }
    else if (address == 0) {
        pi1551.TPI.GetPortCPU()->SetDirection(value);
    }
    else if (address == 1) {
        pi1551.TPI.WriteCPUPort(value);
    }
    else if (address >= 0x4000 && address < 0x8000) {
        pi1551.TPI.Write(address, value);
    }
    else {
        s_u8Memory[address & 0x7ff] = value;
    }
}

void write6502ExtraRAM_1551(u16 address, const u8 value)
{
    if (address & 0x8000) return;
    if (address == 0) {
        pi1551.TPI.GetPortCPU()->SetDirection(value);
    }
    else if (address == 1) {
        pi1551.TPI.WriteCPUPort(value);
    }
    else if (address >= 0x4000 && address < 0x4008) {
        pi1551.TPI.Write(address, value);
    }
    else {
        s_u8Memory[address & 0x7fff] = value;
    }
}

Pi1551::Pi1551()
{
    TPI.ConnectIRQ(&m6502.IRQ);
}

void Pi1551::Initialise()
{
    TPI.ConnectIRQ(&m6502.IRQ);
}

void Pi1551::Update()
{
    if (drive.Update())
    {
        // 1551: keep SO internal; do not modify 6502 V flag
        // m6502.SO();
    }

    // 1551: TPI embeds IRQ timer
    TPI.Execute();
}

void Pi1551::Reset()
{
    TPI.Reset();
    drive.Reset();
    TCBM_Bus::Reset();
    m6502.Reset();
}


