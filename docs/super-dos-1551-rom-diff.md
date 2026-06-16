# Super DOS 1551 ROM diff notes

Compared files:

- Stock: `sdcard/dos1551.bin`
- Super DOS: `sdcard/super_dos_1551.rom`
- Stock disassembly reference: `docs/1551-rom-disassembly.asm`

Both ROM images are 16 KiB and map at `$C000-$FFFF`. `cmp -l` reports 261
changed bytes. The changes are small call-site/data patches plus two injected
code/data areas in unused `$FF` space.

## Byte-level clusters

Address ranges below are ROM CPU addresses, not file offsets.

| Address | Stock bytes | Super DOS bytes | Purpose |
| --- | --- | --- | --- |
| `$C8B7-$C8B8` | `C6 F0` | `16 00` | command parser uses RAM command-table start pointer `$16` instead of fixed `$F0C6` |
| `$CE15-$CE16` | `1A F1` | `17 00` | max track check uses RAM `$17` instead of fixed `$F11A` |
| `$D157-$D158` | `08 D8` | `1C FE` | BAM read setup calls Super-format probe at `$FE1C` instead of stock `$D808` |
| `$D19B-$D19C` | `C0 90` | `C4 16` | BAM free-block loop end uses RAM `$16` instead of fixed `$90` |
| `$D671-$D672` | `1A F1` | `17 00` | max track check uses RAM `$17` |
| `$D68C-$D68D` | `18 F1` | `FD 02` | disk format marker compare uses `$02FD` instead of fixed `$F118` |
| `$D692-$D693` | `B6 F0` | `E1 FD` | sector-count routine redirected from `$F0B6` to `$FDE1` |
| `$D6B5-$D6B6` | `1A F1` | `17 00` | max track check uses RAM `$17` |
| `$D6BA-$D6BB` | `B6 F0` | `E1 FD` | sector-count routine redirected from `$F0B6` to `$FDE1` |
| `$E9DC-$E9F5` | checksum loop | `EA...EA` | disables a ROM checksum/test block |
| `$ECA4-$ECA5` | `59 D4` | `0E FE` | format-with-ID close-channels call redirected to injected parser at `$FE0E` |
| `$ECBB-$ECBC` | `18 F1` | `FD 02` | format marker compare uses `$02FD` |
| `$ECCB-$ECCC` | `C6 F0` | `16 00` | BAM disk-name pointer uses RAM `$16` |
| `$ECDC-$ECDD` | `18 F1` | `FD 02` | in-memory format marker set from `$02FD` |
| `$ECF4-$ECF5` | `18 F1` | `FD 02` | BAM format marker written from `$02FD` |
| `$ED3E-$ED3F` | `B6 F0` | `E1 FD` | BAM creation sector-count routine redirected |
| `$ED58-$ED59` | `C0 90` | `C4 16` | BAM creation loop end uses RAM `$16` |
| `$ED9B-$ED9C` | `1A F1` | `17 00` | BAM verification max track check uses RAM `$17` |
| `$EFB3-$EFB4` | `1A F1` | `17 00` | free-block scan max track check uses RAM `$17` |
| `$EFE8-$EFE9` | `B6 F0` | `E1 FD` | sector-count routine redirected |
| `$F041-$F042` | `1A F1` | `17 00` | free-block total max track check uses RAM `$17` |
| `$F070-$F071` | `B6 F0` | `E1 FD` | sector-count routine redirected |
| `$F23E-$F24C` | fixed density/sector lookup | call `$FDE1`, then NOPs | low-level read/write density setup uses injected sector-count routine |
| `$FCAE-$FCC9` | normal formatter write loop | modified gap loop | formatter can shorten the post-data `$55` gap to one byte |
| `$FD54-$FD55` | `C9 24` | `C5 17` | format/verify end-track compare uses RAM `$17` |
| `$FDE1-$FE59` | mostly `$FF` filler | injected Super DOS routines | sector-count, mode setup, and format extension parser |
| `$FF00-$FF1F` | `$FF` filler | ASCII signature | `SUPER DOS 1551 BY CSORY IN 1992.` |

## Injected routines

### `$FDE1`: sector count per track

Stock code uses `$F0B6`:

```asm
F0B6  LDX $F119       ; 4 zones
F0B9  CMP $F119,X     ; compare track against 36,31,25,18
      DEX
      BCS $F0B9
F0BF  LDA $F114,X     ; sectors: 17,18,19,21
      RTS
```

Super DOS redirects many call sites to `$FDE1`:

```asm
FDE1  LDX #$03
FDE3  BIT $5E
FDE5  BMI $FDEF
FDE7  CMP $F11A,X
FDEA  BCC $FDEF
FDEC  DEX
FDED  BNE $FDE7
FDEF  LDA $F114,X
FDF2  RTS
```

If `$5E` has bit 7 set, the routine immediately returns `$F117`, which is 21
sectors. Otherwise it behaves like the stock zone table for tracks 1-35, using
the same sector counts:

- tracks 1-17: 21 sectors
- tracks 18-24: 19 sectors
- tracks 25-30: 18 sectors
- tracks 31-35: 17 sectors

So Super DOS has a mode where all tracks, including track 18, are treated as
21-sector tracks.

### `$FDF3` / `$FDFB`: mode setup

```asm
FDF3  CLC
FDF4  LDX #$90
FDF6  LDY #$24
FDF8  LDA #$41       ; 'A'
FDFA  BIT $38
FDFC  ROR $5E
FDFE  BPL $FE06
FE00  LDX #$A8
FE02  LDY #$2A
FE04  LDA #$53       ; 'S'
FE06  STA $02FD
FE09  STX $16
FE0B  STY $17
FE0D  RTS
```

This sets the variable constants used by the patched stock code:

- `$02FD`: disk format marker, normally `'A'`, optionally `'S'`
- `$16`: BAM/disk-name table limit, normally `$90`, optionally `$A8`
- `$17`: last track + 1, normally `$24` (36), optionally `$2A` (42)

There are two intentional entry points:

- `$FDF3` starts with `CLC`, so `ROR $5E` clears bit 7 and selects stock-style
  `'A'`, `$90`, `$24` operation.
- `$FDFB` enters at the second byte of `BIT $38`, which is opcode `SEC`; then
  `ROR $5E` sets bit 7 and selects Super-style `'S'`, `$A8`, `$2A` operation.

This overlapping-instruction trick explains how the same byte sequence selects
either 35-track stock mode or 42-track Super mode.

### `$FE0E`: format-command extension parser

This replaces the stock `JSR $D459` close-all-channels call in the format-with-ID
path:

```asm
FE0E  LDA $0202,Y    ; third char after the disk ID position
FE11  PHA
FE12  JSR $D459      ; original close-all-channels call
FE15  PLA
FE16  CMP #$2B       ; '+'
FE18  BEQ $FDFB
FE1A  BNE $FDF3
FE1C  PHA
FE1D  JSR $FDFB
FE20  PLA
FE21  PHA
FE22  TAY
FE23  ASL A
FE24  TAX
FE25  LDA #$12       ; track 18
FE27  STA $08,X
FE29  LDA #$14       ; sector 20
FE2B  STA $09,X
FE2D  LDA #$B0       ; read-block-header job
FE2F  STA $0002,Y
FE32  LDA $0002,Y
FE35  BMI $FE32
FE37  CMP #$02
FE39  BCS $FE53
FE3B  LDA #$80       ; read job
FE3D  STA $0002,Y
FE40  LDA $0002,Y
FE43  BMI $FE40
FE45  CMP #$02
FE47  BCS $FE53
FE49  DEC $09,X
FE4B  LDA $09,X
FE4D  CMP #$13
FE4F  BEQ $FE3B
FE51  BNE $FE56
FE53  JSR $FDF3
FE56  PLA
FE57  JMP $D808      ; continue stock disk-controller setup
```

The parser recognizes `+` after the disk ID and jumps to the `$FDFB` Super-mode
entry. A zero byte takes the longer `$FE1C` path, which first selects Super mode,
then probes track 18 sectors `$14` down to `$13` through the job queue. If the
probe fails, `$FE53` calls `$FDF3` to fall back to stock mode. This same `$FE1C`
probe is also called from the BAM-read path at `$D157`, so Super DOS can detect
an already Super-formatted disk while loading the BAM.

### Formatter write-loop patch at `$FCAE`

Stock writes the post-data gap using `LDY $4E`; Super DOS changes it to:

```asm
FCB6  LDA #$55
FCB8  LDY #$01
FCBA  BIT $5E
FCBC  BMI $FCC0
FCBE  LDY $4E
FCC0  BIT $01
FCC2  BPL $FCC0
FCC4  STA $4001
FCC7  DEY
FCC8  BNE $FCC0
```

If `$5E` is negative, the formatter writes only one `$55` gap byte after each
sector's data block. In normal mode it keeps the stock variable gap length in
`$4E`.

This is the strongest ROM-level match for the G64 formatting suspicion:
Super-format mode can both ask for 21 sectors on track 18 and compress sector
gaps enough to fit that layout at the higher density.

## Emulator-facing notes

Current emulation tables are stock-zone oriented:

- `src/gcr.cpp` has `sector_map_1541`: track 18 is 19 sectors.
- `src/gcr.cpp` has `speed_map_1541`: track 18 is speed zone 2.
- `src/DiskImage.h::GetSpeedZoneIndexD64()` maps zero-based track 17 (physical
  track 18) to zone 2.
- `src/DiskImage.cpp::WriteG64()` writes one speed entry per halftrack from
  `trackDensity[track] & 3`.

For normal DOS, track 18 at 19 sectors / zone 2 is correct. Super DOS appears to
have a mode where track 18 is formatted like an outer track: 21 sectors, likely
speed zone 3, with a one-byte data gap. A fixed stock speed/sector model will
not represent that track correctly in G64.

Implementation note: Pi1551 now treats this as G64-only behavior. During raw
G64 writes, the drive asks `DiskImage` to prepare the current track for the
live CPU density bits. If a Super DOS format switches track 18 from stock zone 2
to zone 3, the G64 track metadata and length are updated before bits are written.
D64 images instead get marked as D64-incompatible and are not saved over as D64.
Short G64 files are initialized with blank metadata for all 84 halftracks, so a
35-track G64 can grow when Super DOS formats tracks beyond the original range
and will be saved back with the extended G64 track table.

## Working hypothesis

The track-18 G64 format failure is consistent with Super DOS trying to write a
non-stock track 18:

1. Super DOS redirects all DOS-level and low-level sector-count decisions through
   `$FDE1`.
2. When `$5E` bit 7 is set, `$FDE1` returns 21 sectors for every track, including
   track 18.
3. The formatter changes the post-data gap to one byte in the same `$5E` negative
   mode.
4. Pi1551 currently derives D64/G64 sector counts and default speed zones from
   stock tables, where track 18 is 19 sectors at zone 2.

Runtime verification should still watch writes to `$4000`/CPU port bits 5-6
during Super DOS format of track 18, and record the resulting `trackDensity` and
track length for halftrack index 34. The expected result is density 3 with a
density-3 track length.

## RAMBOard patch interaction

Also checked:

- `/Users/apple/Documents/Maciejdev/15x1-ramexp-github/1551-RAMBOard-FirstBank/rampatch.asm`

That patch has a dedicated `SUPERDOS1551` build mode and loads
`rom/super_dos_1551.bin`. Its main track cache does not determine the number of
sectors from a fixed sector table. Instead, `ReadTrack` reads headers and data
until it sees the first header repeat:

```asm
ReadTrack:
        sta RE_cached_track
        ...
        sta counter
ReadHeader:
        jsr LF560
        ...
        ; read 8 GCR header bytes into RE_cached_headers
        ...
        ; compare current header with first header
        ; if equal, stop and DecodeData
ReadGCRSectorOK:
        inc bufpage+1
        inc counter
        lda counter
        cmp #maxsector
        beq DecodeData
        jmp ReadHeader
```

`maxsector` is 22, so a 21-sector Super-format track fits. `DecodeData` stores
the observed sector count in `RE_max_sector`, and `DoReadCache` searches only up
to that dynamic value. So the RAMBOard cache can determine the number of sectors
in Super mode, provided the drive is already reading the track at the correct
density.

For normal DOS reads, that condition should hold: the patch hooks the low-level
sector read at `$F403`, after the ROM's density setup. In the Super DOS ROM,
that setup is already patched to call `$FDE1`; with `$5E` negative, `$FDE1`
returns 21 sectors and leaves `X=3`, causing the ROM to select the outer-track
density for track 18.

There was one caveat in the RAMBOard fastloader code. Its local head-move/density
helper used the stock table directly:

```asm
L0422:  lda $0202
        sta $29
        sta $79
        ldx #$04
L042B:  cmp $F119,x     ; density zone selector
        dex
        bcs L042B
        txa
        asl
        asl
        asl
        asl
        asl
        sta $38
        lda $01
        and #$9F
        ora $38
        sta $01
        rts
```

That meant the RAMBOard fastloader path would choose stock density for track 18,
not the Super DOS `$FDE1`/`$5E` result. The RAMBOard patch now makes this path
conditional on `SUPER_DOS_PATCHES`, which is enabled by `SUPERDOS1551`: Super
DOS builds call `$FDE1`, while stock builds keep the `$F119` table.
