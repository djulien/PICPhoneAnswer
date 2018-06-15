//Escape room phone player:
//ASM-fixup script (C) 2010-2018 djulien

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
#define LED_DEBUG


//#include <xc.h> //automatically selects correct device header defs
//#include "helpers.h"
//#include "compiler.h"
//#include "clock.h" //override default clock rate
#include "config.h"
//#include "timer_dim.h"
//#include "timer_50msec.h"
#include "timer_1sec.h"
//#include "wdt.h" //TODO?
//#include "serial.h"
//#include "zc.h"
//#include "outports.h"


////////////////////////////////////////////////////////////////////////////////
////
/// serial port
//

#if 0
#if isPORTA(_SEROUT_PIN)
 #define _SEROUT_TRISA  PINOF(_SEROUT_PIN)
#else
 #define _SEROUT_TRISA  0
#endif
#if isPORTBC(_SEROUT_PIN)
 #define _SEROUT_TRISBC  PINOF(_SEROUT_PIN)
#else
 #define _SEROUT_TRISBC  0
#endif
#endif


////////////////////////////////////////////////////////////////////////////////
////
/// led (test)
//

#define LED_PIN  RC2
#define _LED_PIN  0xC2

#if 0
#define _LED_PIN  PORTPIN(_PORTA, bit2inx(_RA5))

//set TRIS to handle output-only pins:
//kludge: macro body too long for MPASM; use shorter macros
#if isPORTA(_LED_PIN)
 #define _LED_TRISA  PINOF(_LED_PIN)
#else
 #define _LED_TRISA  0
#endif
#if isPORTBC(_LED_PIN)
 #define _LED_TRISBC  PINOF(_LED_PIN)
#else
 #define _LED_TRISBC  0
#endif

#define TRISA_INIT  ~(_LED_TRISA | _SEROUT_TRISA) //should be ~0x28 == 0xd7 for LED on RA5
#define TRISBC_INIT  ~(_SEROUT_TRISBC | _LED_TRISBC) //should be ~0x10 == 0xef


volatile BANK0 uint8_t portbuf[2];
#endif


#if 0
#ifdef debug
 volatile AT_NONBANKED(0) uint8_t trisa_debug;
 volatile AT_NONBANKED(0) uint8_t trisbc_debug;
 INLINE void tris_debug(void)
 {
    debug(); //incl prev debug info
    trisa_debug = TRISA_INIT;
    trisbc_debug = TRISBC_INIT;
 }
 #undef debug
 #define debug()  tris_debug()
#endif
#endif


#define PORTAPIN(portpin)  IIFNZ(isPORTA(portpin), PINOF(portpin))
#define PORTBPIN(portpin)  IIFNZ(isPORTB(portpin), PINOF(portpin))
#define PORTCPIN(portpin)  IIFNZ(isPORTC(portpin), PINOF(portpin))
#define PORTBCPIN(portpin)  IIFNZ(isPORTBC(portpin), PINOF(portpin))

#define PORTAMASK(portpin)  IIFNZ(isPORTA(portpin), 1 << PINOF(portpin))
#define PORTBMASK(portpin)  IIFNZ(isPORTB(portpin), 1 << PINOF(portpin))
#define PORTCMASK(portpin)  IIFNZ(isPORTC(portpin), 1 << PINOF(portpin))
#define PORTBCMASK(portpin)  IIFNZ(isPORTBC(portpin), 1 << PINOF(portpin))
#define PORTMAP16(portpin)  ((PORTAMASK(portpin) << 8) | PORTBCMASK(portpin))

#define _LED_MASK  PORTMAP16(_LED_PIN)
//encode port A and port B/C into one 16-bit value:
//port A in upper byte, port B/C in lower byte
//#define ABC2bits16(Abits, BCbits)  ((Abits) << 8) | ((Bbits) & 0xff))
//#define Abits(bits16)  ((bits16) >> 8)
//#define BCbits(bits16)  ((bits16) & 0xff)

#ifdef LED_DEBUG //debug
 #ifndef debug
  #define debug() //define debug chain
 #endif
//define globals to shorten symbol names (local vars use function name as prefix):
    AT_NONBANKED(0) volatile uint16_t led_debug_val1;
    AT_NONBANKED(0) volatile uint16_t led_debug_val2;
    AT_NONBANKED(0) volatile uint16_t led_debug_val3;
    AT_NONBANKED(0) volatile uint16_t led_debug_val4;
    AT_NONBANKED(0) volatile uint16_t led_debug_val5;
    AT_NONBANKED(0) volatile uint16_t led_debug_val6;
//    AT_NONBANKED(0) volatile uint16_t zcpin_debug; //= _ZC_PIN;
 INLINE void led_debug(void)
 {
    debug(); //incl prev debug info
//use 16 bits to show port + pin:
    led_debug_val1 = _LED_PIN;
    led_debug_val2 = _LED_MASK;
    led_debug_val3 = PORTAPIN(_LED_PIN);
    led_debug_val4 = PORTBCPIN(_LED_PIN);
    led_debug_val5 = PORTAMASK(_LED_PIN);
    led_debug_val6 = PORTBCMASK(_LED_PIN);
//    serin_debug = _SERIN_PIN;
//    serout_debug = _SEROUT_PIN;
//    zcpin_debug = _ZC_PIN;
 }
 #undef debug
 #define debug() led_debug()
#endif


//;initialize front panel data:
//grouped to reduce bank selects
INLINE void led_init(void)
{
	init(); //prev init first; NOTE: no race condition with cooperative event handling (no interrupts)
//enable digital I/O, disable analog functions:
	IFANSELA(ANSELA = 0); //must turn off analog functions for digital I/O
	IFANSELBC(ANSELBC = 0);
//weak pull-ups for input pins are not floating:
    WPUA = 0xff & ~0;
    IFWPUBC(WPUBC = 0xff & ~0);
//leave non-output pins as hi-Z:
    TRISA = 0xff & ~PORTAMASK(_LED_PIN); //| Abits(SEROUT)); //0b111111; //Abits(COL_PINS) | Abits(pin2bits16(SERIN);
    TRISBC = 0xff & ~PORTBCMASK(_LED_PIN); //(BCbits(ROW_PINS) | BCbits(SEROUT)); //0b100000; //BCbits(COL_PINS) | BCbits(pin2bits16(SERIN);
    PORTA = PORTBC = 0;
    IFCM1CON0(CM1CON0 = ~0); //;configure comparator inputs as digital I/O (no comparators); overrides TRISC (page 63, 44, 122); must be OFF for digital I/O to work! needed for PIC16F688; redundant with POR value for PIC16F182X
//	TRISA = ~PORTA_BITS & TRISA_INIT;
//	TRISBC = PORTBC_BITS & TRISBC_INIT;
//	PORTA = 0;
//	PORTBC = 0;
}
#undef init
#define init()  led_init() //function chain in lieu of static init


INLINE void led_1sec(void)
{
	on_tmr_1sec(); //prev event handlers first
	if (LED_PIN) LED_PIN = 0;
    else LED_PIN = 1;
}
#undef on_tmr_1sec
#define on_tmr_1sec()  led_1sec() //event handler function chain


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
    for (;;) //poll for events
    {
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