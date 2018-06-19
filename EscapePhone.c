//Escape room phone player:
//ASM-fixup script (C) 2010-2018 djulien

//detect:
//pk2cmd -p -i
//pk2cmd -?v
//to pgm:
//MPLAB build
//pk2cmd -PPIC16F688 -M -Y -Fbuild/EscapePhone.HEX
//to test:
//run code:  pk2cmd -PPIC16F88 [-A5] -T 
//reset: pk2cmd -PPIC16F688 -R


//#define CLOCK_FREQ  (32 MHz) //max osc freq (with PLL); PIC will use int osc if ext clock is absent
#define CLOCK_FREQ  (4 MHz) //1 MIPS
//#define CLOCK_FREQ  18432000 //20 MHz is max ext clock freq for older PICs; comment this out to run at max int osc freq; PIC will use int osc if ext clock is absent
//#define Timer0_range  (100 usec)
//#define Timer1_halfRange  (50 msec/2) //kludge: BoostC gets /0 error with 50 msec (probably 16-bit arith overflow), so use a smaller value
//receive at 5/6 WS281X bit rate (which is 800 KHz); this gives 2 bytes per WS281X node (8 data bits + 1 start/stop bit per byte)
#define BAUD_RATE  9600 //(8 MHz / 12) ///10 * 5/6) //no-needs to be a little faster than 3x WS281X data rate (2.4 MHz), or a multiple of it; this is just about the limit for an 8 MIPS PIC16X
// 2/3 Mbps => 15 usec/char; can rcv 2 bytes/WS281X node
//#define BAUD_RATE  (10 MHz) //needs to align with visual bit-banging from GPU; //be a little faster than 3x WS281X data rate (2.4 MHz); this is just about the limit for an 8 MIPS PIC16X
//#define REJECT_FRERRS //don't process frame errors (corrupted data could mess up display)
//#define NUMSLOTS  256 //256 dimming slots [0..255] (8 bits) ~= 32.5 usec per slot @ 60 Hz AC
//#define NUM_SSR  8 //#IO pins for chipiplexed/charlieplexed SSRs; 8 gives 8 * 7 == 56 channels
//#define ALL_OFF  0 //for active low (Common Anode) SSRs, use 0xFF or ~0; for active high (Common Cathode), use 0
//#define RGB_FMT  0x123 //set this to 0x213 if WS281X on front panel for R <-> G swap
//#define debug  //include compile-time value checking

//include compile-time value checking:
#define SERIAL_DEBUG
#define TIMER1_DEBUG
#define CLOCK_DEBUG
#define SCAN_DEBUG
#define MP3_TEST


//#include <xc.h> //automatically selects correct device header defs
//#include "helpers.h"
//#include "compiler.h"
//#include "clock.h" //override default clock rate
#include "config.h"
//#include "timer_dim.h"
//#include "timer_50msec.h"
#include "timer_1sec.h"
//#include "wdt.h" //TODO?
#include "serial.h"
//#include "zc.h"
//#include "outports.h"


//TODO: move to helpers:
//encode port A and port B/C into one 16-bit value to allow subsequent split and generic port logic:
//port A in upper byte, port B/C in lower byte
#define PORTAPIN(portpin)  IIFNZ(isPORTA(portpin), PINOF(portpin))
//#define PORTBPIN(portpin)  IIFNZ(isPORTB(portpin), PINOF(portpin))
//#define PORTCPIN(portpin)  IIFNZ(isPORTC(portpin), PINOF(portpin))
#define PORTBCPIN(portpin)  IIFNZ(isPORTBC(portpin), PINOF(portpin))

//CAUTION: use "UL" to preserve bits/avoid warning
#define PORTAMASK(portpin)  IIFNZ(isPORTA(portpin), 1UL << PINOF(portpin))
//#define PORTBMASK(portpin)  IIFNZ(isPORTB(portpin), 1 << PINOF(portpin))
//#define PORTCMASK(portpin)  IIFNZ(isPORTC(portpin), 1 << PINOF(portpin))
#define PORTBCMASK(portpin)  IIFNZ(isPORTBC(portpin), 1UL << PINOF(portpin))
//#define pin2bits16(pin)  ABC2bits(IIFNZ(isPORTA(pin), 1 << PINOF(pin)), IIFNZ(isPORTBC(pin), 1 << PINOF(pin)))
#define PORTMAP16(portpin)  ((PORTAMASK(portpin) << 8) | PORTBCMASK(portpin))
//#define ABC2bits16(Abits, BCbits)  ((Abits) << 8) | ((Bbits) & 0xff))
#define Abits(bits16)  ((bits16) >> 8) //& PORTA_MASK
#define BCbits(bits16)  ((bits16) & 0xff) //& PORTBC_MASK


////////////////////////////////////////////////////////////////////////////////
////
/// Keyboard scanning:
//

//cols (3) on Port A, rows (4) on Port B/C:
//#define LED_PIN  RC2
//#define _LED_PIN  0xC2
//#define _LED_MASK  PORTMAP16(_LED_PIN)
#define COL6_PIN  RA5
#define COL7_PIN  RA4
#define COL8_PIN  RA2

#define ROW0_PIN  RC0
#define ROW1_PIN  RC1
#define ROW2_PIN  RC2
#define ROW3_PIN  RC3

#define _COL6_PIN  0xA5
#define _COL7_PIN  0xA4
#define _COL8_PIN  0xA2

#define _ROW0_PIN  0xC0
#define _ROW1_PIN  0xC1
#define _ROW2_PIN  0xC2
#define _ROW3_PIN  0xC3

#define COL6_MASK  PORTMAP16(_COL6_PIN)
#define COL7_MASK  PORTMAP16(_COL7_PIN)
#define COL8_MASK  PORTMAP16(_COL8_PIN)

#define ROW0_MASK  PORTMAP16(_ROW0_PIN)
#define ROW1_MASK  PORTMAP16(_ROW1_PIN)
#define ROW2_MASK  PORTMAP16(_ROW2_PIN)
#define ROW3_MASK  PORTMAP16(_ROW3_PIN)

//rows = output, cols = input (allows slightly more efficient scanning):
#define COLPORT  PORTA
#define ROWPORT  PORTBC
#define COLbits(bits16)  Abits(bits16)
#define ROWbits(bits16)  BCbits(bits16)

//combine for generic port logic:
//#define COL_PINS  ((PIN2BIT(RA5) | PIN2BIT(RA4) | PIN2BIT(RA2)) << 8)
//#define ROW_PINS  (PIN2BIT(RC0) | PIN2BIT(RC0) | PIN2BIT(RC0) | PIN2BIT(RC0))
#define COLSIN_MASK  (COL6_MASK | COL7_MASK | COL8_MASK)
#define ROWSOUT_MASK  (ROW0_MASK | ROW1_MASK | ROW2_MASK | ROW3_MASK)

//#define COLROW(c, r)  ABC2bits(r, c)
//#define COL_PINS  (pin2bits16(COL6) | pin2bits16(COL7) | pin2bits16(COL8))
//#define ROW_PINS  (pin2bits16(ROW0) | pin2bits16(ROW1) | pin2bits16(ROW2) | pin2bits16(ROW3))

//#define KEY_1  COLROW(COL6, ROW0)
//#define KEY_2  COLROW(COL7, ROW0)
//#define KEY_3  COLROW(COL8, ROW0)
//#define KEY_4  COLROW(COL6, ROW1)
//#define KEY_5  COLROW(COL7, ROW1)
//#define KEY_6  COLROW(COL8, ROW1)
//#define KEY_7  COLROW(COL6, ROW2)
//#define KEY_8  COLROW(COL7, ROW2)
//#define KEY_9  COLROW(COL8, ROW2)
//#define KEY_STAR  COLROW(COL6, ROW3)
//#define KEY_0  COLROW(COL7, ROW3)
//#define KEY_HASH  COLROW(COL8, ROW3)


#ifdef SCAN_DEBUG //debug
 #ifndef debug
  #define debug() //define debug chain
 #endif
//define globals to shorten symbol names (local vars use function name as prefix):
    AT_NONBANKED(0) volatile uint16_t scan_debug_val1;
    AT_NONBANKED(0) volatile uint16_t scan_debug_val2;
    AT_NONBANKED(0) volatile uint16_t scan_debug_val3;
    AT_NONBANKED(0) volatile uint16_t scan_debug_val4;
    AT_NONBANKED(0) volatile uint16_t scan_debug_val5;
    AT_NONBANKED(0) volatile uint16_t scan_debug_val6;
    AT_NONBANKED(0) volatile uint16_t scan_debug_val7;
//    AT_NONBANKED(0) volatile uint16_t zcpin_debug; //= _ZC_PIN;
 INLINE void scan_debug(void)
 {
    debug(); //incl prev debug info
//use 16 bits to show port + pin:
    scan_debug_val1 = COL6_MASK;
    scan_debug_val2 = ROW3_MASK;
//    scan_debug_val3 = ROWSOUT_MASK;
    scan_debug_val3 = Abits(ROWSOUT_MASK);
    scan_debug_val4 = BCbits(ROWSOUT_MASK);
    scan_debug_val5 = ROWbits(ROW1_MASK);
//    scan_debug_val6 = ROW1_MASK;
    scan_debug_val6 = COLbits(ROW1_MASK);
    scan_debug_val7 = 0xff & ~0;

//    serin_debug = _SERIN_PIN;
//    serout_debug = _SEROUT_PIN;
//    zcpin_debug = _ZC_PIN;
 }
 #undef debug
 #define debug() scan_debug()
#endif


//;initialize keyboard scanning pins:
//also sets serial port I/O pins
//grouped to reduce bank selects
INLINE void port_init(void)
{
	init(); //prev init first; NOTE: no race condition with cooperative event handling (no interrupts)
//enable digital I/O, disable analog functions:
	IFANSELA(ANSELA = 0); //must turn off analog functions for digital I/O
	IFANSELBC(ANSELBC = 0);
//weak pull-ups for input pins are not floating:
    WPUA = PORTA_MASK & ~0;
    IFWPUBC(WPUBC = PORTBC_MASK & ~0);
//set output pins, leave non-output pins as hi-Z:
    TRISA = PORTA_MASK & ~(Abits(ROWSOUT_MASK) | Abits(SEROUT_MASK)); //0b111111; //Abits(COL_PINS) | Abits(pin2bits16(SERIN);
    TRISBC = PORTBC_MASK & ~(BCbits(ROWSOUT_MASK) | BCbits(SEROUT_MASK)); //0b010000; //BCbits(COL_PINS) | BCbits(pin2bits16(SERIN);
    PORTA = PORTBC = 0;
//    IFCM1CON0(CM1CON0 = 0xff & ~0); //;configure comparator inputs as digital I/O (no comparators); overrides TRISC (page 63, 44, 122); must be OFF for digital I/O to work! needed for PIC16F688; redundant with POR value for PIC16F182X
//	TRISA = ~PORTA_BITS & TRISA_INIT;
//	TRISBC = PORTBC_BITS & TRISBC_INIT;
//	PORTA = 0;
//	PORTBC = 0;
}
#undef init
#define init()  port_init() //function chain in lieu of static init


//check for key pressed:
//ZERO = key-pressed flag, WREG = key#
void keypress_WREG()
{
//    CARRY = TRUE;
    ZERO = FALSE;
//    for (;;) //scan until key received
//    {
//    PORTA = Abits(ROW0);
        ROWPORT = ROWbits(ROW0_MASK);
        if (COL6_PIN) retlw(1);
        if (COL7_PIN) retlw(2);
        if (COL8_PIN) retlw(3);

        ROWPORT = ROWbits(ROW1_MASK);
        if (COL6_PIN) retlw(4);
        if (COL7_PIN) retlw(5);
        if (COL8_PIN) retlw(6);

        ROWPORT = ROWbits(ROW2_MASK);
        if (COL6_PIN) retlw(7);
        if (COL7_PIN) retlw(8);
        if (COL8_PIN) retlw(9);

        ROWPORT = ROWbits(ROW3_MASK);
        if (COL6_PIN) retlw(11);
        if (COL7_PIN) retlw(10); //avoid confusion with null/0
        if (COL8_PIN) retlw(12);
//    }
//    CARRY = FALSE;
//    RETLW(0);
    ZERO = TRUE;
}


INLINE void scankey_50msec(void)
{
	on_tmr_50msec(); //prev event handlers first
//    keypress_WREG();
}
#undef on_tmr_50msec
#define on_tmr_50msec()  scankey_50msec() //event handler function chain


//inline void on_tmr1_debounce()
//{
//    on_tmr1();
//}

//bkg event handler:
//void yield()
//{
//    on_tmr1(); //only need Timer 1 for debounce
//}


////////////////////////////////////////////////////////////////////////////////
////
/// MP3 player (controlled by serial port):
//


//DFPlayer commands (see datasheet):
#define PLAYTRK_CMD  0x03
#define VOLUME_CMD  0x06
#define RESET_CMD  0x0C
#define START_CMD  0x0D //??


//command buffer:
#define START_BYTE  0x7E //firsst byte
#define VER_BYTE  0xFF //second byte
//length = third byte
//command = 4th byte
//feedback flag = 5th byte
//param = 6th, 7th bytes (high, low)
//checksum = 8th, 9th bytes (high, low)
#define END_BYTE  0xEF


#define Volume(volume)  { WREG = volume; Volume_WREG(); }
non_inline void Volume_WREG(void)
{
    uint8_t param = WREG;
    PutChar(START_BYTE);
    PutChar(VER_BYTE);
    PutChar(1); //cmd len
    PutChar(VOLUME_CMD);
    PutChar(0); //no feedback
    PutChar(0); PutChar(param); //high, low
    PutChar(chksumH); PutChar(chksumL);
    PutChar(END_BYTE);
}


#define Playback(sound)  { WREG = sound; Playback_WREG(); }
non_inline void Playback_WREG(void)
{
    uint8_t param = WREG;
    PutChar(START_BYTE);
    PutChar(VER_BYTE);
    PutChar(1); //cmd len
    PutChar(PLAYTRK_CMD);
    PutChar(0); //no feedback
    PutChar(0); PutChar(param); //high, low
    PutChar(chksumH); PutChar(chksumL);
    PutChar(END_BYTE);
}


////////////////////////////////////////////////////////////////////////////////
////
/// basic hardware tests (blink led, play mp3):
//

#ifdef CALIBRATE_TIMER
//NOTE: uses port_init() defined above
#define LED_PIN  RC2
#define _LED_PIN  0xC2
#define _LED_MASK  PORTMAP16(_LED_PIN)

INLINE void led_1sec(void)
{
	on_tmr_1sec(); //prev event handlers first
	if (LED_PIN) LED_PIN = 0;
    else LED_PIN = 1;
}
#undef on_tmr_1sec
#define on_tmr_1sec()  led_1sec() //event handler function chain
#endif //def CALIBRATE_TIMER


#ifdef MP3_TEST
INLINE void mp3_test(void)
{
	init(); //prev init first; NOTE: no race condition with cooperative event handling (no interrupts)
    Volume(10); //volume level 0 to 30
    Playback(1); //play first mp3 (named "0001*.mp3")
    --PCL; //don't do anything else
}
#undef init
#define init()  mp3_test() //function chain in lieu of static init
#endif //def MP3_TEST


////////////////////////////////////////////////////////////////////////////////
////
/// Main logic
//

#include "func_chains.h" //finalize function chains; NOTE: this adds call/return/banksel overhead, but makes debug easier


void main(void)
{
//	ONPAGE(LEAST_PAGE); //put code where it will fit with no page selects
//    test();

    debug(); //incl debug info (not executable)
	init(); //1-time set up of control regs, memory, etc.
//    PORTC = 0xff;
    for (;;) //poll for events
    {
//        --PCL;
//        on_tmr_dim();
        on_tmr_50msec(); //CAUTION: must come before tmr_1sec()
        on_tmr_1sec();
//these should probably come last (they use above timers):
//        on_rx();
//        on_zc_rise(); //PERF: best: 8 instr (1 usec), worst: 36 instr + I/O func (4.5 usec + 120+ usec)
//        on_zc_fall(); //PERF: best: 12 instr (1.5 usec), worst: 28 instr (3.5 usec)
    }
}

//eof