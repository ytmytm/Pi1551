// Pi1551 - A Commodore 1551 disk drive emulator (clean-room Drive)
// Copyright(C) 2025
//
// This file provides a clean-room implementation of the 1551 drive head/encoder
// loop and IO mapping using the MOS 6523 TPI and CPU port semantics.

#ifndef DRIVE1551_CLEAN_H
#define DRIVE1551_CLEAN_H

#include "m6523.h"
#include "DiskImage.h"
#include <stdlib.h>

class Drive1551Clean
{
public:
    Drive1551Clean();

    // Hook up the TPI (6523 Tri-Port Interface)
    inline void SetTPI(m6523* tpi)
    {
        m_pTPI = tpi;
        // CPU port drives head/motor/density/LED; register callback on writes
        tpi->GetPortCPU()->SetPortOut(this, OnCPUPortOut);
    }

    // Called once per CPU cycle group; returns true if a new byte was presented
    bool Update();

    void Insert(DiskImage* diskImage);
    inline const DiskImage* GetDiskImage() const { return diskImage; }
    void Eject();
    void Reset();

    inline unsigned Track() const { return headTrackPos; }
    inline unsigned SectorPos() const { return headBitOffset >> 3; }
    inline unsigned GetHeadBitOffset() const { return headBitOffset; }
    inline bool IsMotorOn() const { return motor; }
    inline bool IsLEDOn() const { return LED; }
    inline unsigned char GetLastHeadDirection() const { return lastHeadDirection; }

    // CPU port write handler (P0..P7)
    static void OnCPUPortOut(void* pThis, unsigned char status);

private:
    inline void UpdateHeadSectorPosition()
    {
        // Disk spins at 300 rpm = 5 rps; 16 MHz internal reference
        static const float CYCLES_16Mhz_PER_ROTATION = 3200000.0f;
        if (diskImage)
        {
            bitsInTrack = diskImage->BitsInTrack(headTrackPos);
            headBitOffset %= bitsInTrack;
            cyclesPerBit = CYCLES_16Mhz_PER_ROTATION / (float)bitsInTrack;
        }
    }

    inline void MoveHead(unsigned char headDirection)
    {
        if (lastHeadDirection != headDirection)
        {
            if (((lastHeadDirection - 1) & 3) == headDirection)
            {
                if (headTrackPos > 0) headTrackPos--; // inward
            }
            else if (((lastHeadDirection + 1) & 3) == headDirection)
            {
                if (headTrackPos < HALF_TRACK_COUNT - 1) headTrackPos++; // outward
            }
            lastHeadDirection = headDirection;
            UpdateHeadSectorPosition();
        }
    }

    inline unsigned AdvanceAndGetBitIndex(int& byteOffset)
    {
        ++headBitOffset %= bitsInTrack;
        byteOffset = headBitOffset >> 3;
        return (~headBitOffset) & 7;
    }

    inline bool GetNextBit()
    {
        int byteOffset;
        unsigned bit = AdvanceAndGetBitIndex(byteOffset);
        if (byteOffset != cachedByteOffset || cachedTrackPos != headTrackPos)
        {
            cachedByte = diskImage->GetNextByte(headTrackPos, byteOffset);
            cachedByteOffset = byteOffset;
            cachedTrackPos = headTrackPos;
        }
        return ((cachedByte >> bit) & 1) != 0;
    }

    inline void SetNextBit(bool value)
    {
        int byteOffset;
        unsigned bit = AdvanceAndGetBitIndex(byteOffset);
        diskImage->SetBit(headTrackPos, byteOffset, bit, value);
    }

    // Encoder/decoder timing helpers
    inline void ResetEncoderDecoder(float minUs, float maxUs)
    {
        UE7Counter = CLOCK_SEL_AB; // preload from density (A/B) per cell
        UF4Counter = 0;
        // schedule random (noise) flux reversal time in microseconds
        randomFluxReversalTime = ((maxUs - minUs) * ((float)rand() / RAND_MAX)) + minUs;
    }

private:
    // Media
    DiskImage*      diskImage;

    // 6523 interface
    m6523*          m_pTPI;

    // Shift registers and counters
    unsigned        UE7Counter;          // encoder/decoder clock preload/counter (0..15)
    int             UF4Counter;          // 4-bit counter (0..15)
    int             UE3Counter;          // byte bit counter (0..8)
    unsigned        CLOCK_SEL_AB;        // density select (CPU P5..P6)

    unsigned        headTrackPos;        // half-tracks
    unsigned        headBitOffset;       // bit offset into track
    unsigned        bitsInTrack;         // total bits
    float           cyclesPerBit;        // 16MHz ticks per bit
    float           cyclesForBit;        // accumulator

    unsigned        readShiftRegister;   // 10-bit window for SYNC recognition (low 10 bits)
    unsigned char   writeShiftRegister;  // latched output byte

    // Caching for head reads
    unsigned        cachedTrackPos;
    int             cachedByteOffset;
    unsigned char   cachedByte;

    // Controls and status
    bool            motor;
    bool            LED;                 // CPU P3 (0=on, 1=off)
    unsigned char   lastHeadDirection;   // CPU P0..P1 quadrature

    // Byte-ready and sync timing/noise model
    float           randomFluxReversalTime; // microseconds

    // Disk swap emulation on write-protect
    unsigned        newDiskImageQueuedCylesRemaining;

    // Constants for swap timing
    static const unsigned DISK_SWAP_CYCLES_DISK_EJECTING = 400000;
    static const unsigned DISK_SWAP_CYCLES_NO_DISK       = 200000;
    static const unsigned DISK_SWAP_CYCLES_DISK_INSERTING= 400000;
};

#endif


