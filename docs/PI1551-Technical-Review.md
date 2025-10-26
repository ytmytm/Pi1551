### PI1551 technical review vs known documentation and emulator practice

This document reviews the current PI1551 (Commodore 1551) implementation against publicly available documentation and common emulator practice (e.g., VICE). It focuses on memory map, device registers, TCBM signaling, timing, and DOS/command behaviors.

Sources used

- Public 1551/TCBM notes and schematics (community docs; e.g., zimmers/plus/4 resources for C1551; general 6523 TPI information)
- Emulator practice (VICE 1551 support as reference model)

Summary assessment

- CPU and timing
  - 6502 runs effectively at 2 MHz: emulator loop steps two cycles per 1 µs slice and syncs to 1 MHz system timer. This matches the known faster bus of 1551 vs 1541.
  - NE555-derived ~100 Hz “IRQ” timer is modeled in `m6523::Execute()`; this provides periodic IRQ toggling consistent with documentation that a free-running timing source exists in the drive logic.

- Memory map and bus functions
  - Address decoding implements:
    - ROM window at 0x8000–0xFFFF (16 KiB total)
    - TPI/IO at 0x4000–0x7FFF (mirrored as needed)
    - CPU port at $00 (DDRC) and $01 (PORTC) for the host-facing byte port
    - RAM at $0002–$07FF (2 KiB) in normal mode, extended mapping in “extra RAM” mode
  - These regions align with known 1551 layouts: dedicated ROM, TPI register block, CPU port at $00/$01, internal RAM ~2 KiB.

- 6523 TPI (TIA) register model
  - Ports A/B/C data and DDR at offsets 0–5 are implemented; read paths combine input/output via DDR, write paths update port latches; matches typical MOS peripheral semantics.
  - CPU port ($00/$01) is distinct from A/B/C and accessible in the system map, as required by 1551.

- TCBM signaling and mapping
  - Port A of TPI carries the 8-bit parallel data (bidir); GPIO directions switch per DDR.
  - Port C bits mapping:
    - PC3 (0x08) ACK → output to `ACK` line
    - PC2 (0x04) DEV → output to `DEV` select line
    - PC1 (0x02) STATUS1 → output
    - PC0 (0x01) STATUS0 → output
    - PC6 (0x40) SYNC → active-low; asserted only during read; deasserted during write (UC2 gated by R/!W). Implemented.
    - PC4 (0x10) MODE → 0=write, 1=read; drive code reads Port C output latch for this (not input). Implemented.
    - PC7 (bit 7) is fed from external `DAV` into input (via `SetInput`) for TCBM handshake.
  - These assignments agree with available descriptions of C1551 TCBM: Port C low bits carry status encoding, `DEV` selects mapping, `ACK` provides handshake back to the host, and `DAV` is sampled as input.

- Byte-ready (CPU port)
  - CPU port bit 7 indicates “byte latched” (1=yes). The drive sets it when a byte is latched on Port B at the byte boundary. It is cleared by hardware on any TPI port access; implemented in `m6523::Read()` by clearing CPU port bit 7 after a read of any TPI port.

- Device number selection
  - `Pi1551::SetDeviceID` drives PC5 (DEVNUM) using inverse of LSB: 0 → drive 8, 1 → drive 9. That matches common jumper semantics for 8/9 select.

- Browse/command layer
  - `tcbm_commands` provides SD2IEC-like commands for file browsing (CD, MKDIR, SCRATCH, RENAME, memory pseudo-ops, device switching). These are host-side conveniences for browsing/mounting prior to full emulation and are not intended to mirror 1551 DOS command channel precisely.
  - Directory listing format and error channel messages emulate CBM-style responses, sufficient for browser integration.

Areas confirmed correct

- Active-low `RESET` sense, with internal pull-up; wait loop treats low as asserted.
- Handshake timing structure: poll `DAV`, assert/release `ACK`, write status flags; data direction swapped by Port A DDR.
- Two-step 2 MHz emulation cadence per 1 µs and batched GPIO writes via GPCLR/GPSET.
- SYNC/Byte-ready/MODE: SYNC asserted only when reading and 10x1s detected; byte-ready set on latch and cleared by any TPI access; MODE read from Port C output latch (0=write, 1=read).

Open questions / recommended verifications

- ROM accessor for 1551 path
  - 1551 bus read uses `roms.Read(address)`; recommend switching to `ROMs::Read1551()` to avoid accidental coupling with 1541 slots and to reflect 1551’s distinct ROM image unambiguously.

- TPI register mirroring and side-effects
  - Confirm exact mirroring range and any undocumented read-modify-write quirks for 6523 reads/writes used by the ROM. SYNC/byte-ready/MODE semantics are now implemented as per docs.

- IRQ cadence
  - The 100 Hz IRQ model is plausible; verify against 1551 ROM expectations (e.g., timer-driven routines) to ensure rate and phase do not disrupt command loops.

- Extra RAM mode
  - Validate that “extra RAM” layouts (masking to 32 KiB RAM at $0000–$7FFF excluding I/O/ROM) match any known 1551 memory expansions (if meant as a developer mode).

- Browse command set vs 1551 DOS
  - Since `tcbm_commands` is a convenience layer, this is acceptable; just document that these commands are for PI1551’s browser and are not part of the original 1551 DOS command protocol.

Hardware pin plan cross-check

- Data bus DIO1..DIO8 (GPIO14,15,18,23,24,25,8,7) and handshake/status lines (DAV=GPIO12, ACK=GPIO20, STATUS0=GPIO16, STATUS1=GPIO21, DEV=GPIO9, RESET=GPIO11) match the documented mapping in `docs/rpi-pinmapping.md`. LED moved to GPIO10 to avoid conflict with STATUS0.

Action items

- Switch 1551 ROM reads to dedicated accessor (Read1551) and keep current default filename via options.
- Note in docs that browse commands are SD2IEC-like convenience and out of scope of original 1551 DOS protocol.

Conclusion

The current PI1551 implementation aligns well with documented 1551/TCBM behavior in core areas: memory map, TPI port usage, handshake lines, and timing cadence. Recent correctness tweaks include sourcing MODE from Port C output, asserting SYNC only during read, and clearing byte-ready on any TPI access; DEV bit mapping (0x04) is correct. The largest conceptual difference is the added browse/SD2IEC layer (intended), which is acceptable for user experience. With minor clarifications (ROM accessor, comments) the implementation appears sound.


