// Pi1551 ROM-idle sleep predicates shared by the firmware and native tests.
#ifndef PI1551_IDLE_SLEEP_H
#define PI1551_IDLE_SLEEP_H

#include "types.h"

namespace Pi1551IdleSleep
{
	static constexpr u16 ROM_TPI_WAIT_PC = 0xEAC4;
	static constexpr u16 JOB_CODE_FIRST = 0x0002;
	static constexpr u16 JOB_CODE_LAST = 0x0006;
	static constexpr u16 CHANNEL_FIRST = 0x022B;
	// The ROM's LEADC housekeeping loop loads X with $0e and scans exactly
	// $022b-$0239.  The following bytes are initialized alongside the table,
	// but are reused for other state ($023a/$023b are commonly $84/$05 while
	// an otherwise idle stock drive waits at $eac4).
	static constexpr u16 CHANNEL_LAST = 0x0239;
	static constexpr u16 COMMAND_PENDING = 0x0255;
	// Primary TALK/LISTEN state.  These remain non-zero for the whole transfer,
	// including command/status channel 15 which LEADC deliberately does not scan.
	static constexpr u16 TALK_ACTIVE = 0x005B;
	static constexpr u16 LISTEN_ACTIVE = 0x005C;

	inline bool HostBusChanged(u8 previousData, bool previousDav,
		u8 currentData, bool currentDav)
	{
		return currentData != previousData || currentDav != previousDav;
	}

	inline bool HostTransferIsIdle(const u8* memory)
	{
		return memory[TALK_ACTIVE] == 0 && memory[LISTEN_ACTIVE] == 0;
	}

	inline bool JobsAreIdle(const u8* memory)
	{
		for (u16 address = JOB_CODE_FIRST; address <= JOB_CODE_LAST; ++address)
		{
			// Bit 7 requests work from the drive mechanism. Values with bit 7
			// clear are idle/completion status codes and need no 100Hz IRQ work.
			if (memory[address] & 0x80)
				return false;
		}
		return memory[COMMAND_PENDING] == 0;
	}

	inline bool ChannelsAreClosed(const u8* memory)
	{
		for (u16 address = CHANNEL_FIRST; address <= CHANNEL_LAST; ++address)
		{
			if (memory[address] != 0xFF)
				return false;
		}
		return true;
	}

	inline bool CanSleep(bool enabled, bool d64, u16 pc, bool motorOn,
		bool ledOn, bool diskChangeInProgress, bool dataBusIsOutput,
		bool irqAsserted, const u8* memory)
	{
		return enabled && d64 && pc == ROM_TPI_WAIT_PC
			&& !motorOn && !ledOn && !diskChangeInProgress
			&& !dataBusIsOutput && !irqAsserted
			&& JobsAreIdle(memory) && ChannelsAreClosed(memory)
			&& HostTransferIsIdle(memory);
	}

	inline bool HostNeedsWake(u8 sleepingData, bool sleepingDav,
		u8 currentData, bool currentDav, bool resetAsserted,
		bool uiCommandPending)
	{
		return resetAsserted || uiCommandPending
			|| HostBusChanged(sleepingData, sleepingDav, currentData, currentDav);
	}

	inline bool HostCommandPending(u8 data)
	{
		// The stock ROM uses BMI after LDA $4000. TCBM request codes are
		// $81-$84, so a set bit 7 must be handled before entering sleep.
		return (data & 0x80) != 0;
	}
}

#endif
