// Pi1551 - Clean-room 1551 system wrapper (based on 1541 wrapper structure)

#ifndef PI1551_CLEAN_H
#define PI1551_CLEAN_H

#include "m6502.h"
#include "m6523.h"
#include "tcbm_bus.h"

#if defined(USE_DRIVE1551_CLEAN)
#include "Drive1551-clean.h"
typedef Drive1551Clean Drive1551Impl;
#else
#include "Drive1551.h"
typedef Drive Drive1551Impl;
#endif

class Pi1551
{
public:
    Pi1551();

    void Initialise();

    void Update();

    void Reset();

    Drive1551Impl drive;
    m6523 TPI;

    M6502 m6502;

    inline void SetDeviceID(u8 id)
    {
        // Port C bit 5 is DEVNUM input: 0 -> #8, 1 -> #9 (per docs)
        TPI.GetPortC()->SetInput(0x20, (id & 1) != 0);
    }
};

#endif


