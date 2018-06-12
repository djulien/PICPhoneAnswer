////////////////////////////////////////////////////////////////////////////////
////
/// Device config
//

#ifndef _CONFIG_H
#define _CONFIG_H

#include <stdint.h> //uint*_t
#include "compiler.h"
#include "clock.h" //sets UseIntOsc based on device type and target frequency

#if !defined(_CONFIG1) && defined(_CONFIG)
 #define _CONFIG1  _CONFIG
#endif


#ifdef _CONFIG1
 #define IF_CONFIG1(stmt)  stmt
#else
 #define IF_CONFIG1(stmt)  //noop
#endif

//some PICs don't have CONFIG2:
#ifdef _CONFIG2
 #define IF_CONFIG2(stmt)  stmt
#else
 #define IF_CONFIG2(stmt)  //noop
#endif

 
#if 0
//naming conventions:
#if !defined(_WDT_OFF) && defined(_WDTE_OFF)
 #define _WDT_OFF  _WDTE_OFF
#endif
#if !defined(_BORV_25) && defined(_BORV_HI)
 #define _BORV_25  _BORV_HI
#endif
#if !defined(_BOD_OFF) && defined(_BOREN_OFF)
 #define _BOD_OFF  _BOREN_OFF
#endif
#if !defined(_INTRC_OSC_NOCLKOUT) && defined(_CLKOUTEN_OFF)
 #define _INTRC_OSC_NOCLKOUT  _CLKOUTEN_OFF
#endif
#if !defined(_EC_OSC) && defined(_FOSC_ECH)
 #define _EC_OSC  _FOSC_ECH
#endif
#endif


//these settings have worked well for my software, but feel free to change them:
#define MY_CONFIG1  \
(0xFFFF \
	& _IESO_OFF  /*;internal/external switchover not needed; turn on to use optional external clock?  disabled when EC mode is on (page 31); TODO: turn on for battery-backup or RTC*/ \
	& _BOREN_OFF  /*;brown-out disabled; TODO: turn this on when battery-backup clock is implemented?*/ \
	& _CPD_OFF  /*;data memory (EEPROM) NOT protected; TODO: CPD on or off? (EEPROM cleared)*/ \
	& _CP_OFF  /*;program code memory NOT protected (maybe it should be?)*/ \
	& _MCLRE_OFF  /*;use MCLR pin as INPUT pin (required for AC line sync with Renard); no external reset needed anyway*/ \
	& _PWRTE_ON  /*;hold PIC in reset for 64 msec after power up until signals stabilize; seems like a good idea since MCLR is not used*/ \
	& _WDTE_OFF/*ON*/  /*;use WDT to restart if software crashes (paranoid); WDT has 8-bit pre- (shared) and 16-bit post-scalars (page 125)*/ \
	& IIF(UseIntOsc, _FOSC_INTOSC, _FOSC_ECH)  /*;I/O on RA4, CLKIN on RA5; external clock (18.432 MHz); if not present, int osc will be used*/ \
	& IIF(UseIntOsc, _FCMEN_OFF, _FCMEN_ON)  /*;turn on fail-safe clock monitor in case external clock is not connected or fails (page 33); RA5 will still be configured as clock input though (not available for I/O)*/ \
/*;disable fail-safe clock monitor; NOTE: this bit must explicitly be turned off since MY_CONFIG started with all bits ON*/ \
)

#if !UseIntOsc && defined(PIC16X)
 #error RED_MSG "don't need to use ext clock on PIC16X chips"
#endif


#ifdef PIC16X //extended
 #define MY_CONFIG2  \
 (0xFFFF \
	& _WRT_OFF  /*Write protection off*/ \
	& IIF(PLL != 1, _PLLEN_ON, _PLLEN_OFF)  /*4x PLL only needed for top speed*/ \
	& _STVREN_OFF  /*Stack Overflow or Underflow will NOT cause a Reset (circular)*/ \
	& _BOREN_OFF  /*Brown-out Reset Voltage (VBOR) disabled; TODO?*/ \
	& _LVP_OFF  /*High-voltage on MCLR/VPP must be used for programming*/ \
 )
//	& _BORV_19  /*Brown-out Reset Voltage (VBOR) set to 2.5 V*/ 


//should be 0x2984, 0x19FF
// #pragma DATA _CONFIG1, MY_CONFIG1
// #pragma DATA _CONFIG2, MY_CONFIG2
 static __code uint16_t __at (_CONFIG1) cfg1 = MY_CONFIG1;
 static __code uint16_t __at (_CONFIG2) cfg2 = MY_CONFIG2;
#else
// #pragma DATA _CONFIG, MY_CONFIG1
 static __code uint16_t __at (_CONFIG) cfg1 = MY_CONFIG1;
#endif


#ifdef CONFIG_DEBUG //debug
 #ifndef debug
  #define debug() //define debug chain
 #endif
//define globals to shorten symbol names (local vars use function name as prefix):
    volatile AT_NONBANKED(0) uint16_t cfg1_debug; //= MY_CONFIG1;
    IF_CONFIG2(volatile AT_NONBANKED(0) uint16_t cfg2_debug;) //= MY_CONFIG2;
 INLINE void config_debug(void)
 {
    debug(); //incl prev debug
    cfg1_debug = MY_CONFIG1;
    IF_CONFIG2(cfg2_debug = MY_CONFIG2);
 }
 #undef debug
 #define debug()  config_debug()
#endif


#endif //ndef _CONFIG_H
//eof