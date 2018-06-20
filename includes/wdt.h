////////////////////////////////////////////////////////////////////////////////
////
/// Watchdog timer
//

#ifndef _WDT_H
#define _WDT_H

//WDT config:
//;WDT time base is 31.25 KHz LFINTOSC.  This does not depend on the HF int osc or ext clock, so it is independent of any instruction timing.
//;Since there will be a RTC tick every ~ 20 msec, WDT interval should be > 20 msec.
//If prescalar is assigned to WDT, it is set to 1:1 so that the Timer 0 prescalar calculations do not affect WDT calculations.
//;With no prescalar, possible WDT postscalar values are 1:32 (1 msec), 1:64, 1:128, 1:256, 1:512 (16 msec), 1:1024, 1:2048, 1:4096, 1:8192, 1:16384, 1:32768, or 1:65536 (~ 2 sec).
//#define WDT_Postscalar  1024  //;the smallest WDT internval > 20 msec
//#define WDT_Postscalar(range, clock)  (4096/IIF(MY_OPTIONS(range, clock) & (1<<PSA), 2, 1))  //;if nothing has happened within 1/10 sec, something's broken (must be > 20 msec)
//#define WDT_Postscalar(range, clock)  (4096/(IIF0(!NeedsPrescalar(range, clock), 1, 0) + 1))  //;if nothing has happened within 1/10 sec, something's broken (must be > 20 msec)
//#define WDT_halfRange  MAX(Timer0_Limit/2, Timer1_halfLimit) //min interval wanted for WDT (usec)
#define WDT_Range  MAX(Timer0_Limit, Timer1_Limit) //min interval wanted for WDT (usec)
//#define WDT_Postscalar(range, clock)  (1<<WDT_PostscalarSize(range, clock))
#define WDT_MinPostscalarSize  5 //smallest configurable WDT postscalar bit size; smallest postscalar is 1:32
#define WDT_MaxPostscalarSize  16 //largest configurable WDT postscalar bit size; largest postscalar is 1:65536
//#define WDT_Postscalar  NumBits8(WDT_Range / LFINTOSC_FREQ) //;WDT postscalar; divide by LFINTOSC_FREQ has already taken into account WDT_MinPostscalar == 5
//#define WDT_Postscalar  (NumBits8(divup(WDT_Range, 32 * ONE_MSEC)) + 5) //;WDT postscalar; divide by LFINTOSC_FREQ has already taken into account WDT_MinPostscalar == 5
//#define WDT_Postscalar  NumBits16(divup(WDT_Range, ONE_MSEC)) //;WDT postscalar; divide by 1 msec has already taken into account WDT_MinPostscalar == 5
//#define WDT_Postscalar  NumBits16((WDT_halfRange >> (WDT_MinPostscalarSize-1)) * 11/10 / (ONE_SEC/LFINTOSC_FREQ)) //;WDT postscalar; set target period 10% larger than desired interval to avoid WDT triggering
#define WDT_Postscalar  NumBits16((WDT_Range >> WDT_MinPostscalarSize) * 11/10 / (ONE_SEC/LFINTOSC_FREQ)) //;WDT postscalar; set target period 10% larger than desired interval to avoid WDT triggering
//#define WDT_Postscalar  NumBits8(WDT_Range / (ONE_SEC >> WDT_MinPostscalarSize)) //;WDT postscalar
//#define IntOsc_Prescalar(clock)  NumBits8(InstrPerSec(clock) / (LFINTOSC_FREQ >> IntOsc_MinPrescalarSize)) //;Int osc prescalar
#undef WDT_Postscalar //TODO: macro body overflow
#define WDT_Postscalar  7 //128 msec

#if (WDT_Postscalar < 0) || (WDT_Postscalar > WDT_MaxPostscalarSize - WDT_MinPostscalarSize)
 #error RED_MSG "WDT postscalar out of range " TOSTR(WDT_MinPostscalarSize) ".." TOSTR(WDT_MaxPostscalarSize) ""
#endif


//;WDTCON (watchdog control) register (page 126):
//;set this register once at startup only
//NOTE: if Timer0/WDT prescalar is assigned to WDT, it's set to 1:1 so the calculations below are independent of Timer 0.
//If WDT thread runs 1x/sec, WDT interval should be set to 2 sec.
#define MY_WDTCON  \
(0  \
	| (WDT_Postscalar /*<< WDTPS0*/ * _WDTPS0) /*;<WDTPS3, WDTPS2, WDTPS1, WDTPS0> form a 4-bit binary value:; 1:64 .. 1:64K postscalar on WDT (page 126)*/ \
	| IIFNZ(DONT_CARE, /*1<<SWDTEN*/ _SWDTEN) /*;WDT is enabled via CONFIG; wdtcon has no effect here unless off in CONFIG*/ \
)
//IFWDT(non_volatile uint8 FixedAdrs(initialized_wdtcon, WDTCON) = MY_DTCON); //;??NOTE: this seems to turn on WDT even if it shouldn't be on
#warning CYAN_MSG "TODO: WDT, BOR"

#if UseIntOsc //don't need to check if ext clock failed
 #define ExtClockFailed  FALSE
#else
// volatile bit ExtClockFailed @adrsof(PIR1).OSFIF; //ext clock failed; PIC is running on int osc
 #define ExtClockFailed  OSFIF //ext clock failed; PIC is running on int osc
#endif


#ifdef WDT_DEBUG //debug
 #ifndef debug
  #define debug() //define debug chain
 #endif
//define globals to shorten symbol names (local vars use function name as prefix):
    volatile AT_NONBANKED(0) uint8_t my_wdtcon_debug; //= MY_WDTCON;
    volatile AT_NONBANKED(0) uint8_t wdt_postcalar_debug; //= WDT_Postscalar;
    volatile AT_NONBANKED(0) uint16_t wdt_limit_debug; //= WDT_Range;
 INLINE void wdt_debug(void)
 {
    debug(); //incl prev debug first
    my_wdtcon_debug = MY_WDTCON;
    wdt_postcalar_debug = WDT_Postscalar;
    wdt_limit_debug = WDT_Range;
 }
 #undef debug
 #define debug()  wdt_debug()
#endif


#ifndef init
 #define init() //initialize function chain
#endif

//initialize timing-related regs:
//NOTE: power-up default speed for PIC16F688 is 1 MIPS (4 MHz)
INLINE void wdt_init(void)
{
	init(); //prev init first
    LABDCL(0x30);
	WDTCON = MY_WDTCON; //config and turn on WDT ;??NOTE: this seems to turn on WDT even if it shouldn't be on??; should be 0x0a for 32 msec, 0x0c for 64 msec, 0x0e for 128 msec, 0x16 for 2 sec interval
}
#undef init
#define init()  init_wdt() //function chain in lieu of static init


#endif //ndef _WDT_H
//eof
