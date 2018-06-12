////////////////////////////////////////////////////////////////////////////////
////
/// ZC
//

#ifndef _ZC_H
#define _ZC_H

#include "timer_1sec.h"
#include "timer_dim.h"
//#ifndef on_tmr1
// #error RED_MSG "timer1 event handler needs to be defined before zc"
//#endif


#define ZCPIN  RA3 //this pin in input-only, so use it for ZC (which is an input-only function)
#define _ZCPIN  PORTPIN(_PORTA, bit2inx(_RA3)) //this pin in input-only, so use it for ZC (which is an input-only function)


//ZC sampling:
//TODO: consolidate with other bit vars
BANK0 volatile union
{
    struct
    {
        uint8_t zc_present: 1;
        uint8_t zc_60hz: 1; //distinguishes between 50 Hz and 60 Hz (AC)
        uint8_t unused: 6;
    } bits;
    uint8_t allbits;
} zc_state;
#define zc_present  zc_state.bits.zc_present
#define zc_60hz  zc_state.bits.zc_60hz

#if 0 //SDCC generates poor code for byte access to struct :(
BANK0 volatile union
{
    struct
    {
        uint8_t unused: 7;
        uint8_t edge; //lsb
    } bits;
    uint8_t allbits;
} zc_filter; //filter out noise on zc signal
#endif
volatile BANK0 uint8_t zc_filter; //filter out noise on zc signal

//volatile uint8_t zc_delay; //used with Timer 1 to measure 1 sec for zc rate sampling
//volatile uint16_t zc_count; //strictly for zc rate sampling (1 sec interval); not useful for other purposes
volatile uint8_t zc_count; //counts #zc crossings per sec; only needs to be 8 bits (120 for 60 Hz detection)
//#define ZC_TMR1_COUNT  (ONE_SEC / (50 msec)) //#Timer 1 ticks per second

//rising/falling ZC edge patterns:
//these are typically inverse of each other, but can be skewed if needed
//only falling edge is used for dimming; rising edge is only for ZC profiling for debug/dev
#define IsFallingEdge()  ((zc_filter) == 0b11110000)
#define IsRisingEdge()  ((zc_filter) == 0b00001111)

INLINE void zc_sample(void)
{
	/*wait(30 usec, EMPTY)*/;
//	bitcopy(porta & /*(1<<PINOF(ZC_PIN))*/ _ZC_PIN, Carry);
//    CARRY = 0;
//    if (ZCPIN) CARRY = TRUE;
	/*porta += -1<<IOPIN(ZC_PIN);*/
//	rl_nc(zc_filter.allbits); /*shift new sample into lsb of zc filter; room for 8 samples*/
    zc_filter <<= 1;
//    lslf(zc_filter.allbits); //shift new sample into lsb of zc filter; room for 8 samples
//    if (ZCPIN) ++zc_filter.allbits; //shift new sample into lsb of zc filter; room for 8 samples
    if (ZCPIN) zc_filter |= 1;
}


#ifdef ZC_DEBUG //debug
 #ifndef debug
  #define debug() //define debug chain
 #endif
//define globals to shorten symbol names (local vars use function name as prefix):
    volatile AT_NONBANKED(0) uint16_t zc_pin_debug; //= _ZCPIN;
 INLINE void zc_debug(void)
 {
    debug(); //incl prev debug first
    zc_pin_debug = _ZCPIN;
 }
 #undef debug
 #define debug()  zc_debug()
#endif


#ifndef init
 #define init() //initialize function chain
#endif


//sample ZC for 1 sec to see if it's there:
//NOTE: assumes ZC is stable at power on; otherwise, would need to repeat this check
INLINE void init_zc(void)
{
	init(); //prev init first
//	ONPAGE(LEAST_PAGE);
//initialize zc sampling:
//    zc_delay = ZC_TMR1_COUNT; // + 1; //controls sampling period; compensate for one extra (partial) tick at start
//don't need it?    TMR1_16 = 1; //kludge: 
    zc_count = 0; //this should be 25K after 1 sec; used for calibration
    zc_state.allbits = 0; //zc_present = zc_60hz = FALSE; //NOTE: these are sticky
	zc_filter = 0;
//NOTE: zc rate sampling will finish asynchronously during tmr1 event handling
}
#undef init
#define init()  init_zc() //function chain in lieu of static init


#ifndef on_zc_fall
 #define on_zc_fall() //initialize function chain
#endif

//check for ZC signal:
//NOTE: ZC is filtered (to reduce noise), so can't do simple check of ZCPIN here
//NOTE: zc handler will be called on either edge; it should check lsb of zc_filter to pick which edge to respond to
//TODO: add rising vs. falling detection for SSR doubling (requires asymetric zc signal)
//NOTE: need macros here so "return" will exit caller
#define on_zc_check_fall()  \
{ \
    zc_sample(); \
    if (!IsFallingEdge()) return; \
}
//    if (!IsRisingEdge(zc_filter) && !IsFallingEdge(zc_filter)) return;


//zc fall event handler:
//samples ZC then resets display list if needed
INLINE void on_zc_tick(void)
{
	on_zc_fall(); //prev event handlers first
//	ONPAGE(LEAST_PAGE); //keep demo code separate from protocol and I/O so they will fit within first code page with no page selects
//    if (zc_filter.bits.edge) return; //rising edge
//    if (!++zc_count) --zc_count; //count falling edges; prevent overflow so zc freq logic will be correct
//    ++zc_count;
//    if (ZERO) --zc_count; //prevent overflow so zc freq logic will be correct
//    if (zc_count + 1) ++zc_count; //prevent overflow so zc freq logic will be correct
    incfsz_WREG(_zc_count);
    ++zc_count; //prevent overflow so zc freq logic will be correct
}
#undef on_zc_fall
#define on_zc_fall()  on_zc_tick() //event handler function chain


#ifndef on_zc_rise
 #define on_zc_rise() //initialize function chain
#endif

//check for ZC signal:
//NOTE: ZC is filtered (to reduce noise), so can't do simple check of ZCPIN here
//NOTE: zc handler will be called on either edge; it should check lsb of zc_filter to pick which edge to respond to
//TODO: add rising vs. falling detection for SSR doubling (requires asymetric zc signal)
//NOTE: need macros here so "return" will exit caller
//TODO: if zc_fall not used, add zc_sample() here
#define on_zc_check_rise()  if (!IsRisingEdge()) return //already sampled during zc check fall


//#ifndef on_tmr_1sec
// #define on_tmr_1sec() //initialize function chain
//#endif

//#define isZCpresent()  (Timer0_Preset != TMR0_PRESET_DC50Hz)

//TODO:
//#ifndef on_zc_present
// #define on_zc_rise() //initialize function chain
//#endif
//#ifndef on_zc_60hz
// #define on_zc_60hz() //initialize function chain
//#endif


INLINE void on_tmr_1sec_zc(void)
{
	on_tmr_1sec(); //prev event handlers first
    LABDCL(0xC0);
//no; might not get full 60 Hz first time:    if (zc_present) return; //already detected; don't need to keep sampling
//no        zc_present = zc_60hz = FALSE; //made these sticky
//update Timer 0 preset to match measured zc rate:
//    Timer0_Preset = TMR0_PRESET_DC50Hz;
//    if (zc_count >= 10) zc_state.bits.zc_present = TRUE; //treat anything >= 10 Hz as a sync signal
//	if (zc_count > 2 * 55) zc_state.bits.zc_60hz = TRUE; //use 55 Hz (avg of 50 and 60) as threshold; treat anything higher as 60 Hz AC (else it was probably 50 Hz AC)
//treat anything >= 10 Hz as a sync signal
//use 55 Hz (avg of 50 and 60) as threshold; treat anything higher as 60 Hz AC (else it was probably 50 Hz AC)
//    if (zc_count >= 10) { zc_present = 1; Timer0_Preset = TMR0_PRESET_AC50Hz; }
//	if (zc_count > 2 * 55) { zc_60hz = 1; Timer0_Preset = TMR0_PRESET_AC60Hz; }
//NOTE: status bits are sticky
    if (zc_count >= 10) zc_present = 1;
	if (zc_count > 2 * 55) zc_60hz = 1;
//restart sampling:
//since sampling is async, might as well start another sample period
//otherwise, need additional logic to only do it 1x
//    zc_delay = ZC_TMR1_COUNT;
    zc_count = 0; //reset count for next 1 sec sample period
//        return;
//    if (zc_delay == ZC_TMR1_COUNT) zc_count = 0; //wait for first (partial) tmr1 tick before sampling start
}
#undef on_tmr_1sec
#define on_tmr_1sec()  on_tmr_1sec_zc() //event handler function chain


#endif //ndef _ZC_H
//eof
