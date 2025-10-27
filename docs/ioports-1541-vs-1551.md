# 1541-II 

## VIA #1 $1800

```
$1800 PB
7   ATN IN
6-5 device number
4   ATN OUT
3   CLOCK OUT
2   CLOCK IN
1   DATA OUT
0   DATA IN
```

## VIA #2 $1C00

```
$1C00 PB
7   SYNC
6-5 bit rate/density
4   WP sensor
3   LED
2   MOTOR
1-0 STEP MOTOR (HEAD)
$1C01 PA
7-0 data from HEAD
```

# 1551

# CPU port

```
zp00 = $00 ;(9)  cpu data direction register
zp01 = $01 ;(65) cpu port, FDC control register             on 1541:
; bit 0: drive head stepper 0 (STP)                         $1C00-0
; bit 1: drive head stepper 1 (STP)                         $1C00-1
; bit 2: drive motor on/off (MTR)                           $1C00-2
; bit 3: drive LED (ACT) (0 - on, 1 - off)                  $1C00-3
; bit 4: write protect sensor (WPS)                         $1C00-4
; bit 5: density select #0 (DS0)                            $1C00-5
; bit 6: density select #1 (DS1)                            $1C00-6
; bit 7: byte latched (1 - yes, 0 - no)                     SO (VIA-CA1, CPU-SO, w 1571 - VIA1 PB-7) (gate array #26? 41?
```

# Sector read example

# gate array pins

https://www.zimmers.net/anonftp/pub/cbm/schematics/drives/new/1571/1571_Service_Manual_Preliminary_314002-04_(1986_Oct)_Alternate.pdf

## 1551

Sector reading - waiting until drive head shifted in whole data:
on 1551 - check bit 7 of zp1

```
B_F2AD	BIT zp01
	BPL B_F2AD          ; wait until high
	BIT V_4000
	LDA V_4001
	CMP #$52	; header magic value
	BNE B_F2FD
```

No one knows for sure how this flag is cleared. It's certain that this line (called in VICE `byte_ready_level` from `rotation.c`) is set by the serial shift register. On 1541 this is latched in two places - oVerflow input of 6502 latches on edge, and also VIA does it.
On 1541 this is cleared by `CLV` instruction, on 1571 this is cleared in VIA by reading `$180f` register.

But on 1551 it's just a guess that it's latched somewhere (gate array is the only place) and cleared by reading any of the TPI registers.
From 1551 disassembly it's clear that it's not only access to `$4001` and not only access to `$4000` but at least both of them.
VICE clears `byte_ready_level` on access of any TPI register, but I couldn't find any documentation why. But it works. It's just more puzzling *how* it works because as I understand the gate arrays from 1541 and 1551 are the same.

### Implementation

** Working! **

`make clean && make USE_DRIVE1551_CLEAN=1 USE_PI1551_CLEAN=1 V=1`

** Working! **

`make clean && make V=1`

## 1571

same as on 1571 - bit 7 of $180f (reversed polarity)

```
!:		bit $180f
		bmi !-          ; wait until low
		lda $1c01
```

## 1541

on 1541 this is connected to CPU overflow input line

```
!:		bvc !-
		clv
		lda $1c01
		cmp #$52			// is that a header?
```

# Sector write example

## 1541

```
(sector save from 1541 - changes state of  CB2 i DDR PA)
F594: A9 FF     LDA #$FF
F596: 8D 03 1C  STA $1C03       ; port A (read/write head) to output
F599: AD 0C 1C  LDA $1C0C
F59C: 29 1F     AND #$1F
F59E: 09 C0     ORA #$C0        ; change PCR to output   110 = Low output  CB2
F5A0: 8D 0C 1C  STA $1C0C
...
F5CC: AD 0C 1C  LDA $1C0C
F5CF: 09 E0     ORA #$E0        ; PCR to input again     111 = High output CB2
F5D1: 8D 0C 1C  STA $1C0C
F5D4: A9 00     LDA #$00
F5D6: 8D 03 1C  STA $1C03       ; port A (read/write head) to input
```

# Mapping singnals from 1541 to 1551

```
GATE           1541                       1551
19:OE - MODE - CB2 VIA#2                - TIA PC-4
17:     SYNC - PB7 VIA#2                - TIA PC-6
12:    STEP1 - PB0 VIA#2                - CPU P0
11:    STEP0 - PB1 VIA#2                - CPU P1
13:      MTR - PB2 VIA#2                - CPU P2
41:     SOE  - CA2 VIA#2 + CPU SO
42:     BYTE - CA1 VIA#2                - CPU P7 (42->25 (ATNIN), ATNOUT 26->CPU P7) (1571:CA1 VIA#2, CPU SO, 1571:PA7 VIA#1) that's why negated comparing to 1571
15:      DS0 - PB5 VIA#2                - CPU P5
16:      DS1 - PB6 VIA#2                - CPU P6
2:9       YB - PA  VIA#2                - TIA PB
-        WPS - PB4 VIA#2                - CPU P4
-        LED - PB3 VIA#2                - CPU P3 (0 = LED on, 1=LED off, reversed comparing to 1541)              
```

# TIA - triport interface 

```
PA - TCBM port
PB - HEAD data
PC - control drive/TCBM

; TIA 6523 registers $4000-$4005
;
; Port registers A, B and C
V_4000 = $4000	; I/O port towards the computer                         TCBM-IO-0:7 (bidir)
V_4001 = $4001	; connected to the shift register of the R/W head       VIA2 PA $1C01
V_4002 = $4002	; drive control, status and sync register
;
; bit 7: DAV (DAta Valid)                                               TCBM-IO-8 (input)
; 	handshake from bit #6 from $FEF2/FEC2 of plus/4
; bit 6: SYNC                                                           $1C00-7
; 	0 -> SYNC found
; 	1 -> no SYNC
; bit 5: DEVNUM (jumper)                                                  $1800-6/5
;+ 	drive number (0 -> 8, 1 -> 9)                                       (jumper to GND, normally 0 maps->drive 8, DEV=bit 2 set) [setting from config file]
; bit 4: MODE (R/W head)                                                CB2 VIA#2
; 	0 -> head in write mode
; 	1 -> head in read mode
; bit 3: ACK (ACKnowledge)                                              TCBM-IO-9 (output)
; 	handshake to bit #7 of $FEF2/$FEC2
; bit 2: DEV                                                            TCBM-IO-10 $1800-6/5 (zanegowany bit 5) (output)
; 	1 -> drive mapped at $FEF0 on plus/4 #8
; 	0 -> drive mapped at $FEC0 on plus/4 #9
; bit 1: STATUS1 - mapped at $FEF1/FEC1 on plus/4                       TCBM-IO-11 (output)
; bit 0: STATUS0 - mapped at $FEF1/FEC1 on plus/4                       TCBM-IO-12 (output)
; 		  01 Timeout during reading
; 		  10 Timeout during writing
; 		  11 End of data
;
; Data direction registers for ports A, B and C
V_4003 = $4003
V_4004 = $4004
V_4005 = $4005
```

# 1551 IRQ

https://www.softwolves.com/arkiv/cbm-hackers/15/15949.html

1551 IRQ from NE555 oscillator, every 10 msec == 100Hz == 20000 cycles (2MHz) or 10000 cycles (1MHz)

No NMI source (1541 doesn't have either)

# RPi3 GPIO

GPIO

https://datasheets.raspberrypi.com/bcm2835/bcm2835-peripherals.pdf

# Pi1551 device number 

Bit 5 input of $4002 (port C) - 0 -- drive 8, 1 -- drive 9 (so J1 is open on https://www.zimmers.net/anonftp/pub/cbm/schematics/drives/new/1551/251860.gif )

```
.8:f14f  AD 02 40    LDA $4002
.8:f152  29 20       AND #$20
.8:f154  D0 03       BNE $F159
.8:f156  A9 08       LDA #$08
.8:f158  2C          BIT $09A9
.8:f159  A9 09       LDA #$09
.8:f15b  85 66       STA $66
                     JSR $C20D

.8:c20d  0A          ASL A      ; $08 -> $10  ; $09 -> $12
.8:c20e  0A          ASL A      ; $10 -> $20  ; $12 -> $24
.8:c20f  29 04       AND #$04   ; $00         ; $04
.8:c211  49 04       EOR #$04   ; $04         ; $00
.8:c213  85 97       STA $97
.8:c215  AD 02 40    LDA $4002
.8:c218  29 FB       AND #$FB
.8:c21a  05 97       ORA $97
.8:c21c  8D 02 40    STA $4002
.8:c21f  60          RTS
```

# Pi1551 code mapping between 1541 (VIA#2) and 1551 (TPI+CPU)

## SYNC

```
was:
m_pVIA->GetPortB()->SetInput(0x80)
is:
m_pTPI->GetPortC()->SetInput(0x40)
```

## WRITING

```
was:
bool writing = ((FCR & m6523::FCR_CB2_OUTPUT_MODE0) == 0) && ((FCR & m6523::FCR_CB2_IO) != 0);
is:
bool writing = !(m_pTPI->GetPortC()->GetOutput() & 0x10); // TIA PC bit 4 MODE: 0=write, 1=read (read from Port C output)
```

Additional:

```
was:
SYNC reported whenever last 10 bits are 1s
is:
SYNC (PC6) asserted only when reading; forced high while writing (UC2 gated by R/!W)
```

## HEAD PORT WRITE

```
was (write):
writeShiftRegister = m_pVIA->GetPortA()->GetOutput();
is:
writeShiftRegister = m_pTPI->GetPortB()->GetOutput();
```

## HEAD PORT READ

```
was (read):
m_pVIA->GetPortA()->SetInput(writeShiftRegister);
is:
m_pTPI->GetPortB()->SetInput(writeShiftRegister);
```

## SO - byte latch flag

```
was:
SO = (m_pVIA->GetFCR() & m6523::FCR_CA2_OUTPUT_MODE0) != 0;	// bit 2 of the FCR indicates "Byte Ready Active" turned on or not.
is:
SO = true;  // 1551 has no FCR register; byte ready is always active in hardware
```

```
CA1 (bytelatch)
was:
m_pVIA->InputCA1(!SO);
is:
m_pTPI->GetPortCPU()->SetInput(0x80, SO);
```

## WRITE PROTECT sensor

```
was:
m_pVIA->GetPortB()->SetInput(0x10, !diskImage->GetReadOnly())
is:
m_pTPI->GetPortCPU()->SetInput(0x10, !diskImage->GetReadOnly())
```
