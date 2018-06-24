////////////////////////////////////////////////////////////////////////////////
////
/// Timer0
//

#ifndef _TIMER0_H
#define _TIMER0_H

#include "compiler.h" //device registers
#include "helpers.h" //RED_MSG, NumBits8(), TOSTR()
#include "clock.h" //TimerPreset()
#include "zc.h" //zc_* vars; TODO: relax this dependency
//#include "timer_1sec.h"


#ifndef Timer0_range
 #define Timer0_range  (100 usec)
#endif

#define TMR0_TICKS(usec)  (0 - TimerPreset(usec, 0, Timer0, CLOCK_FREQ))
#define Timer0_Limit  Instr2uSec(256 * Timer0_Prescalar, CLOCK_FREQ) //max duration for Timer 0

//choose the smallest prescalar that will give the desired range:
//smaller prescalars give better accuracy, but it needs to be large enough to cover the desired range of time intervals
//since ext clock freq > int osc freq, only need to check faster (ext) freq here

//kludge: use trial & error to select prescalar:
//hard-coded values work, except BoostC has arithmetic errors with values of 32768 so try to choose an alternate value here
//#define Timer0_Prescalar  4 //max 256; 4:1 prescalar + 8-bit timer gives 1K instr total range (128 - 205 usec); 8-bit loop gives ~32 - 52 msec total
//#define Timer1_Prescalar  4 //max 8; 4:1 prescalar + 16-bit timer gives ~262K instr total range (~ 33 - 52 msec); 8-bit loop gives ~8 - 13 sec total
#define Timer0_Prescalar  1
#if Timer0_Limit < 1
 #warning RED_MSG "[ERROR] Timer0 limit arithmetic bad" TOSTR(Timer0_Limit)
#endif
#if Timer0_Limit < Timer0_range
 #undef Timer0_Prescalar
 #define Timer0_Prescalar  2
 #if Timer0_Limit < Timer0_range
  #undef Timer0_Prescalar
  #define Timer0_Prescalar  4
  #if Timer0_Limit < Timer0_range
   #undef Timer0_Prescalar
   #define Timer0_Prescalar  8
   #if Timer0_Limit < Timer0_range //could go up to 256, but getting inaccurate by then so stop here
    #error RED_MSG "[ERROR] Can't find a Timer0 prescalar to give " TOSTR(Timer0_range) " usec range"
   #endif
  #endif
 #endif
#endif

#if 0
#if Timer0_Limit == 256*4000/8000 //8 MIPS
 #define Timer0_Limit_tostr  "128 usec"
// #undef Timer0_Limit
// #define Timer0_Limit  128 //kludge: avoid macro body problems or arithmetic errors in BoostC
#elif Timer0_Limit == 256*2000/4608 //4.6 MIPS
 #define Timer0_Limit_tostr  "111 usec"
// #undef Timer0_Limit
// #define Timer0_Limit  222 //kludge: avoid macro body problems or arithmetic errors in BoostC
#elif Timer0_Limit == 256*4000/5000 //5 MIPS
 #define Timer0_Limit_tostr  "204 usec"
// #undef Timer0_Limit
// #define Timer0_Limit  204 //kludge: avoid macro body problems or arithmetic errors in BoostC
#elif Timer0_Limit == 256*4000/4608 //4.6 MIPS
 #define Timer0_Limit_tostr  "222 usec"
// #undef Timer0_Limit
// #define Timer0_Limit  222 //kludge: avoid macro body problems or arithmetic errors in BoostC
#elif Timer0_Limit == 0
 #error RED_MSG "[ERROR] Timer 0 limit arithmetic is broken"
#else
 #define Timer0_Limit_tostr  Timer0_Limit usec
#endif
//#endif
//#endif
//#endif
//#endif
#endif


#warning BLUE_MSG "[INFO] Timer 0 limit is " TOSTR(Timer0_Limit) " with " TOSTR(Timer0_Prescalar) ":1 prescalar."

//volatile bit Timer0Wrap @adrsof(INTCON).T0IF; //timer 0 8-bit wrap-around
#define Timer0Wrap  T0IF //timer 0 8-bit wrap-around


//asociate timer regs with names:
#define Timer0_reg  TMR0 //tmr0
#define Timer0_ADDR  TMR0_ADDR


//OPTIONS reg configuration:
//Turns misc control bits on/off, and set prescalar as determined above.
#define MY_OPTIONS(clock)  \
(0 \
	| IIFNZ(FALSE, /*1<<NOT_WPUEN*/ _NOT_WPUEN) /*;enable weak pull-ups on PORTA (needed to pull ZC high when open); might mess up charlieplexing, so turn off for other pins*/ \
	| IIFNZ(DONT_CARE, /*1<<T0SE*/ _T0SE) /*;Timer 0 source edge: don't care*/ \
	| IIFNZ(DONT_CARE, /*1<<INTEDG*/ _INTEDG) /*;Ext interrupt not used*/ \
	| IIFNZ(FALSE, /*1<<PSA*/ _PSA) /*;FALSE => pre-scalar assigned to timer 0, TRUE => WDT*/ \
	| IIFNZ(FALSE, /*1<<T0CS*/ _T0CS) /*FALSE: Timer 0 clock source = (FOSC/4), TRUE: T0CKI pin*/ \
	| ((NumBits8(Timer0_Prescalar) - 2) /*<< PS0*/ * _PS0) /*;prescalar value log2*/ \
)


#ifndef NUMSLOTS
 #define NUMSLOTS  256
#endif

//Timer 0 presets:
#define DIMSLICE  rdiv(1 sec, NUMSLOTS /*?? +6*/) //timeslice to use for 255 dimming levels at given rate
#define EVENT_OVERHEAD  2 //20 //approx #instr to flush display event and start next dimming timeslot; all prep overhead occurs before start of timeslot to reduce latency and jitter
#define TMR0_PRESET_DC50Hz  TimerPreset(rdiv(DIMSLICE, 50), EVENT_OVERHEAD, Timer0, CLOCK_FREQ) //should be ~ 86 usec
#define TMR0_PRESET_AC50Hz  TimerPreset(rdiv(DIMSLICE, 2 * 50), EVENT_OVERHEAD, Timer0, CLOCK_FREQ) //should be ~ 43 usec
#define TMR0_PRESET_AC60Hz  TimerPreset(rdiv(DIMSLICE, 2 * 60), EVENT_OVERHEAD, Timer0, CLOCK_FREQ) //should be ~ 32 usec
volatile BANK0 uint8_t Timer0_Preset;
//with 4:1 prescalar, Timer 0 interval is 0.5 usec @ 8 MIPS, preset for 30 usec tick ~= 60


#ifdef TIMER0_DEBUG //debug
 #ifndef debug
  #define debug() //define debug chain
 #endif
//define globals to shorten symbol names (local vars use function name as prefix):
    volatile AT_NONBANKED(0) uint8_t optreg_debug; //= MY_OPTIONS(CLOCK_FREQ / PLL);
    volatile AT_NONBANKED(0) uint16_t tmr0_dimslice_debug; //= DIMSLICE;
    volatile AT_NONBANKED(0) uint16_t tmr0_preset_50dc_debug; //= TMR0_PRESET_DC50Hz;
    volatile AT_NONBANKED(0) uint16_t tmr0_preset_50ac_debug; //= TMR0_PRESET_AC50Hz;
    volatile AT_NONBANKED(0) uint16_t tmr0_preset_60ac_debug; //= TMR0_PRESET_AC60Hz;
    volatile AT_NONBANKED(0) uint8_t tmr0_presbits_debug; //= NumBits8(Timer0_Prescalar);
    volatile AT_NONBANKED(0) uint8_t tmr0_prescalar_debug; //= Timer0_Prescalar;
    volatile AT_NONBANKED(0) uint16_t tmr0_limit_debug; //= Timer0_Limit;
    volatile AT_NONBANKED(0) uint16_t tmr0_ticks_test1_debug; //= TMR0_TICKS(30 usec);
    volatile AT_NONBANKED(0) uint16_t tmr0_ticks_test2_debug; //= TMR0_TICKS(100 usec);
 INLINE void tmr_dim_debug(void)
 {
    debug(); //incl prev debug first
    optreg_debug = MY_OPTIONS(CLOCK_FREQ / PLL);
    tmr0_dimslice_debug = DIMSLICE;
    tmr0_preset_50dc_debug = TMR0_PRESET_DC50Hz; //should be ~ 100 (156 * 0.5 usec * 50 Hz * 256 ~= 1 sec
    tmr0_preset_50ac_debug = TMR0_PRESET_AC50Hz; //should be ~ 178 (78 * 0.5 usec * 2*50 Hz * 256 ~= 1 sec
    tmr0_preset_60ac_debug = TMR0_PRESET_AC60Hz; //should be ~ 191 (65 * 0.5 usec * 2*60 Hz * 256 ~= 1 sec
    tmr0_presbits_debug = NumBits8(Timer0_Prescalar);
    tmr0_prescalar_debug = Timer0_Prescalar;
    tmr0_limit_debug = Timer0_Limit;
    tmr0_ticks_test1_debug = TMR0_TICKS(30 usec); //should be 60
    tmr0_ticks_test2_debug = TMR0_TICKS(100 usec); //should be 200
 }
 #undef debug
 #define debug()  tmr_dim_debug()
#endif


//#define TMR0_reset(...)  USE_ARG2(__VA_ARGS__, TMR0_reset_1ARG, TMR0_reset_0ARGS) (__VA_ARGS__)
//#define TMR0_reset_0ARGS(val)  TMR0_reset_1ARG(TRUE) //relative
//#define TMR0_reset_1ARG(relative)  
//restart current timer interval:
//NOTE: fractional interval will be lost
INLINE void TMR0_reset()
{
//	if (relative) TMR0 += Timer0_Preset; 
//    else
    TMR0 = Timer0_Preset;
}


//;initialize timer 0:
INLINE void init_tmr0(void)
{
	init(); //prev init first
    LABDCL(0x00);
    Timer0_Preset = TMR0_PRESET_DC50Hz; //assume 50 Hz DC until ZC detected
	OPTION_REG = MY_OPTIONS(CLOCK_FREQ / PLL); /*should be 0x00 for 1:2 prescalar or 0x01 for 1:4 prescalar (0x80/0x81 if no WPU)*/
//    intcon = MY_INTCON;
}
#undef init
#define init()  init_tmr0() //function chain in lieu of static init


#ifndef on_tmr_dim
 #define on_tmr_dim() //initialize function chain
#endif

//NOTE: need macros here so "return" will exit caller
#define on_tmr_dim_check()  if (!TMR0IF) return

INLINE void on_tmr_dim_tick(void)
{
	on_tmr_dim(); //prev event handlers first
    LABDCL(0x01);
//use += to compensate for overruns:
    TMR0 += Timer0_Preset; //TimerPreset(100 usec, 6, Timer0, CLOCK_FREQ); // / 0x100; /*BoostC sets LSB first, which might wrap while setting MSB; explicitly set LSB first here to avoid premature wrap*/
}
#undef on_tmr_dim
#define on_tmr_dim()  on_tmr_dim_tick() //event handler function chain


//#ifndef on_tmr_1sec
// #define on_tmr_1sec() //initialize function chain
//#endif

//set Timer 0 preset to match ZC rate:
//check ZC once every second
INLINE void on_tmr_1sec_dim_preset(void)
{
	on_tmr_1sec(); //prev event handlers first
//	ONPAGE(LEAST_PAGE); //keep demo code separate from protocol and I/O so they will fit within first code page with no page selects
    WREG = TMR0_PRESET_DC50Hz;
//CAUTION: zc_60hz overrides zc_present; check in reverse order
    if (zc_present) WREG = TMR0_PRESET_AC50Hz;
	if (zc_60hz) WREG = TMR0_PRESET_AC60Hz;
    Timer0_Preset = WREG;
}
#undef on_tmr_1sec
#define on_tmr_1sec()  on_tmr_1sec_dim_preset() //event handler function chain


#endif //ndef _TIMER0_H
//eof