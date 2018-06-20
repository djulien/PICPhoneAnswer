////////////////////////////////////////////////////////////////////////////////
////
/// Timer1
//

#ifndef _TIMER1_H
#define _TIMER1_H

#include "compiler.h" //device registers
#include "helpers.h" //U16FIXUP(), RED_MSG, IIF(), NumBits(), TOSTR()
#include "clock.h" //Instr2uSec()


#ifndef Timer1_Range
 #define Timer1_Range  (50 msec)
#endif
//#define Timer1_halfRange  (Timer1_Range / 2) //kludge: BoostC gets /0 error with 50 msec (probably 16-bit arith overflow), so use a smaller value
//#define Timer1_8thRange  (Timer1_Range / 8) //kludge: BoostC gets /0 error with 50 msec (probably 16-bit arith overflow), so use a smaller value

//#define TMR1_TICKS(usec)  (0 - TimerPreset(usec, 0, Timer1, CLOCK_FREQ))
#define TMR1_TICKS(usec)  ((usec) / U16FIXUP(50 msec))
//#define Timer1_halfLimit  Instr2uSec(256 * 256 / 2 * Timer1_Prescalar, CLOCK_FREQ) //max duration for Timer 1; avoid arith overflow error in BoostC by dividing by 2
//#define Timer1_Limit  Instr2uSec(256 * 256 / 1000 * Timer1_Prescalar, CLOCK_FREQ / 1000) //max duration for Timer 1; avoid arith overflow error in BoostC by dividing by 2
//CAUTION: "256 * 256" doesn't work here but "65536" does
#define Timer1_Limit  Instr2uSec(0x10000 * Timer1_Prescalar, CLOCK_FREQ) //max duration for Timer 1; avoid arith overflow error in BoostC by dividing by 2
//#define Timer1_8thLimit  Instr2uSec(256 / 8 * 256 * Timer1_Prescalar, CLOCK_FREQ) //max duration for Timer 1; avoid arith overflow error in BoostC by dividing by 2


//kludge: use trial & error to select prescalar:
#define Timer1_Prescalar  1
#if Timer1_Limit < 1
 #warning RED_MSG "[ERROR] Timer1 limit arithmetic bad" TOSTR(Timer1_Limit)
#endif
//#if Timer1_8thLimit < 1
// #warning RED_MSG "[ERROR] Timer1 limit arithmetic bad" TOSTR(Timer1_8thLimit)
//#endif
//#warning BLUE_MSG "T1 limit @ "Timer1_Prescalar" ps = "Timer1_halfLimit""
//#if Timer1_halfLimit < 0
// #warning YELLOW_MSG "[WARNING] BoostC arithmetic overflow, trying work-around"
//#endif
#if Timer1_Limit < U16FIXUP(Timer1_Range)
 #undef Timer1_Prescalar
 #define Timer1_Prescalar  2
// #warning BLUE_MSG "T1 limit @ "Timer1_Prescalar" 2 ps = "Timer1_halfLimit""
 #if Timer1_Limit < U16FIXUP(Timer1_Range)
  #undef Timer1_Prescalar
  #define Timer1_Prescalar  4
//  #warning BLUE_MSG "T1 limit @ "Timer1_Prescalar" 4 ps = "Timer1_halfLimit""
  #if Timer1_Limit < U16FIXUP(Timer1_Range)
   #undef Timer1_Prescalar
   #define Timer1_Prescalar  8
//   #warning BLUE_MSG "T1 limit @ "Timer1_Prescalar" 8 ps = "Timer1_halfLimit""
   #if Timer1_Limit < U16FIXUP(Timer1_Range) //exceeded max prescalar here
    #error RED_MSG "[ERROR] Can't find a Timer1 prescalar to give " TOSTR(U16FIXUP(Timer1_Range)) " usec range; limit was " TOSTR(Timer1_Limit)""
   #endif
  #endif
 #endif
#endif

#if 0
#if Timer1_halfLimit == 256*256/2*8000/8000 //8 MIPS
 #define Timer1_limit_tostr  "65.536 msec" //"32.768 msec"
// #undef Timer1_halfLimit
// #define Timer1_halfLimit  32768 //kludge: avoid macro body problems or arithmetic errors in BoostC
#elif Timer1_halfLimit == 256*256/2*4000/5000 //5 MIPS
 #define Timer1_limit_tostr  "52.428 msec"
// #undef Timer1_halfLimit
// #define Timer1_halfLimit  52428 //kludge: avoid macro body problems or arithmetic errors in BoostC
#elif Timer1_halfLimit == 256*256/2*4000/4608 //4.6 MIPS
 #define Timer1_limit_tostr  "56.888 msec"
// #undef Timer1_halfLimit
// #define Timer1_halfLimit  65535 //kludge: BoostC treats 65536 as 0 *sometimes*
// #define Timer1_halfLimit  56888 //kludge: avoid macro body problems or arithmetic errors in BoostC
#elif Timer1_halfLimit < 1
 #error RED_MSG "[ERROR] Timer 1 limit arithmetic is broken"
#else
 #define Timer1_limit_tostr  Timer1_halfLimit "*2 msec"
#endif
//#endif
//#endif
//#endif
#endif

//#warning BLUE_MSG "[INFO] Timer 1 limit is 2*" TOSTR(Timer1_halfLimit) " with " TOSTR(Timer1_Prescalar) ":1 prescalar."

//volatile bit Timer1Wrap @adrsof(PIR1).TMR1IF; //timer 1 16-bit wrap-around
//volatile bit Timer1Enable @adrsof(T1CON).TMR1ON; //time 1 on/off
#define Timer1Wrap  TMR1IF //timer 1 16-bit wrap-around
#define Timer1Enable  TMR1ON //time 1 on/off

//associate timer regs with names:
#define Timer1_reg  TMR1L //tmr1L//_16
#define Timer1_ADDR  TMR1L_ADDR

//;T1CON (timer1 control) register (page 51, 53):
//;set this register once at startup only, except that Timer 1 is turned off/on briefly during preset (as recommended by Microchip)
#define MY_T1CON(clock)  \
(0 \
	| IIFNZ(FALSE, /*1<<TMR1GE*/ _TMR1GE) /*;timer 1 gate-enable off (timer 1 always on)*/ \
	| IIFNZ(FALSE, /*1<<T1OSCEN*/ _T1OSCEN) /*;LP osc disabled (timer 1 source = ext osc)*/ \
	| IIFNZ(DONT_CARE, /*1<<NOT_T1SYNC*/ _NOT_T1SYNC) /*;no sync with ext clock needed*/ \
	| /*(0<<TMR1CS0)*/ IIFNZ(FALSE, _TMR1CS0) /*;use system clock (config) always*/ \
	| /*(1<<TMR1ON)*/ _TMR1ON /*;timer 1 on*/ \
	| ((NumBits8(Timer1_Prescalar) - 1) /*<< T1CKPS0*/ * _T1CKPS0) /*;prescalar on timer 1 (page 53); <T1CKPS1, T1CKPS0> form a 2-bit binary value*/ \
)

//#warning TOSTR(TimerPreset(50 msec / 2, 8, Timer1, CLOCK_FREQ))
//#define TMR1_PRESET_50msec  (2 * TimerPreset(50 msec / 2, 8, Timer1, CLOCK_FREQ))
#define TMR1_PRESET_50msec  TimerPreset(U16FIXUP(50 msec), 8, Timer1, CLOCK_FREQ)
#warning BLUE_MSG "timer1 preset" TOSTR(TMR1_PRESET_50msec)
//with 8:1 prescalar, Timer 1 interval is 1 usec @ 8 MIPS, preset for 50 msec tick == 50,000 == 0x3cb0


#ifdef TIMER1_DEBUG //debug
 #ifndef debug
  #define debug() //define debug chain
 #endif
//define globals to shorten symbol names (local vars use function name as prefix):
    volatile AT_NONBANKED(0) uint8_t t1con_debug; //= MY_T1CON(CLOCK_FREQ / PLL);
    volatile AT_NONBANKED(0) uint16_t tmr1_preset_debug; //= TMR1_PRESET_50msec; //TimerPreset(50 msec / 2, 8, Timer1, CLOCK_FREQ); // / 0x100; /*BoostC sets LSB first, which might wrap while setting MSB; explicitly set LSB first here to avoid premature wrap*/
    volatile AT_NONBANKED(0) uint16_t tmr1_50msec_debug; //= 50 msec;
    volatile AT_NONBANKED(0) uint8_t tmr1_presbits_debug; //= NumBits8(Timer1_Prescalar);
    volatile AT_NONBANKED(0) uint8_t tmr1_prescalar_debug; //= Timer1_Prescalar;
    volatile AT_NONBANKED(0) uint32_t tmr1_limit1_debug; //= Timer1_Limit;
    volatile AT_NONBANKED(0) uint32_t tmr1_limit2_debug; //= Timer1_Limit;
    volatile AT_NONBANKED(0) uint32_t tmr1_limit3_debug; //= Timer1_Limit;
    volatile AT_NONBANKED(0) uint32_t tmr1_limit4_debug; //= Timer1_Limit;
//    volatile AT_NONBANKED(0) uint32_t tmr1_8thlimit_debug; //= Timer1_halfLimit;
    volatile AT_NONBANKED(0) uint32_t tmr1_ticks_test1_debug; //= TMR1_TICKS(ONE_SEC);
    volatile AT_NONBANKED(0) uint32_t tmr1_ticks_test2_debug; //= TMR1_TICKS(1000 msec / 60);
 INLINE void tmr_50msec_debug(void)
 {
    debug(); //incl prev debug first
//tmr1_ticks_test2_debug = Instr2uSec(8 * Timer1_Prescalar, CLOCK_FREQ); //max duration for Timer 1; avoid arith overflow error in BoostC by dividing by 2
    t1con_debug = MY_T1CON(CLOCK_FREQ / PLL); //should be 0x31 with 8:1 prescalar and timer enabled, 0x01 with 1:1 prescalar
    tmr1_preset_debug = U16FIXUP(TMR1_PRESET_50msec); //should be ~ 65536 - 50000 == 0x3cb0 with 8:1 pre @ 8 MIPS
    tmr1_50msec_debug = 50 msec; //50,000 == 0xc350
    tmr1_presbits_debug = NumBits8(Timer1_Prescalar); //should be 4 (for prescalar 8)
    tmr1_prescalar_debug = Timer1_Prescalar; //should be 8 @ 8 MIPS, makes T1 tick == 1 usec for easier timing arithmetic
    tmr1_limit1_debug = U16FIXUP(Timer1_Range); //50,000 == 0xc350
    tmr1_limit2_debug = Timer1_Limit; //65.5 msec @ 8 MIPS with prescalar 8; should be 0x10000
    tmr1_limit3_debug = Instr2uSec(256UL * 256 * Timer1_Prescalar, CLOCK_FREQ); //CAUTION: need "UL" for sign extends; max duration for Timer 1; avoid arith overflow error in BoostC by dividing by 2
    tmr1_limit4_debug = Instr2uSec(0x10000 * Timer1_Prescalar, CLOCK_FREQ); //max duration for Timer 1; avoid arith overflow error in BoostC by dividing by 2
//    tmr1_limit_debug = Instr2uSec(65536 * 8, CLOCK_FREQ); //max duration for Timer 1; avoid arith overflow error in BoostC by dividing by 2
//    tmr1_8thlimit_debug = Timer1_8thLimit;
    tmr1_ticks_test1_debug = TMR1_TICKS(ONE_SEC);  //should be 20 == 0x14
    tmr1_ticks_test2_debug = TMR1_TICKS(280 msec); //should be 5 or 6 (5 * 50 msec < 280 msec < 6 * 50 msec)
 }
 #undef debug
 #define debug()  tmr_50msec_debug()
#endif


#ifndef init
 #define init() //initialize function chain
#endif

//;initialize timer 1:
INLINE void init_tmr1(void)
{
	init(); //prev init first
    LABDCL(0x10);
//    t1con = MY_T1CON;
    TMR1_16 = U16FIXUP(TMR1_PRESET_50msec); //avoid overflow; / 0x100; /*BoostC sets LSB first, which might wrap while setting MSB; explicitly set LSB first here to avoid premature wrap*/
//    TMR1L = TimerPreset(50 msec / 2, 8, Timer1, CLOCK_FREQ) % 0x100; // / 0x100; /*BoostC sets LSB first, which might wrap while setting MSB; explicitly set LSB first here to avoid premature wrap*/
//    TMR1H = TimerPreset(50 msec / 2, 8, Timer1, CLOCK_FREQ) / 0x100; /*BoostC sets LSB first, which might wrap while setting MSB; explicitly set LSB first here to avoid premature wrap*/
	T1CON = MY_T1CON(CLOCK_FREQ / PLL); //configure + turn on Timer 1; should be 0x21 for 1:4 prescalar
//    loop_1sec = TMR1_LOOP_1sec;
}
#undef init
#define init()  init_tmr1() //function chain in lieu of static init


#ifndef on_tmr_50msec
 #define on_tmr_50msec() //initialize function chain
#endif

//NOTE: need macros here so "return" will exit caller
#define on_tmr_50msec_check()  if (!TMR1IF) return

INLINE void on_tmr_50msec_tick(void)
{
	on_tmr_50msec(); //prev event handlers first
    LABDCL(0x10);
	/*t1con.*/ TMR1ON = FALSE; /*for cumulative intervals, can't use Microchip workaround and must set low byte first, but then need a temp for _WREG variant; just disable timer during update for simplicity*/
//	WREG = TimerPreset(duration, IIF(time_base, 8, 6), which, CLOCK_FREQ) / 0x100; /*BoostC sets LSB first, which might wrap while setting MSB; explicitly set LSB first here to avoid premature wrap*/
//	if (time_base) tmr1H /*op_##time_base*/ += WREG; else tmr1H = WREG;
//SDCC uses temps here, so use explicit opcodes:
//    TMR1_16 += TMR1_PRESET_50msec; // / 0x100; /*BoostC sets LSB first, which might wrap while setting MSB; explicitly set LSB first here to avoid premature wrap*/
//    TMR1L += TMR1_PRESET_50msec % 0x100;
    WREG = TMR1_PRESET_50msec % 0x100; addwf(ASMREG(TMR1L)); //should be ~ 65536 - 50000 == 0x3cb0 with 8:1 pre @ 8 MIPS
    WREG = U16FIXUP(TMR1_PRESET_50msec) / 0x100; addwfc(ASMREG(TMR1H));
//    TMR1L += TimerPreset(50 msec / 2, 8, Timer1, CLOCK_FREQ) % 0x100; // / 0x100; /*BoostC sets LSB first, which might wrap while setting MSB; explicitly set LSB first here to avoid premature wrap*/
//    if (CARRY) ++TMR1H;
//    TMR1H += TimerPreset(50 msec / 2, 8, Timer1, CLOCK_FREQ) / 0x100; /*BoostC sets LSB first, which might wrap while setting MSB; explicitly set LSB first here to avoid premature wrap*/
    TMR1IF = FALSE; //NOTE: data sheets say this must be cleared in software
	/*T1CON.*/ TMR1ON = TRUE; /*for cumulative intervals, can't use Microchip workaround and must set low byte first, but then need a temp for _WREG variant; just disable timer during update for simplicity*/
}
#undef on_tmr_50msec
#define on_tmr_50msec()  on_tmr_50msec_tick() //event handler function chain


#endif //ndef _TIMER1_H
//eof