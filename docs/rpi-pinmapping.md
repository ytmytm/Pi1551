
01	02
...
39	40

			WIRING			WIRING 2.1 (split lines, opt. inverted)			TCBM							
01	3.3V                                                                                        01	3.3V
02	5V                                                                                          02	5V
03	GPIO02	ATN IN/OUT		SDA (LCD)										SDA                 03	GPIO02
04	5V                                                                                          04	5V
05	GPIO03	RESET			SCL (LCD)										SCL                 05	GPIO03
06	GND                                                                                         06	GND
07	GPIO04	SW4				SW4	(exit folder)								SW4                 07	GPIO04
08	GPIO14                                                                                      08	GPIO14
09	GND                                                                                         09	GND
10	GPIO15                                                                                      10	GPIO15
11	GPIO17	CLK IN/OUT		CLK OUT                                                             11	GPIO17
12	GPIO18	DATA IN/OUT		DATA OUT                                                            12	GPIO18
13	GPIO27	SW1	/ rot BUT	SW1 (reset/select)								SW1                 13	GPIO27
14	GND                                                                                         14	GND
15	GPIO22	SW2	/ rot CLK	SW2	(prev/up)									SW2                 15	GPIO22
16	GPIO23	SW3	/ rot DAT	SW3	(next/down)									SW3                 16	GPIO23
17	3.3V                                                                                        17	3.3V
18	GPIO24					ATN IN                                                              18	GPIO24
19	GPIO10                                                                                      19	GPIO10
20	GND                                                                                         20	GND
21	GPIO09                                                                                      21	GPIO09
22	GPIO25					DATA IN                                                             22	GPIO25
23	GPIO11                                                                                      23	GPIO11
24	GPIO08                                                                                      24	GPIO08
25	GND                                                                                         25	GND
26	GPIO07                                                                                      26	GPIO07
27	ID SD	SDA (LCD)                                                                           27	ID SD
28	ID SC	SCL (LCD)                                                                           28	ID SC
29	GPIO05	SW5				SW5	(insert disk)								SW5                 29	GPIO05
30	GND                                                                                         30	GND
31	GPIO06                                                                                      31	GPIO06
32	GPIO12                                                                                      32	GPIO12
33	GPIO13	PIEZO			PIEZO											PIEZO               33	GPIO13
34	GND                                                                                         34	GND
35	GPIO19                                                                                      35	GPIO19
36	GPIO16	LED				LED												LED                 36	GPIO16
37	GPIO26					CLK IN                                                              37	GPIO26
38	GPIO20					RESET IN                                                            38	GPIO20
39	GND                                                                                         39	GND
40	GPIO21                                                                                      40	GPIO21


bitowo:
GPIO02	SDA
GPIO03	SCL
GPIO04	SW4 (n/a w/ rotary)
GPIO05	SW5	(n/a w/ rotary)
GPIO06
GPIO07
GPIO08
GPIO09
GPIO10
GPIO11
GPIO12
GPIO13	PIEZO
GPIO14
GPIO15
GPIO16	LED
GPIO17
GPIO18
GPIO19
GPIO20
GPIO21
GPIO22	SW2
GPIO23	SW3
GPIO24
GPIO25
GPIO26
GPIO27	SW1

17 wolne, 13 potrzebne
- jak się przesunie LED na 9 to jest dziura na 8 bitów 14-21
- wyjścia 10-12 (ACK, ST0, ST1)
- wejście na 6/21/24/26? (DAV)
- wejście na 6/21/24/26? (RESET)


fizycznie obok siebie?
12+GND
DAV
ACK
ST0
ST1
DIO1-7

albo przesunąć SW3 z 23 i LED z 16 wszystko jest na jednej stronie - akurat 12

GPIO: 14/15/18/23/24/25/08/07/12/16/20/21
      DIO1-DIO8 / DAV / STATUS0 / ACK / STATUS1
DIO1-8: 0:14/15/18/23/24/25/08/07:7
DAV: 12
STATUS0: 16
ACK: 20
STATUS1: 21

GPIO: 02/03/04/17/27/22/10/09/11/05/06/13/19/26
      S+ S+ S+ S  S+ S+ L  D  R  S+    S+
      D  C  W  W  W  W  E  E  S  W     N
      A  L  4  3  1  2  D  V  T  5     D
+SW3 17 (was 23)
+LED 10 (was 16)
+DEV 09
+/RESET 11 (was 20)
+GND
