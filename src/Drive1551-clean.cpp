// Pi1551 - Clean-room 1551 drive emulation core

#include "Drive1551-clean.h"
#include "debug.h"
#include <stdlib.h>

// Notes (from docs/ioports-1541-vs-1551.txt and 1551 ROM):
// - CPU port ($00/$01) bits:
//   P0..P1: stepper sequence; P2: motor; P3: LED (0=on); P4: write protect; P5..P6: density; P7: byte-ready (1=yes)
// - TPI Port B carries head data (parallel) to/from UD2/UD3 equivalent shifter interface.
// - TPI Port C bit6 is SYNC (active-low = 0 when last 10 bits are ones)
// - TPI Port C bit4 MODE: 0=write, 1=read
// - Byte-ready (CPU P7) is set when a full byte is latched to Port B; it is cleared by CPU port access in hardware.

Drive1551Clean::Drive1551Clean()
    : diskImage(0)
    , m_pTPI(0)
    , UE7Counter(0)
    , UF4Counter(0)
    , UE3Counter(0)
    , CLOCK_SEL_AB(3)
    , headTrackPos(18 * 2)
    , headBitOffset(0)
    , bitsInTrack(0)
    , cyclesPerBit(0)
    , cyclesForBit(0)
    , readShiftRegister(0)
    , writeShiftRegister(0)
    , cachedTrackPos((unsigned)-1)
    , cachedByteOffset(-1)
    , cachedByte(0)
    , motor(false)
    , LED(false)
    , lastHeadDirection(0)
    , randomFluxReversalTime(0)
    , newDiskImageQueuedCylesRemaining(0)
{
    srand(0x811c9dc5U);
    Reset();
}

void Drive1551Clean::Reset()
{
    headTrackPos = 18 * 2;        // Start around track 19
    CLOCK_SEL_AB = 3;             // Speed zone 3
    UpdateHeadSectorPosition();
    lastHeadDirection = 0;
    motor = false;
    LED = false;
    readShiftRegister = 0;
    writeShiftRegister = 0;
    UE3Counter = 0;
    ResetEncoderDecoder(18.0f, 22.0f);
    cyclesForBit = 0;
    newDiskImageQueuedCylesRemaining = DISK_SWAP_CYCLES_DISK_EJECTING + DISK_SWAP_CYCLES_NO_DISK + DISK_SWAP_CYCLES_DISK_INSERTING;
    // Ensure initial CPU P7 (byte-ready) is low
    if (m_pTPI) m_pTPI->GetPortCPU()->SetInput(0x80, false);
    // SYNC not asserted initially (active-low), so set PC6 high
    if (m_pTPI) m_pTPI->GetPortC()->SetInput(0x40, true);
}

void Drive1551Clean::Insert(DiskImage* di)
{
    Eject();
    diskImage = di;
    newDiskImageQueuedCylesRemaining = DISK_SWAP_CYCLES_DISK_EJECTING + DISK_SWAP_CYCLES_NO_DISK + DISK_SWAP_CYCLES_DISK_INSERTING;
}

void Drive1551Clean::Eject()
{
    if (diskImage) diskImage = 0;
}

void Drive1551Clean::OnCPUPortOut(void* pThis, unsigned char status)
{
    Drive1551Clean* p = (Drive1551Clean*)pThis;
    if (p->motor)
        p->MoveHead(status & 3);
    p->motor = (status & 4) != 0;                 // P2
    p->CLOCK_SEL_AB = ((status >> 5) & 3);        // P5..P6
    p->LED = (status & 8) == 0;                   // P3: 0=on, 1=off
    // Note: P4 (write protect) is input to CPU port; we drive it elsewhere.
}

bool Drive1551Clean::Update()
{
    // Emulate write-protect timing during disk swap on CPU P4 bit.
    if (newDiskImageQueuedCylesRemaining > 0)
    {
        newDiskImageQueuedCylesRemaining--;
        if (!m_pTPI) return false;
        if (newDiskImageQueuedCylesRemaining == 0)
            m_pTPI->GetPortCPU()->SetInput(0x10, diskImage ? !diskImage->GetReadOnly() : false);
        else if (newDiskImageQueuedCylesRemaining > DISK_SWAP_CYCLES_NO_DISK + DISK_SWAP_CYCLES_DISK_INSERTING)
            m_pTPI->GetPortCPU()->SetInput(0x10, false); // ejecting -> write protected (0)
        else if (newDiskImageQueuedCylesRemaining > DISK_SWAP_CYCLES_DISK_INSERTING)
            m_pTPI->GetPortCPU()->SetInput(0x10, true);  // no disk -> not protected (1)
        else
            m_pTPI->GetPortCPU()->SetInput(0x10, false); // inserting -> write protected (0)
        return false;
    }

    if (!(diskImage && motor && m_pTPI))
        return false;

    // MODE bit from TPI Port C bit4: 0=write, 1=read
    bool writing = !(m_pTPI->GetPortC()->GetInput() & 0x10);

    // Simulate 16 internal clocks per CPU cycle group (16 MHz / 1 MHz CPU base)
    for (int i = 0; i < 16; ++i)
    {
        if (!writing)
        {
            // schedule next bit time and possible genuine flux reversal
            if (++cyclesForBit >= cyclesPerBit)
            {
                cyclesForBit -= cyclesPerBit;
                if (GetNextBit())
                {
                    // genuine flux reversal resets timing window to density
                    ResetEncoderDecoder(18.0f, 20.0f);
                }
            }
            // random noise-induced flux reversal window
            randomFluxReversalTime -= 0.0625f; // 1/16 us
            if (randomFluxReversalTime <= 0)
            {
                ResetEncoderDecoder(2.0f, 25.0f);
            }
        }

        if (++UE7Counter == 0x10)
        {
            UE7Counter = CLOCK_SEL_AB;      // preload per density zone

            ++UF4Counter &= 0xf;            // 4-bit counter

            if ((UF4Counter & 0x3) == 2)
            {
                // Shift read data: NOR(C,D) -> 1 only when UF4 count == 2
                readShiftRegister <<= 1;
                readShiftRegister |= (UF4Counter == 2) ? 1u : 0u;

                if (writing)
                {
                    SetNextBit((writeShiftRegister & 0x80) != 0);
                }
                writeShiftRegister <<= 1;

                // SYNC detection (active-low on PC6) only during reading
                if (!writing)
                {
                    bool inSync = ((readShiftRegister & 0x3ff) == 0x3ff);
                    m_pTPI->GetPortC()->SetInput(0x40, !inSync);
                    if (inSync)
                    {
                        UE3Counter = 0;
                    }
                    else
                    {
                        UE3Counter++;
                    }
                }
                else
                {
                    // While writing, UC2 input prevents SYNC; keep PC6 deasserted (high)
                    m_pTPI->GetPortC()->SetInput(0x40, true);
                    UE3Counter++;
                }
            }
            else if (((UF4Counter & 2) == 0) && (UE3Counter == 8))
            {
                // Byte boundary reached when bit clock is low and 8 bits collected
                UE3Counter = 0;

                // Present byte on TPI Port B and assert byte-ready on CPU P7
                unsigned char byteValue = (unsigned char)(readShiftRegister & 0xff);
                if (writing)
                {
                    writeShiftRegister = m_pTPI->GetPortB()->GetOutput();
                }
                else
                {
                    m_pTPI->GetPortB()->SetInput(byteValue);
                }
                m_pTPI->GetPortCPU()->SetInput(0x80, true); // P7=1: new byte latched
            }
        }
    }

    return true;
}


