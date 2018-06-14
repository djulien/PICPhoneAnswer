//;==================================================================================================
//;Compiler-specific headers and other definitions.
//This section attempts to establish a common compile-time environment so that different compilers can be used.
//The BoostC convention of lower case variable names and upper case addresses is used.
//GCC is used for basic C syntax checking and generic code debug.
//SDCC had missing features or generated incorrect code.
//BoostC was used for initial code development/debug; it was buggy but has a nice IDE, allows longer macro bodies, and supports inline functions and reference params.
//  However, constructs such as jumptables are painful, and there is no precise control of page or bank allocation.
//CC5X was used for final code development/debug; it gives precise control of page and bank allocation and has good support
//  for asm-like instructions directly in C.  However, user-defined inline functions are not supported, so awk is used to
//  "fix up" of the generated asm code to give the same effect (as well as other optimizations).
//;==================================================================================================

//kludge: alloc gcc preprocessor to be used with other compilers
#if defined(_GCC_) && defined(_NOGCC_)
 #undef _GCC_
#endif


//data types:
typedef unsigned char uint8;
typedef unsigned short uint16;
#ifdef _BOOSTC
 typedef struct { uint8 bytes[3]; } uint24; //24-bit not supported by BoostC, arrays within typedef not supported either
 //BoostC doesn't handle 24-bit values, so set one byte at a time:
 #define IsNonZero24(reg24)  (reg24.bytes[0] || reg24.bytes[1] || reg24.bytes[2])
 inline void inc24(uint24& reg24)
 {
	if (!++reg24.bytes[0])
		if (!++reg24.bytes[1])
			++reg24.bytes[2];
 }
 inline void zero24(uint24& reg24)
 {
	reg24.bytes[0] = 0;
	reg24.bytes[1] = 0;
	reg24.bytes[2] = 0;
 }
#else //__CC5X__
 typedef unsigned long uint24:24;
#endif
typedef unsigned long uint32;

//alternate types to allow byte-by-byte access:
//used for compilers that do not support the appropriate base type (ie, BoostC can't handle 24-bit values as operand or function parameters)
typedef union
{
	uint8 as8;
	uint8 bytes[1];
} uint1x8;
typedef union
{
	uint16 as16;
	uint8 bytes[2];
} uint2x8;
typedef union
{
	uint24 as24; //this data type not supported by BoostC
	uint8 bytes[3];
} uint3x8;
typedef union
{
	uint32 as32;
	uint8 bytes[4];
} uint4x8;
typedef union
{
//	uint32 as32;
	uint8 bytes[8];
} uint8x8;

//pointers:
//explicitly define size
#ifdef __CC5X__
 typedef size1 uint8* ptr8;
 typedef size2 uint8* ptr16;
#else
 typedef uint8 ptr8;
 typedef uint8* ptr16;
#endif


//inline function chain to prevent compiler from removing unreferenced code:
#define keep_unused()
//inline function chain for initialization:
#define init()


//device IDs for supported processors (arbitrary values):
//this allows sender to query to find out what's out there
//#define PIC12F675_ID  0x26
//#define PIC12F1840_ID  0x28
//#define PIC16F688_ID  0x68
//#define PIC16F1823_ID  0x83
//#define PIC16F1824_ID  0x84
//#define PIC16F1825_ID  0x85
//#define PIC16F1826_ID  0x86
//#define PIC16F1827_ID  0x87
//#define PIC16F1828_ID  0x88
//#define PIC16F1829_ID  0x89
//#define PIC16F1847_ID  0xC7
//#define PIC18F2550_ID  0xF5


//make preprocessor symbols more consistent across compilers:
//MPASM, BoostC, CC5X, etc all use different names, so define generic symbols for consistency
//NOTE: don't mix "#ifdef" and "#if defined" in BoostC; doesn't work
//#define PIC12F675_key  0x12F675
//#define PIC12F1840_key  0x12F1840
//#define PIC16F688_key  0x16F688
//#define PIC16F1823_key  0x16F1823
//#define PIC16F1824_key  0x16F1824
//#define PIC16F1825_key  0x16F1825
//#define PIC16F1826_key  0x16F1826
//#define PIC16F1827_key  0x16F1827
//#define PIC16F1828_key  0x16F1828
//#define PIC16F1829_key  0x16F1829
//#define PIC16F1847_key  0x16F1847
//#define PIC18F2550_key  0x18F2550

//device:
//BROKEN:
//#define PIC12F675  defined(_PIC12F675) || defined(_12F675) || defined(__12F675)
#define PIC12F1840  0x12F1840
#define PIC16F688  0x16F688 //defined(_PIC16F688) || defined(_16F688) || defined(__16F688)
//#define PIC16F1823  defined(_PIC16F1823) || defined(_16F1823) || defined(__16F1823)
#define PIC16F1825  0x16F1825
#define PIC16F1827  0x16F1827 //defined(_PIC16F1827) || defined(_16F1827) || defined(__16F1827)
//#define PIC16F1847  defined(_PIC16F1847) || defined(_16F1847) || defined(__16F1847)

//#if defined(_PIC12F675) || defined(_12F675) || defined(__12F675)
//// #define PIC12F675
// #define DEVICE  PIC12F675
// #define TOTAL_RAM  64
// #define DEVICE_CODE  0x26 //app-defined designator (arbitrary)
//#endif

#define K  *1024

#if defined(_PIC12F1840) || defined(_12F1840) || defined(__12F1840)
// #define PIC12F1840
 #define SERIN_PIN  0xA1A5 //serial input pin; default = 0xA1; moved to A5
 #define SEROUT_PIN  0xA0A4 //serial output pin; default = 0xA0; moved to A4
 #define CLKIN_PIN  0xA5 //;ext clock pin; might be available for LED control
 #define ZC_PIN  0xA3  //;this pin can only be used for input; used for ZC/config
//no status LEDs

 #define DEVICE  PIC12F1840
 #define TOTAL_RAM  256
 #define TOTAL_ROM  (4K)
 #define EEPROM_ORG  0xF000
 #define NODEIO_PINS  (8-2-2-1) //3
 #define DEVICE_CODE  0x28 //app-defined designator (arbitrary)
 #ifndef _PIC16X //BoostC #include out of date?
  #define _PIC16X  //kludge: this device does support extended instruction set
 #endif
#endif

#if defined(_PIC16F688) || defined(_16F688) || defined(__16F688)
// #define PIC16F688
 #define SERIN_PIN  0xC5 //;serial input pin
 #define SEROUT_PIN  0xC4 //;serial output pin
 #define CLKIN_PIN  0xA5 //;ext clock pin; might be available for LED control
 #define ZC_PIN  0xA3  //;this pin can only be used for input; used for ZC/config
 #define NEEDS_CMCON0  ~0 //;configure comparator inputs as digital I/O (no comparators); overrides TRISC (page 63, 44, 122); must be OFF for digital I/O to work!
//no status LEDs

 #define DEVICE  PIC16F688
 #define TOTAL_RAM  256
 #define TOTAL_ROM  (4K)
 #define EEPROM_ORG  0x2100
 #define NODEIO_PINS  (14-2-2-1-1) //8, or 9 with no ext clock
 #define DEVICE_CODE  0x68 //app-defined designator (arbitrary)
#endif

//#if defined(_PIC16F1823) || defined(_16F1823) || defined(__16F1823)
//// #define PIC16F1823
// #define SERIN_PIN  0xC5 //;serial input pin
// #define SEROUT_PIN  0xC4 //;serial output pin
// #define CLKIN_PIN  0xA5 //;ext clock pin; might be available for LED control
// #define ZC_PIN  0xA3  //;this pin can only be used for input; used for ZC/config
//
// #define DEVICE  PIC16F1823
// #define TOTAL_RAM  128
// #define EEPROM_ORG  0xF000
// #define NODEIO_PINS  8
// #define DEVICE_CODE  0x83 //app-defined designator (arbitrary)
// #ifndef _PIC16X //BoostC #include out of date?
//  #define _PIC16X  //kludge: this device does support extended instruction set
// #endif
//#endif

//#if defined(_PIC16F1824) || defined(_16F1824) || defined(__16F1824)
//// #define PIC16F1824
// #define SERIN_PIN  0xC5 //;serial input pin
// #define SEROUT_PIN  0xC4 //;serial output pin
// #define CLKIN_PIN  0xA5 //;ext clock pin; might be available for LED control
// #define ZC_PIN  0xA3  //;this pin can only be used for input; used for ZC/config
//
// #define DEVICE  PIC16F1824
// #define TOTAL_RAM  256
// #define EEPROM_ORG  0xF000
// #define NODEIO_PINS  8
// #define DEVICE_CODE  0x84 //app-defined designator (arbitrary)
// #ifndef _PIC16X //BoostC #include out of date?
//  #define _PIC16X  //kludge: this device does support extended instruction set
// #endif
//#endif

#if defined(_PIC16F1825) || defined(_16F1825) || defined(__16F1825)
// #define PIC16F1825
 #define SERIN_PIN  0xC5C5 //;serial input pin
 #define SEROUT_PIN  0xC4C4 //;serial output pin
 #define CLKIN_PIN  0xA5 //;ext clock pin; might be available for LED control
 #define ZC_PIN  0xA3  //;this pin can only be used for input; used for ZC/config
 #define fpbit_RXIO  0xA5 //RX LED on PX1
 #define fpbit_NODEIO  0 //TODO
 #define fpbit_COMMERR  0xA1 //"FE" LED on PX1
 #define fpbit_OTHER  0xA0 //Attn LED on PX1
 #define portFP  portX
 #define FP_on  0 //PX1 and Pex-Ren1827 status LEDs are connected directly to VCC, so status bits are inverted (pull to ground to turn on)

 #define DEVICE  PIC16F1825
 #define TOTAL_RAM  (1K) //1024
 #define TOTAL_ROM  (8K)
 #define EEPROM_ORG  0xF000
 #define NODEIO_PINS  (14-2-2-1) //9, but only 8 currently supported
 #define DEVICE_CODE  0x85 //app-defined designator (arbitrary)
 #ifndef _PIC16X //BoostC #include out of date?
  #define _PIC16X  //kludge: this device does support extended instruction set
 #endif
#endif

//#if defined(_PIC16F1826) || defined(_16F1826) || defined(__16F1826)
//// #define PIC16F1826
// #define SERIN_PIN  0xC5 //;serial input pin
// #define SEROUT_PIN  0xC4 //;serial output pin
// #define CLKIN_PIN  0xA5 //;ext clock pin; might be available for LED control
// #define ZC_PIN  0xA3  //;this pin can only be used for input; used for ZC/config
//
// #define DEVICE  PIC16F1826
// #define TOTAL_RAM  256
// #define EEPROM_ORG  0xF000
// #define NODEIO_PINS  8
// #define DEVICE_CODE  0x86 //app-defined designator (arbitrary)
// #ifndef _PIC16X //BoostC #include out of date?
//  #define _PIC16X  //kludge: this device does support extended instruction set
// #endif
//#endif

#if defined(_PIC16F1827) || defined(_16F1827) || defined(__16F1827)
// #define PIC16F1827
 #define SERIN_PIN  0xB1B1 //0xB1B2 //serial input pin; default = 0xB1; moved to A5
 #define SEROUT_PIN  0xB2B2 //0xB2B5 //serial output pin; default = 0xB2; moved to B5
 #define CLKIN_PIN  0xA7 //;ext clock pin; might be available for LED control
 #define ZC_PIN  0xA5  //;this pin can only be used for input; used for ZC/config
 #define fpbit_RXIO  0xB7
 #define fpbit_NODEIO  0xB6
 #define fpbit_COMMERR  0 //TODO
 #define fpbit_OTHER  0 //TODO
 #define portFP  portbc
 #define FP_on  0 //PX1 and Pex-Ren1827 status LEDs are connected directly to VCC, so status bits are inverted (pull to ground to turn on)

 #define DEVICE  PIC16F1827
 #define TOTAL_RAM  384
 #define TOTAL_ROM  (4K)
 #define EEPROM_ORG  0xF000
 #define NODEIO_PINS  (18-2-1) //15, but only 8 currently supported
 #define DEVICE_CODE  0x87 //app-defined designator (arbitrary)
 #ifndef _PIC16X //BoostC #include out of date?
  #define _PIC16X  //kludge: this device does support extended instruction set
 #endif
#endif

//#if defined(_PIC16F1828) || defined(_16F1828) || defined(__16F1828)
//// #define PIC16F1828
// #define DEVICE  PIC16F1828
// #define TOTAL_RAM  256
// #define EEPROM_ORG  0xF000
// #define NODEIO_PINS  8
// #define DEVICE_CODE  0x88 //app-defined designator (arbitrary)
//#endif

//#if defined(_PIC16F1829) || defined(_16F1829) || defined(__16F1829)
//// #define PIC16F1829
// #define DEVICE  PIC16F1829
// #define TOTAL_RAM  1024
// #define EEPROM_ORG  0xF000
// #define NODEIO_PINS  8
// #define DEVICE_CODE  0x89 //app-defined designator (arbitrary)
//#endif

//#if defined(_PIC16F1847) || defined(_16F1847) || defined(__16F1847)
//// #define PIC16F1847
// #define DEVICE  PIC16F1847
// #define TOTAL_RAM  1024
// #define EEPROM_ORG  0xF000
// #define NODEIO_PINS  8
// #define DEVICE_CODE  0xC7 //app-defined designator (arbitrary)
//#endif

//#if defined(_PIC18F2550) || defined(_18F2550) || defined(__18F2550)
// #define PIC18F2550
// #define DEVICE  PIC18F2550
// #define TOTAL_RAM  1024
// #define DEVICE_CODE  0xF5 //app-defined designator (arbitrary)
//#endif

#ifndef DEVICE
 #error "[ERROR] Unsupported processor; add more definitions here."
#endif

//device class:
#if defined(_PIC16) || defined(_16CXX)
 #define PIC16
#endif
#if defined(_PIC16X) //extended instruction set, faster clock
 #define PIC16X
#endif
#if defined(_PIC18)
 #define PIC18
#endif


//make reg names and address symbols consistent:
//use "NAME_ADDR" for address consts and "name" for reg vars
//also define inline functions for special device opcodes
#ifdef __CC5X__
 #warning "[INFO] Using CC5X compiler; inline fixup script must be applied to generated asm code"
// #include <system.h> //do not use this for libraries

 #define WREG  W //make text more readable/searchable

//use SDCC naming conventions for register addresses:
// #define INDF_ADDR  0
// #define TMR0_ADDR  0x15
// #define STATUS_ADDR  3
// #define FSR0_ADDR  4
 #define PORTA_ADDR  &PORTA //0x0C
 #define PORTB_ADDR  &PORTB //0x0D
// #define SPBRGH_ADDR  SPBRGH
// #define SPBRG_ADDR  SPBRG
// #define CMCON0_ADDR  CMCON0
// #define OPTION_REG_ADDR  OPTION_REG
 #define TRISA_ADDR  &TRISA //0x8C
 #define TRISB_ADDR  &TRISB //0x8D
 #define ANSELA_ADDR  &ANSELA //0x18C
 #define ANSELB_ADDR  &ANSELB //0x18D

//use BoostC naming conventions for registers:
// uint8 indf @INDF;
 uint8 indf0 @INDF0;
 uint8 indf1 @INDF1;
 volatile uint8 tmr0 @TMR0;
 volatile uint8 pcl @PCL;
 volatile uint8 status @STATUS;
// uint8 fsr @FSR;
 uint8 fsr0l @FSR0L;
 uint8 fsr1l @FSR1L;
 uint8 fsr0h @FSR0H;
 uint8 fsr1h @FSR1H;
 volatile uint8 porta @PORTA;
 volatile uint8 portb @PORTB;
 uint8 pclath @PCLATH;
 uint8 intcon @INTCON;
 uint8 pir1 @PIR1;
 volatile uint8 tmr1l @TMR1L;
 volatile uint8 tmr1h @TMR1H;
 uint8 t1con @T1CON;
 uint8 baudctl @BAUDCON; //@BAUDCTL;
 #define baudcon  baudctl
 uint8 spbrgh @SPBRGH;
 uint8 spbrgl @SPBRGL; //@SPBRG;
 volatile uint8 rcreg @RCREG;
 uint8 txreg @TXREG;
 volatile uint8 txsta @TXSTA;
 volatile uint8 rcsta @RCSTA;
 uint8 wdtcon @WDTCON;
 uint8 cmcon0 @CM1CON0; //@CMCON0;
 uint8 cmcon1 @CM1CON1; //@CMCON1;
 uint8 adcon0 @ADCON0;
 uint8 option_reg @OPTION_REG;
 uint8 trisa @TRISA;
 uint8 trisb @TRISB;
 volatile uint8 pcon @PCON;
 uint8 osccon @OSCCON;
 volatile uint8 oscstat @OSCCONH; //OSCSTAT;
 uint8 ansela @ANSELA;
 uint8 anselb @ANSELB;
 uint8 wpua @WPUA;
 uint8 eedath @EEDATH;
 uint8 eeadrh @EEADRH;
 volatile uint8 eedatl @EEDATL;
 volatile uint8 eeadrl @EEADRL;
 uint8 eecon1 @EECON1;
 uint8 eecon2 @EECON2;

// #define C  0 //status.C
// #define Z  2 //status.Z
// #define RP0  5 //status.RP0
// #define RP1  6 //status.RP1
// #define IRP  7 //status.IRP

//use Microchip naming conventions for reg bits:
//NOTE: this overrides bit vars already defined, so reg name needs to be given when used
//INTCON:
 #define TMR0IF  2
 #define T0IF  TMR0IF
//PIR1:
 #define TMR1IF  0
 #define TXIF  4
 #define RCIF  5
//PIR2:
 #define EEIF  4
 #define OSFIF  7
//T1CON:
 #define TMR1ON  0
 #define /*T1SYNC_*/NOT_T1SYNC  2
 #define T1OSCEN  3
 #define T1CKPS0  4
 #define T1CKPS1  5
 #define TMR1CS0  6
 #define TMR1CS1  7
 #define TMR1GE  6
 #warning "check this ^"
//OPTION_REG:
 #define PS0  0
 #define PS1  1
 #define PS2  2
 #define PSA  3
 #define /*TMR0SE*/T0SE  4
 #define /*TMR0CS*/T0CS  5
 #define INTEDG  6
 #define /*WPUEN_*/NOT_WPUEN  7
//PCON:
 #define BOR_  0
 #define POR_  1
//WDTCON:
 #define SWDTEN  0
//OSCCON:
 #define SCS0  0
 #define SCS1  1
 #define IRCF0  3
 #define SPLLEN  7
//OSCSTAT:
 #define HFIOFS  0
//LFIOFR           EQU  H'0001'
//MFIOFR           EQU  H'0002'
//HFIOFL           EQU  H'0003'
//HFIOFR           EQU  H'0004'
//OSTS             EQU  H'0005'
 #define PLLR  6
//T1OSCR           EQU  H'0007'
//EECON1:
 #define RD  0
 #define WR  1
 #define WREN  2
 #define WRERR  3
 #define EEPGD  7
//RCSTA:
 #define RX9D  0
 #define OERR  1
 #define FERR  2
 #define ADDEN  3
 #define CREN  4
 #define SREN  5
 #define RX9  6
 #define SPEN  7
//TXSTA:
 #define TX9D  0
 #define TRMT  1
 #define BRGH  2
 #define SENDB  3
 #define SYNC  4
 #define TXEN  5
 #define TX9  6
 #define CSRC  7
//BAUDCON:
 #define ABDEN  0
 #define WUE  1
 #define BRG16  3
 #define SCKP  4
 #define RCIDL  6
 #define ABDOVF  7
//WDTCON:
 #define SWDTEN  0
 #define WDTPS0  1

//use Microchip naming conventions for config bits:
 #warning "CC5XBUG: apply macros within #include within #asm"
 #define LIST ;
 #define NOLIST ;
 #define IFNDEF ;
 #define MESSG ;
 #define ENDIF ;
 #define __MAXRAM ;
 #define __BADRAM ;
 #asm
;// #include "D:\Program Files\Microchip\MPASM Suite\p16f1827.inc"
_CONFIG1         EQU  H'8007'
_CONFIG2         EQU  H'8008'
// #define _INTRC_OSC_NOCLKOUT  (_FOSC_INTOSC & _CLKOUTEN_OFF) // INTOSC oscillator: I/O function on CLKIN pin; CLKOUT function is disabled. I/O or oscillator function on the CLKOUT pin

;----- CONFIG1 Options --------------------------------------------------
_FOSC_LP         EQU  H'FFF8'    ; LP Oscillator, Low-power crystal connected between OSC1 and OSC2 pins
_FOSC_XT         EQU  H'FFF9'    ; XT Oscillator, Crystal/resonator connected between OSC1 and OSC2 pins
_FOSC_HS         EQU  H'FFFA'    ; HS Oscillator, High-speed crystal/resonator connected between OSC1 and OSC2 pins
_FOSC_EXTRC      EQU  H'FFFB'    ; EXTRC oscillator: External RC circuit connected to CLKIN pin
_FOSC_INTOSC     EQU  H'FFFC'    ; INTOSC oscillator: I/O function on CLKIN pin
_FOSC_ECL        EQU  H'FFFD'    ; ECL, External Clock, Low Power Mode (0-0.5 MHz): device clock supplied to CLKIN pin
_FOSC_ECM        EQU  H'FFFE'    ; ECM, External Clock, Medium Power Mode (0.5-4 MHz): device clock supplied to CLKIN pin
_FOSC_ECH        EQU  H'FFFF'    ; ECH, External Clock, High Power Mode (4-32 MHz): device clock supplied to CLKIN pin

_WDTE_OFF        EQU  H'FFE7'    ; WDT disabled
_WDTE_SWDTEN     EQU  H'FFEF'    ; WDT controlled by the SWDTEN bit in the WDTCON register
_WDTE_NSLEEP     EQU  H'FFF7'    ; WDT enabled while running and disabled in Sleep
_WDTE_ON         EQU  H'FFFF'    ; WDT enabled

_PWRTE_ON        EQU  H'FFDF'    ; PWRT enabled
_PWRTE_OFF       EQU  H'FFFF'    ; PWRT disabled

_MCLRE_OFF       EQU  H'FFBF'    ; MCLR/VPP pin function is digital input
_MCLRE_ON        EQU  H'FFFF'    ; MCLR/VPP pin function is MCLR

_CP_ON           EQU  H'FF7F'    ; Program memory code protection is enabled
_CP_OFF          EQU  H'FFFF'    ; Program memory code protection is disabled

_CPD_ON          EQU  H'FEFF'    ; Data memory code protection is enabled
_CPD_OFF         EQU  H'FFFF'    ; Data memory code protection is disabled

_BOREN_OFF       EQU  H'F9FF'    ; Brown-out Reset disabled
_BOREN_SBODEN    EQU  H'FBFF'    ; Brown-out Reset controlled by the SBOREN bit in the BORCON register
_BOREN_NSLEEP    EQU  H'FDFF'    ; Brown-out Reset enabled while running and disabled in Sleep
_BOREN_ON        EQU  H'FFFF'    ; Brown-out Reset enabled

_CLKOUTEN_ON     EQU  H'F7FF'    ; CLKOUT function is enabled on the CLKOUT pin
_CLKOUTEN_OFF    EQU  H'FFFF'    ; CLKOUT function is disabled. I/O or oscillator function on the CLKOUT pin

_IESO_OFF        EQU  H'EFFF'    ; Internal/External Switchover mode is disabled
_IESO_ON         EQU  H'FFFF'    ; Internal/External Switchover mode is enabled

_FCMEN_OFF       EQU  H'DFFF'    ; Fail-Safe Clock Monitor is disabled
_FCMEN_ON        EQU  H'FFFF'    ; Fail-Safe Clock Monitor is enabled

;----- CONFIG2 Options --------------------------------------------------
_WRT_ALL         EQU  H'FFFC'    ; 000h to FFFh write protected, no addresses may be modified by EECON control
_WRT_HALF        EQU  H'FFFD'    ; 000h to 7FFh write protected, 800h to FFFh may be modified by EECON control
_WRT_BOOT        EQU  H'FFFE'    ; 000h to 1FFh write protected, 200h to FFFh may be modified by EECON control
_WRT_OFF         EQU  H'FFFF'    ; Write protection off

_PLLEN_OFF       EQU  H'FEFF'    ; 4x PLL disabled
_PLLEN_ON        EQU  H'FFFF'    ; 4x PLL enabled

_STVREN_OFF      EQU  H'FDFF'    ; Stack Overflow or Underflow will not cause a Reset
_STVREN_ON       EQU  H'FFFF'    ; Stack Overflow or Underflow will cause a Reset

_BORV_25         EQU  H'FBFF'    ; Brown-out Reset Voltage (VBOR) set to 2.5 V
_BORV_19         EQU  H'FFFF'    ; Brown-out Reset Voltage (VBOR) set to 1.9 V

_LVP_OFF         EQU  H'DFFF'    ; High-voltage on MCLR/VPP must be used for programming
_LVP_ON          EQU  H'FFFF'    ; Low-voltage programming enabled
 #endasm
#endif //def _CC5X


#ifdef _BOOSTC
 #warning "[INFO] Using BoostC compiler; generated code is not optimal or may contain page select errors; use .map file to align functions; use fixup script to remove W load/store from ASM"
 #warning "[CAUTION] BoostC compiler requires manual edits to .TDK files to allow space for poor code clean-up"
 #include <system.h> //do not use this for libraries

 #define SIZEOF(thing)  SIZEOF_##thing //kludge: BoostC doesn't support sizeof() within "#if"
 #define DW  data //asm

//use SDCC naming conventions for register addresses:
 #define STATUS_ADDR  STATUS
 //NOTE: use upper-case "L" on fsr so it's easier to distinguish from "1" (one), and always specify L/H to make it clear
 #ifdef INDF //create "fsr0" aliases for "fsr"
  #define fsr0L_ADDR  FSR0L_ADDR
  #define indf0_ADDR  INDF0_ADDR
  #define FSR0L_ADDR  FSR_ADDR
  #define INDF0_ADDR  INDF_ADDR
  #define FSR_ADDR  FSR
  #define fsr_ADDR  FSR_ADDR
  #define INDF_ADDR  INDF
//  uint8 fsr0l @FSR_ADDR;
  #define indf0  indf
  #define fsrL  fsr
  #define fsr0L  fsrL
  #define fsrL_ADDR  FSR0L
//  #define fsrlL  fsr
  volatile bit fsrh @STATUS_ADDR.IRP; //CAUTION: 1 bit only
  #define fsr0H  fsrH
  #define fsrH  fsrh

//  #define fsrH_byte  status
  #define fsrH_adjust(reg /*fsrH*/, val)  /*1 instr*/ \
  { \
	if (val > 0) reg = 1; \
	if (val < 0) reg = 0; \
  }
//  #define fsrH_save(reg)  { reg = 0; if (fsrH) ++reg/*reg = 1*/; } //fsrH is 1 bit; 3 instr
  inline void fsrH_save(uint8& reg)  { reg = 0; if (fsrH) ++reg/*reg = 1*/; } //fsrH is 1 bit; 3 instr; use inline func to force reg type
  #define fsrH_restore(val)  { fsrH = 0; if (val /*& 1*/) fsrH = 1; } //3 instr
  #define fsrH9_xchg(reg)  xchg_partial(reg, status, WREG &= (1<<IRP)) //only change 1 bit in status; 5 instr

//  inline void fsrH_mask(void)
//  {
//	  WREG &= 1<<IRP; //mask off IRP bit
//  }
// #define FSR1L  FSR
// #define fsr1l  fsr
// #define fsr1h  status.IRP //CAUTION: 1 bit only
// #define fsrl  fsr
//  #define fsr  fsr0l
//  #define fsrh  fsr0h
//  #define FSR  FSR0L
//  #define indf  indf0
//  #define _indf  _indf0
//  #define INDF  INDF0
 #else //create "fsr" aliases for "fsr0"
//  #define FSR_ADDR  FSR
  #define fsr_ADDR  FSR0L_ADDR
  #define INDF_ADDR  INDF0
  #define indf  indf0
  #define fsr  fsrL
  #define fsrL  fsr0L
  #define fsrh  fsrH
  #define fsrH  fsr0h //fsr0H
  #define fsrL_ADDR  FSR0L
  #define fsrH_ADDR  FSR0H
//  #define fsrH_byte  fsrH
  #define fsrH_adjust(reg /*fsrH*/, val)  /*1 instr*/ \
  { \
	if (val > 0) ++reg; \
	if (val < 0) --reg; \
  }
//  #define fsrH_save(reg)  { reg = 0; if (fsrH & 1) ++reg; } //assumes linear addressing (only lsb of fsrH used)
  inline void fsrH_save(uint8& reg)  { reg = fsrH; } //reg = 0; if (fsrH & 1) ++reg/*reg = 1*/; } //fsrH is 1 bit; 3 instr; use inline func to force reg type
  #define fsrH_restore(val)  { fsrH = val; } //0; if (val /*& 1*/) ++fsrH; } //fsrH might be 1 bit or byte, but same #instr needed for both cases
//  inline void fsrH_mask(void) {} //let the hardware do the masking; no bits are shared with other registers
//  #define fsrH_xchg(reg)  xchg(reg, fsrH)
  #define fsrH9_xchg(reg)  \
  { \
 	if (fsrH & 1) { if (!reg) { fsrH = FALSE; reg = TRUE; }} \
	else { if (reg) { fsrH = 1; reg = 0; }} \
  }
 #endif
 #ifdef INDF0
  #define fsr0L_ADDR  FSR0L_ADDR
//  #define fsr0l_ADDR  FSR0L_ADDR
  #define FSR0L_ADDR  FSR0L
  #define FSR0H_ADDR  FSR0H
  #define INDF0_ADDR  INDF0
  //default generic "fsr" to fsr0:
  #define fsr0L  fsr0l
  #define fsr0H  fsr0h
  #if FSR0H_ADDR == FSR0L_ADDR + 1
   uint16 fsr0_16 @FSR0L_ADDR; //little endian
   #define fsr0  fsr0_16
  #else
   #warning "[INFO] FSR0 L+H are not addressable as a 16-bit reg"
  #endif
 #endif
 #ifdef INDF1
  #define fsr1L_ADDR  FSR1L_ADDR
  #define FSR1L_ADDR  FSR1L
  #define FSR1H_ADDR  FSR1H
  #define INDF1_ADDR  INDF1
  #define fsr1L  fsr1l
  #define fsr1H  fsr1h
  #if FSR1H_ADDR == FSR1L_ADDR + 1
   uint16 fsr1_16 @FSR1L_ADDR; //little endian
   #define fsr1  fsr1_16
  #else
   #warning "[INFO] FSR1 L+H are not addressable as a 16-bit reg"
  #endif
 #endif
 #define TMR0_ADDR  TMR0
 #if defined(TMR1) && !defined(TMR1L)
  #define TMR1L  TMR1
  #define tmr1L  tmr1
//  #warning "tmr1"
 #else
  #define tmr1L  tmr1l
//  #warning "tmr1l"
 #endif
 #define tmr1H  tmr1h
 #if defined(TMR1L) && defined(TMR1H)
  #define TMR1H_ADDR  TMR1H
  #define TMR1L_ADDR  TMR1L
  #if TMR1H_ADDR == TMR1L_ADDR + 1
   uint16 tmr1_16 @TMR1L_ADDR; //little endian
//   #define tmr1  tmr1_16
  #else
   #warning "[INFO] TMR1 L+H are not addressable as a 16-bit reg"
  #endif
 #endif
 #if defined(APFCON0) && !defined(APFCON)
  #define APFCON  APFCON0
  #define apfcon  apfcon0
 #endif
 #ifdef PORTA
  #define TRISA_ADDR  TRISA
  #define PORTA_ADDR  PORTA
  #define ANSELA_ADDR  ANSELA
  #ifdef LATA
   #define LATA_ADDR  LATA
   #define lata_ADDR  LATA_ADDR
  #else
   #define LATA_ADDR  LATA
   #define lata_ADDR  PORTA_ADDR
   #define lata  porta
  #endif
  #ifdef ANSEL
   #define ANSELA  ANSEL
   #define ansela  ansel
  #endif
 #endif
 #ifdef PORTB
  #define TRISB_ADDR  TRISB
  #define PORTB_ADDR  PORTB
  #define portbc  portb
  #define trisbc  trisb
  #ifdef WPUB
   #define wpubc  wpub
   #define WPUBC_ADDR  WPUB
  #endif
  #ifdef LATB
   #define latbc  latb
   #define LATB_ADDR  LATB
   #define latb_ADDR  LATB_ADDR
   #define LATBC_ADDR  LATB
  #else
   #define LATBC_ADDR  LATB
   #define latbc_ADDR  LATBC_ADDR
   #define latbc  portb
  #endif
  #ifdef ANSELB
   #define ANSELB_ADDR  ANSELB
   #define anselbc  anselb
  #endif
 #endif
 #ifdef PORTC
  #define TRISC_ADDR  TRISC
  #define PORTC_ADDR  PORTC
  #define portbc  portc
  #define trisbc  trisc
  #ifdef WPUB
   #define wpubc  wpuc
   #define WPUBC_ADDR  WPUC
  #endif
  #ifdef LATC
   #define latbc  latc
   #define LATC_ADDR  LATC
   #define latc_ADDR  LATC_ADDR
   #define LATBC_ADDR  LATC
  #else
   #define LATBC_ADDR  LATC
   #define latbc_ADDR  LATC_ADDR
   #define latbc  portc
  #endif
  #ifdef ANSELC
   #define ANSELC_ADDR  ANSELC
   #define anselbc  anselc
  #endif
 #endif
 #ifndef BAUDCON
  #define baudcon  baudctl
 #endif
 #if defined(SPBRG) && !defined(SPBRGL)
  #define SPBRGL  SPBRG
 #endif
 #if defined(SPBRGL) || defined(SPBRGH)
  #define SPBRGH_ADDR  SPBRGH
  #define SPBRGL_ADDR  SPBRG
  #define spbrgL  spbrg
  #define spbrgH  spbrgh
  #if SPBRGH_ADDR == SPBRGL_ADDR + 1
   uint16 spbrg_16 @SPBRGL_ADDR; //little endian
//   #define spbrg  spbrg_16
  #else
//   #warning "[INFO] SPBRG L+H are not addressable as a 16-bit reg"
  #endif
 #endif
 #ifndef HFIOFS
  #define HFIOFS  HTS
  #define oscstat  osccon
 #endif
 #if defined(EEADR) && !defined(EEADRL)
   #define EEADRL  EEADR
 #endif
 #ifndef CFGS
  #define CFGS  0 //no explicit User ID/Device ID space defined
 #endif
// #define eeadrL  eeadr
 uint8 eeadrL @EEADR;
 #if defined(EEADRL) || defined(EEADRH)
  #define EEADRL_ADDR  EEADRL
  #define EEADRH_ADDR  EEADRH
  #if EEADRH_ADDR == EEADRL_ADDR + 1
   uint16 eeadr_16 @EEADRL_ADDR; //little endian
   #define eeadr  eeadr_16
  #else
   #warning "[INFO] EEADR L+H are not addressable as a 16-bit reg"
  #endif
 #endif
 #define eeadrH  eeadrh
 #define eedatH  eedath
 #if defined(EEDATA) && !defined(EEDATL)
  #define EEDATL  EEDATA
 #endif
 #define eedatL  eedata
 #if defined(EEDATL) || defined(EEDATH)
  #define EEDATL_ADDR  EEDATL
  #define EEDATH_ADDR  EEDATH
  #define eedatH_ADDR  EEDATH_ADDR
  #if EEDATH_ADDR == EEDATL_ADDR + 1
   uint16 eedat_16 @EEDATL_ADDR; //little endian
   #define eedat  eedat_16
  #else
   #warning "[INFO] EEDAT L+H are not addressable as a 16-bit reg"
  #endif
 #endif
// #ifdef CMCON0
//  #define CMCON0_ADDR  CMCON0
// #endif
 #define CMCON0_ADDR  CMCON0
 #define OPTION_REG_ADDR  OPTION_REG
 #define INTCON_ADDR  INTCON
 #define PIR1_ADDR  PIR1
 #define pir1_ADDR  PIR1_ADDR
 #define RCSTA_ADDR  RCSTA
 #define rcsta_ADDR  RCSTA_ADDR
 #define TXREG_ADDR  TXREG
 #define T1CON_ADDR  T1CON
 #define RCREG_ADDR  RCREG
 #define rcreg_ADDR  RCREG_ADDR
 #define TXREG_ADDR  TXREG
 #define txreg_ADDR  TXREG_ADDR
 #ifdef RP1
  #define IFRP1(stmt)  stmt
  volatile bit rp1 @STATUS_ADDR.RP1;
 #else
  #define IFRP1(stmt)
 #endif

 #define BITADDR(adrs)  (adrs)/8.(adrs)%8 //for use with @ on bit vars
 
//bits:
 #ifdef HTS
  #define HFIOFS  HTS
 #endif
 #ifdef NOT_RAPU
  #define NOT_WPUEN  NOT_RAPU
 #endif
 #ifdef TMR1CS
  #define TMR1CS0  TMR1CS
 #endif
 #if defined(SCS) && !defined(SCS0)
  #define SCS0  SCS
 #endif

//registers:
// #define oscstat  osccon
// #define baudcon  baudctl
// volatile uint8 TMPREG @0x7F; //unbanked temp reg

 //pseudo-registers:
 //BoostC will generate opcodes and then ASM fixup script will replace with desired functionality
 //bit or byte vars are used here instead of DW+opcode so BoostC will in-line these instructions in conditional stmts (it won't do that with a DW); also, RAM can be re-alloacted but prog space is more difficult)
 //no RAM space is used for these in final compiled code
 //non-banked addresses are used here only to avoid generating unnecessary bank selects in surrounding code (simpler ASM fixup)
 #undef WREG //#def conflict
 volatile uint8 WREG @0x70; //allow references to W in expressions; loads/stores will be removed from ASM with fixup script; unbanked to avoid extra bank selects
// volatile bit LEZ @0x70.0; //<= 0
 volatile bit /*ALWAYS @0x70.0,*/ NEVER @0x70.0; //dummy flags to keep/remove code
 #define ALWAYS  !NEVER
 volatile uint8 BANKSELECT_ @0x70; //force bank select; usage: BANKSELECT = reg/const
 //volatile uint8 BANKSELECTINDIR_ @0x70; //force indirect bank select; usage: BANKSELECTINDIR = reg/const
 volatile uint8 RETLW_ @0x70; //return value in W; usage RETLW = val
 volatile uint8 RETURN_ @0x70; //force hard return to caller; usage: RETURN = 0 or RETURN.# = TRUE/FALSE
 volatile uint8x8 ABSGOTO_ @0x70; //jump to label outside of current function; usage: ABSGOTO.# = TRUE/FALSE
 volatile uint8x8 ABSCALL_ @0x70; //call label outside of current function; usage: ABSCALL.# = TRUE/FALSE
 volatile uint8x8 ABSLABEL_ @0x70; //set target label for call/goto; usage: ABSLABEL.# = TRUE/FALSE
 volatile uint8x8 ABSDIFF_ @0x70; //calculate distance between 2 labels, leave result in WREG; usage: ABSDIFF_[to] = from
 volatile uint8x8 ABSDIFFXOR_ @0x70; //XOR ABSDIFF with WREG: ABSDIFFXOR_[to] = from
 volatile uint8 INGOTOC_ @0x70; //in/out of gotoc; usage: INGOTOC.# = TRUE/FALSE
 volatile uint8 ONPAGE_ @0x70; //force function to specified page#; usage: ONPAGE.# = TRUE/FALSE
 volatile uint2x8 ATADRS_ @0x70; //force code to specified address; usage: ATADRS[0] = low; ATADRS[1] = high;
 volatile uint8 TRAMPOLINE_ @0x70; //allow crossing page boundary or pad tracked prog address to match actual; usage: TRAMPOLINE = TRUE/FALSE
 volatile uint8 SET_EEADR_ @0x70; //set EEADR; usage: SET_EEADR = 0 or SET_EEADR.# = TRUE/FALSE
 volatile uint8 NOOP_ @0x70; //generate nop instr; usage: NOOP_.# = TRUE/FALSE
 volatile uint8x8 MOVIW_ @0x70; //load WREG indirect via fsr, with optional pre/post inc/dec; uses 1 instr cycle, but requires PIC16 extended instruction set
 volatile uint8x8 MOVWI_ @0x70; //save WREG indirect via fsr, with optional pre/post inc/dec; uses 1 instr cycle, but requires PIC16 extended instruction set
 volatile uint8 INDF_PREINC_ @0x70; //save WREG indirect via fsr after inc fsr; uses 1 instr cycle, but requires PIC16 extended instruction set
 volatile uint8 INDF_PREDEC_ @0x70; //save WREG indirect via fsr after dec fsr; uses 1 instr cycle, but requires PIC16 extended instruction set
 volatile uint8 INDF_POSTINC_ @0x70; //save WREG indirect via fsr then inc fsr; uses 1 instr cycle, but requires PIC16 extended instruction set
 volatile uint8 INDF_POSTDEC_ @0x70; //save WREG indirect via fsr then dec fsr; uses 1 instr cycle, but requires PIC16 extended instruction set
 #ifdef PIC16X
  volatile uint8 INDF1_PREINC_ @0x70; //save WREG indirect via fsr after inc fsr; uses 1 instr cycle, but requires PIC16 extended instruction set
  volatile uint8 INDF1_PREDEC_ @0x70; //save WREG indirect via fsr after dec fsr; uses 1 instr cycle, but requires PIC16 extended instruction set
  volatile uint8 INDF1_POSTINC_ @0x70; //save WREG indirect via fsr then inc fsr; uses 1 instr cycle, but requires PIC16 extended instruction set
  volatile uint8 INDF1_POSTDEC_ @0x70; //save WREG indirect via fsr then dec fsr; uses 1 instr cycle, but requires PIC16 extended instruction set
 #endif
 volatile uint8x8 SWOPCODE_ @0x70; //swap/move opcodes; usage: SWOPCODE_[to] = from
 //volatile uint8x8 PROGPAD_ @0x70; //pad prog space; usage: PROGPAD_[min] = max
 volatile uint8 PROGPAD_ @0x70; //pad prog space; usage: PROGPAD_ = instr
 volatile uint8 MARKER_ @0x70; //make it easier to find code; usage: MARKER_ = where
 volatile uint8 NOFLIP_ @0x70; //inhibit instr flipping; usage: MARKER_ = duration
 volatile uint8 PROGADJUST_ @0x70; //compensate for incorret prog address tracking
 //pseudo-opcodes:
 #define BANKSELECT(reg)  BANKSELECT_ = reg
 //#define BANKSELECT_INDIR(reg)  BANKSELECTINDIR_ = reg
 #define RETLW(val)  RETLW_ = val
 #define RETURN()  RETURN_.0 = TRUE
 //#define ABSGOTO(n)  ABSGOTO_.bytes[n/8].(n%8) = TRUE
 #define ABSGOTO(n)  ABSGOTO_.bytes[(n)/8] |= 1 << ((n)%8)
 //#define ABSCALL(n)  ABSCALL_.bytes[n/8].(n%8) = TRUE
 #define ABSCALL(n)  ABSCALL_.bytes[(n)/8] |= 1 << ((n)%8)
 //#define ABSLABEL(n)  ABSLABEL_.bytes[n/8].(n%8) = TRUE
 #define ABSLABEL(n)  ABSLABEL_.bytes[(n)/8] |= 1 << ((n)%8)
 #define ABSDIFF_WREG(from, to)  ABSDIFF_.bytes[to] = from
 #define ABSDIFF_XOR_WREG(from, to)  ABSDIFFXOR_.bytes[to] = from
 #define GOTOC_WARNONLY  MAYBE
 #define INGOTOC(yesno)  INGOTOC_.IIF(yesno == GOTOC_WARNONLY, 7, 0) = yesno
 #define ONPAGE(n)  ONPAGE_.n = TRUE
 #define ATADRS(addr)  { ATADRS_.bytes[0] = IIF(addr < 0, -2, (addr) % 0x100); ATADRS_.bytes[1] = IIF(addr < 0, -1, (addr) / 0x100); } //kludge: make bytes different to avoid folding
 #define TRAMPOLINE(n)  REPEAT(2*(n), TRAMPOLINE_.0 = n) //kludge: put some padding in prog space to compensate for address tracking errors; movlp will be in pairs
 #define SET_EEADR()  SET_EEADR_.0 = TRUE
 #define NOOP(n)  NOOP_.n = TRUE
#if 0 //obsolete?
 #define indf2WREG(fsr)  MOVIW(fsr##_WHICH, fsr##_FSRMODE)
 #define indf2WREG_undo(fsr)  MOVIW(fsr##_WHICH, 3 - fsr##_FSRMODE) //reverse direction and precedence
//#define indf2WREF_noauto(fsr)  MOVIW(fsr##_WHICH, -1) //no auto inc/dec
//	asm data 0x10 + 8 * IIF(fsr##_ADDR == FSR1_ADDR, 8, 0) + ((FSRMODE_##fsr) & 3) /*moviw*/
 #define WREG2indf(fsr)  MOVWI(fsr##_WHICH, fsr##_FSRMODE)
//	asm data 0x18 + 8 * IIF(fsr##_ADDR == FSR1_ADDR, 8, 0) + ((FSRMODE_##fsr) & 3) /*movwi*/
 #define PRE_INC  0x40
 #define PRE_DEC  0x41
 #define POST_INC  0x42
 #define POST_DEC  0x43
 #define IsDecreasing(direction)  ((direction) & 1)
 #define incdec(reg, direction)  { if (!IsDecreasing(direction)) ++reg; if (IsDecreasing(direction)) --reg; } //1 instr for const direction; 4 instr for variable direction (vs. 4.5 avg for if/else)
 #ifdef PIC16X //extended instr set
  #define MOVIW(fsr, adjust)  /*fsr[pre/post inc/dec or offset] => WREG*/ \
  { \
	if ((fsr) & 1) MOVIW_.bytes[(adjust)/8] |= 1 << ((adjust) % 8); \
	if (!((fsr) & 1)) MOVIW_.bytes[(adjust)/8] &= ~(1 << ((adjust) % 8)); \
  }
  #define MOVWI(fsr, adjust)  /*WREG => fsr[pre/post inc/dec or offset]*/ \
  { \
	if ((fsr) & 1) MOVWI_.bytes[(adjust)/8] |= 1 << ((adjust) % 8); \
	if (!((fsr) & 1)) MOVWI_.bytes[(adjust)/8] &= ~(1 << ((adjust) % 8)); \
  }
 #else
  //NOTE: assumes no bank wrap; status.Z will not reflect loaded value
  #define MOVIW(ignored, adjust)  \
  { \
	if ((adjust) == PRE_INC) { ++fsrL; WREG = indf; } /*pre-inc*/ \
	if ((adjust) == PRE_DEC) { --fsrL; WREG = indf; } /*pre-dec*/ \
	if ((adjust) == POST_INC) { WREG = indf; ++fsrL; } /*post-inc*/ \
	if ((adjust) == POST_DEC) { WREG = indf; --fsrL; } /*post-dec*/ \
	if (!((adjust) & 0x40)) { fsrL += ((adjust) & 0x3F) - 0x20; WREG = indf; } /*offset*/ \
  }
  #define MOVWI(ignored, adjust)  \
  { \
	if ((adjust) == PRE_INC) { ++fsrL; indf = WREG; } /*pre-inc*/ \
	if ((adjust) == PRE_DEC) { --fsrL; indf = WREG; } /*pre-dec*/ \
	if ((adjust) == POST_INC) { indf = WREG; ++fsrL; } /*post-inc*/ \
	if ((adjust) == POST_DEC) { indf = WREG; --fsrL; } /*post-dec*/ \
	if (!((adjust) & 0x40)) { fsrL += ((adjust) & 0x3F) - 0x20; indf = WREG; } /*offset*/ \
  }
 #endif
#endif
// #define WANT_SWAP  1
// #define WANT_MOVE  2
 #define SWOPCODE(from, to)  SWOPCODE_.bytes[to] = from //{ SWOPCODE_.bytes[1] = from; SWOPCODE_.bytes[2] = to; SWOPCODE_.bytes[0] = mode; }
 //#define PROGPAD(min, max)  PROGPAD_[min] = max
 #define PROGPAD(instr)  PROGPAD_ = instr
 #define MARKER(value)  MARKER_ = value
 #define NOFLIP(n)  NOFLIP_ = n
 #define PROGADJUST(n)  PROGADJUST_ = n
//config:
 #ifndef _INTRC_OSC_NOCLKOUT
  #define _INTRC_OSC_NOCLKOUT  (_FOSC_INTOSC & _CLKOUTEN_OFF) // INTOSC oscillator: I/O function on CLKIN pin; CLKOUT function is disabled. I/O or oscillator function on the CLKOUT pin
 #endif
 #ifndef _EC_OSC
  #define _EC_OSC  _FOSC_ECH // ECH, External Clock, High Power Mode (4-32 MHz): device clock supplied to CLKIN pin
 #endif
 #ifndef _WDT_OFF
  #define _WDT_OFF  _WDTE_OFF // WDT disabled
  #define _WDT_ON  _WDTE_ON // WDT enabled
 #endif
 #ifndef _BOD_OFF
  #define _BOD_OFF  _BOREN_OFF // Brown-out Reset disabled
  #define _BOD_ON  _BOREN_ON // Brown-out Reset enabled
 #endif
 #ifndef _CONFIG
  #define _CONFIG  _CONFIG1 //first config word
 #endif

//give distinct symbolic names to make ASM debug easier:
 volatile bit Carry @STATUS_ADDR.C;
 volatile bit NotBorrow @STATUS_ADDR.C; //Borrow == inverted Carry; TODO: treat "Borrow" as !Carry within opcode fixups?
 #define Borrow  !NotBorrow //CAUTION: BoostC will generate a temp for !Borrow; use NotBorrow to avoid this
 volatile bit EqualsZ @STATUS_ADDR.Z;
//Borrow is Carry inverted:
//avoid "!!" (BoostC compiler uses a temp)
//#define Borrow  !Carry
//#define Borrow(value)  (Carry == !(value))
 #ifdef C
  #undef C //void conflict/confusion with port names
  #define C_  0
 #endif

//special opcodes and extensions:
 #define clrwdt()  clear_wdt()
// inline void nop(void)
// {
// 	asm nop;
// }
// inline void nop2(void)
// {
// 	asm //use asm block to prevent dead code removal
// 	{
// 		goto nextline; //1 instr takes 2 cycles to execute
// 	nextline:
// 	}
// }
// inline void RETURN(void)
// {
// 	asm data 8; //NOTE: compiler does not "see" this as a return; this avoids removal of following code as dead, but may cause page/bank tracking problems
// }
// inline void RETURN_Bank0(void)
// {
//#ifdef RP1
//	status.RP0 = 0;
//	status.RP1 = 0;
//#else //def RP1
//	asm movlb 0; //movf 0x20, F; //kludge: force Bank 0 select so BSR is in consistent state (compiler loses track if RETURN() is used)
//#endif //def RP1
//	RETURN();
// }
 //use functions rather than asm whenever possible so BoostC knows it's one instr and can use skip rather than goto around
// /*inline*/ void RETURN_0(void) @0x7FF {} //page 0
// /*inline*/ void RETURN_1(void) @0xFFF {} //page 1
// void MAIN_0(void) @0x7FE {}
// void MAIN_1(void) @0xFFE {}

 //#define GOTOC_setup(page)  /*if ((page) & 0x700)*/ asm movlp byteof(page, 1); /*need to set more bits; compiler only sets 0xE00*/
// #define START  TRUE
// #define STOP  FALSE
// #define GOTOC(onoff)  pclath = IIF(onoff, 0x7f, 0x7e) //NOTE: this is just a flag for asm fixup script to intervene
 //pseudo-opcodes to signal asm fixup script:
// void GOTOC_start(void) @0x7FF {}
// void GOTOC_end(void) @0x7FE {}
// #define _INGOTOC(yesno)  data IIF(yesno, 0x2FFF, 0x2FFE) //call 0x7FF or 0x7FE
//#if 1
 //use functions rasther than asm whenever possible so BoostC knows it's one instr and can use skip rather than goto around
// #define _INGOTOC(yesno)  _INGOTOC_##yesno
// #define _INGOTOC_TRUE  data 0x2FFF //call 0x7FF or 0x7FE
// #define _INGOTOC_FALSE  data 0x2FFE //call 0x7FF or 0x7FE
// #define INGOTOC(yesno)  INGOTOC_##yesno()
// inline void INGOTOC_TRUE(void)
// {
//	asm _INGOTOC(TRUE);
// }
// inline void INGOTOC_FALSE(void)
// {
//	asm _INGOTOC(FALSE);
// }
// #define _ONPAGE(n)  data 0x2FFD - (n) //call 0x7FD or 0x7FC
// #define ONPAGE(n)  ONPAGE_##n()
// inline void ONPAGE_0(void)
// {
//	asm _ONPAGE(0);
// }
// inline void ONPAGE_1(void)
// {
//	asm _ONPAGE(1);
// }
 //allow alternate entry points into functions (up to 3 of them):
 //CAUTION: must verify correct page and bank handling
// #define _ABSGOTO(n)  data 0x2FF4 + (n)%4
// #define ABSGOTO(n)  ABSGOTO_##n()
// #define _ABSCALL(n)  data 0x2FF8 + (n)%4 //CAUTION: overlaps with FIXED_ADRS and SET_EEADR, ao only first 2 can be used
// #define ABSCALL(n)  ABSCALL_##n()
// #define _ABSLABEL(n)  data 0x2FF0 + (n)%4
// #define ABSLABEL(n)  ABSLABEL_##n()
 //make these look like regular functions so they can be used with an "if" without goto around it
// /*inline*/ void ABSGOTO_0(void) @0x7FD
// {
//	asm _ABSGOTO(0);
// }
// /*inline*/ void ABSGOTO_1(void) @0x7FC //used in page 0 only
// {
//	asm _ABSGOTO(1);
// }
// /*inline*/ void ABSGOTO_2(void) @0xFFD
// {
//	asm _ABSGOTO(2);
// }
// /*inline*/ void ABSGOTO_3(void) @0xFFC
// {
//	asm _ABSGOTO(3);
// }
// inline void ABSCALL_0(void)
// {
//	asm _ABSCALL(0);
// }
// inline void ABSCALL_1(void)
// {
//	asm _ABSCALL(1);
// }
// inline void ABSLABEL_0(void)
// {
//	asm _ABSLABEL(0);
// }
// inline void ABSLABEL_1(void)
// {
//	asm _ABSLABEL(1);
// }
// inline void ABSLABEL_2(void)
// {
//	asm _ABSLABEL(2);
// }
// inline void ABSLABEL_3(void)
// {
//	asm _ABSLABEL(3);
// }
 //set eeadrh/eeadrl to preceding "goto" target:
// #define SET_EEADR(adrs)  \
// { \
//	if (NEVER) goto adrs; \
//	eeadrh = byteof(0xBEEF, 1); \
//	eeadrl = byteof(0xBEEF, 0); \
//	_SET_EEADR(); \
// }
 //keep function at specified adrs (don't relocate):
// inline void FIXED_ADRS(void)
// {
//	asm data 0x2FFA;
// }
 //set eeadrh/eeadrl to current prog adrs:
// inline void SET_EEADR(void)
// {
//	asm data 0x2FFB;
// }

//#else
 //pseudo opcodes for non-standard C constructs:
// #define INGOTOC(yesno)  INGOTOC_##yesno()
// void INGOTOC_TRUE(void) @0x2FFF {}
// void INGOTOC_FALSE(void) @0x2FFE {}

// #define ONPAGE(n)  ONPAGE_##n()
// void ONPAGE_0(void) @0x2FFD {}
// void ONPAGE_1(void) @0x2FFC {}

// void FIXED_ADRS(void) @0x2FFA {}
// void SET_EEADR(void) @0x2FFB {}

 //alternate entry point handling:
// #define ABSLABEL(n)  asm data 0x2FF0 + (n)%4
// #define ABSGOTO(n)  asm data 0x2FF4 + (n)%4

// #define ABSCALL(n)  ABSCALL_##n() //CAUTION: overlaps with FIXED_ADRS and SET_EEADR, ao only first 2 can be used
// void ABSCALL_0(void) @0x2FF8 {}
// void ABSCALL_1(void) @0x2FF9 {}
//#endif

 //make debug within BoostC IDE easier:
 //these will just return immediately so IDE can step thru the special opcodes above
// #if 0
// void RetFromSpecialOpcodes_pg0(void) @0x0FF8
// {
//	RETURN();
//	RETURN();
//	RETURN();
//	RETURN();
//	RETURN();
//	RETURN();
//	RETURN();
//	//compiler will generate another one at function end
// }
// void RetFromSpecialOpcodes_pg1(void) @0x1FF8
// {
//	RETURN();
//	RETURN();
//	RETURN();
//	RETURN();
//	RETURN();
//	RETURN();
//	RETURN();
//	//compiler will generate another one at function end
// }
// #endif
  
// #define RETLW(val)  data 0x3400 | (val)
// #define rl(reg)  ((reg)<<1)
// #define rl_nc(reg)  ((reg)+(reg))
 //rotate left, leave Carry as-is:
 inline void rl_nc(uint8& reg)
 {
	asm rlf _reg, F;
 }
 inline void rl_nc(uint2x8& reg16) { rl_nc(reg16.bytes[0]); rl_nc(reg16.bytes[1]); }
 inline void rl_nc_WREG(uint8& reg)
 {
	asm rlf _reg, W;
	asm movwf _WREG; //IDE debug only; this instr will be dropped by ASM fixup
 }
 inline void rl_WREG(uint8& reg) { Carry = 0; rl_nc_WREG(reg); }
 inline void rl(uint8& reg) //2 instr; trashes WREG
 {
	rl_nc_WREG(reg); //load Carry with rotated bit
	rl_nc(reg);
 }
 //left-rotate upper half of byte in-place:
 inline void rl_upper(uint8& reg) //5 instr; trashes WREG
 {
	WREG = reg & 0xF0; reg += WREG; //shift upper 4 bits left by 1
	if (Carry) reg |= 0x10; //insert top bit into bottom position
 }
// #define rr(reg)  ((reg)>>1)
 //rotate right, leave Carry as-is:
 inline void rr_nc(uint8& reg)
 {
	asm rrf _reg, F;
 }
 inline void rr_nc_WREG(uint8& reg)
 {
	asm rrf _reg, W;
	asm movwf _WREG; //IDE debug only; this instr will be dropped by ASM fixup
 }
 inline void rr_WREG(uint8& reg) { Carry = 0; rr_nc_WREG(reg); }
 inline void rr(uint8& reg)
 {
	rr_nc_WREG(reg); //load Carry with rotated bit
	rr_nc(reg);
 }
// #define swap(reg)  (((reg)>>4) || ((reg)<<4))
 inline void swap(uint8& reg)
 {
	asm swapf _reg, F;
 }
 inline void swap_WREG(uint8& reg)
 {
	asm swapf _reg, W;
	asm movwf _WREG; //IDE debug only; this instr will be dropped by ASM fixup
 }
 //BoostC sometimes doesn't understand COM instr, so explicitly use it here:
 inline void compl(uint8& reg)
 {
	asm comf _reg, F;
 }
 inline void compl_WREG(uint8& reg)
 {
	asm comf _reg, W;
	asm movwf _WREG; //IDE debug only; this instr will be dropped by ASM fixup
 }
 //BoostC doesn't handle W = W + reg correctly, so force it here:
 inline void add_WREG(uint8& reg)
 {
	asm addwf _reg, W;
 }
 //load WREG indirect via fsr, with optional pre/post inc/dec:
 //uses 1 instr cycle, but requires PIC16 extended instruction set
 //an associated FSRMODE_ macro controls inc/dec behavior
// #define indf2WREG(fsr)  \
//	if ((FSRMODE_##fsr) & ~3) \
//	{ \
//		if (fsr##_ADDR == FSR1_ADDR) WREG = indf1; \
//		else WREG = indf0; \
//	} \
//	else asm data 0x10 + 8 * IIF0(fsr##_ADDR == FSR1_ADDR, 8, 0) + ((FSRMODE_##fsr) & 3) /*moviw*/
// inline void indf2WREG_postinc(uint8& fsr)
// {
//	asm data 0x12; //WREG = indf0; ++fsr0L; (//MOVIW FSR0++) //CAUTION: requires extended instr (post-inc); executes in 1 instr cycle
//	asm movwf _WREG; //IDE debug only; this instr will be dropped by ASM fixup
// }
 //allow macros to control fsr inc/dec behavior:
// #define indf2WREG(fsr)  \
// { \
//	if ((FSRMODE_##fsr) == 0) indf2WREG_preinc(fsrBINX) \
//	if ((FSRMODE_##fsr) == 1) indf2WREG_predec(fsrBINX) \
//	if ((FSRMODE_##fsr) == 2) indf2WREG_postinc(fsrBINX) \
//	if ((FSRMODE_##fsr) == 3) indf2WREG_postdec(fsrBINX) \
//	if ((FSRMODE_##fsr) & ~3) indf2WREG_postdec(fsrBINX) \
// }

 //save WREG indirect via fsr, with optional pre/post inc/dec:
 //uses 1 instr cycle, but requires PIC16 extended instruction set
 //an associated FSRMODE_ macro controls inc/dec behavior
// #define WREG2indf(fsr)  \
//	if ((FSRMODE_##fsr) & ~3) \
//	{ \
//		if (fsr##_ADDR == FSR1_ADDR) indf1 = WREG; \
//		else indf0 = WREG; \
//	} \
//	else asm data 0x18 + 8 * IIF0(fsr##_ADDR == FSR1_ADDR, 8, 0) + ((FSRMODE_##fsr) & 3) /*movwi*/

 //add literal to fsr0 without changing WREG (uses 1 instr cycle):
// #define fsr_add(fsr, val)  asm data 0x3100 + (((fsr) & 1) << 6) + ((val) & 0x3f)

// inline void sleep(void)
// {
// 	asm sleep;
// }
#endif //def _BOOSTC


#ifdef _GCC_ ////dummy defs just to pass syntax checks
 #warning "[INFO] Using GCC compiler (not working yet); use gcc -E -D_NOGCC_ -D_BOOSTC -D_PIC16F1827 -D_PIC16 renrgb_main.c"

//define dummy vars to simulate registers:
 unsigned uint8 WREG, spbrgl, spbrgh, tmr1l, tmr1h, pir1, osccon, rcreg, _rcreg, rcsta, baudctl, txsta, intcon, tmr0, porta, portb, portc, pcl, pclath, trisa, trisb, trisc, txreg, fsr0l, fsr0h, status, option_reg;
 typedef unsigned int bit;
// const uint8 *instrptr = "~~~", *instreame = instrptr + 3; //simulate data stream (for test/debug)
// const uint8 *instrptr = "\0x7E\0x90\0xCC\0x0F\0\0xCC\0\0xF0\0xAA\0xAA\0xAA\0xAA\0xAA\0xAA\0xAA\0xAA\0xAA\0xAA\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"; //simulate data stream (for test/debug)
// const uint8* instreame = instrptr + 58; //simulated data from Vixen; can't use strlen() because of embedded nulls

 #define maintype  int //return type for main()

//dummy addresses:
 #define INDF_ADDR  1
 #define FSR_ADDR  2
 #define TMR0_ADDR  3
 #define STATUS_ADDR  4
 #define TRISA_ADDR  0x85
 #define PORTA_ADDR  6
 #define ANSELA_ADDR  7
 #define TRISB_ADDR  0x88
 #define PORTB_ADDR  9
 #define ANSELB_ADDR  10
// #define SPBRGH_ADDR  11
// #define SPBRGL_ADDR  12
// #define CMCON0_ADDR  13
// #define OPTION_REG_ADDR  14
// #define INTCON_ADDR  15
 #define PIR1_ADDR  16
 #define RCSTA_ADDR  0x197

 #define fsr  fsr0l
 #define indf  indf0

// #define _retadrs  retadrs
// #define _pcl  pcl
// #define _pclath  pclath
// #define _ge_OutByte  ge_OutByte
// #define _porta  porta
// #define _portc  portc
// #define _x  x
// #define _y  y
// #define _ofs  ofs
// #define _bits  bits
 #define indf  (*(uint8*)fsr)

// #define asm
// #define data
// #define movf
// #define movwf
// #define swapf
// #define xorlw
// #define addlw
// #define addwf
// #define subwf
// #define rrf
// #define W  0
// #define F  1
// #define Z  2

// #define NOT_RAPU  7
// #define PS0  0
// #define IRCF0  4
// #define SCS  0
// #define BRG16  3
// #define WUE  1
// #define ABDEN  0
// #define SCKP  4
// #define BRGH  2
// #define SYNC  4
// #define TX9  6
// #define TXEN  5
// #define CSRC  7
// #define SENDB  3
// #define TX9D  0
// #define SPEN  7
// #define CREN  4
// #define ADEN  3
// #define RX9D  0
// #define OSFIF  2
// #define RX9  6
// #define SREN  5
// #define ADDEN  3
// #define T0IF  2
// #define RCIF  5
// #define HTS  2
// #define PSA  3
// #define OERR  1
// #define FERR  2
// #define T0CS  5

 #define Trace(params)  if (WantTrace) { --WantTrace; printf params; }
 unsigned long traceseq = 0;
 int WantTrace = 0;
 const char* snat(const char* buf, int len)
 {
	if (!buf) return("(null)");
	char outbuf[512];
	sprintf(outbuf, "%d @0x%x: ", len, buf);
	while (len-- > 0)
	{
		sprintf(outbuf + strlen(outbuf), "0x%x", *buf++);
		if (len) strcat(outbuf, ", ");
	}
	return(outbuf);
 }
#endif //def _GCC_


#ifdef SDCC
 #warning "[INFO] Using SDCC compiler (not working yet)"
 #warning "use command line:  sdcc --use-non-free [-E] -mpic14 -p16f688 -D_PIC16F688 -DNO_BIT_DEFINES -D_PIC16  thisfile.c"
 #define inline  //not supported
// #define _GCC_
// #error "SDCC not working yet"

//use BoostC naming conventions for registers:
 #define indf  INDF
 #define tmr0  TMR0
 #define pcl  PCL
 #define status  STATUS
 #define fsr  FSR
 #define porta  PORTA
 #define portc  PORTC
 #define pclath  PCLATH
 #define intcon  INTCON
 #define pir1  PIR1
 #define tmr1l  TMR1L
 #define tmr1h  TMR1H
 #define t1con  T1CON
 #define baudctl  BAUDCTL
 #define spbrgh  SPBRGH
 #define spbrg  SPBRG
 #define rcreg  RCREG
 #define txreg  TXREG
 #define txsta  TXSTA
 #define rcsta  RCSTA
 #define wdtcon  WDTCON
 #define cmcon0  CMCON0
 #define cmcon1  CMCON1
 #define adcon0  ADCON0
 #define option_reg  OPTION_REG
 #define trisa  TRISA
 #define trisc  TRISC
 #define pcon  PCON
 #define osccon  OSCCON
 #define ansel  ANSEL
 #define wpua  WPUA
 #define eedath  EEDATH
 #define eeadrh  EEADRH
 #define eedata  EEDATA
 #define eeadr  EEADR
 #define eecon1  EECON1
 #define eecon2  EECON2

 #define C  0 //status.C
 #define Z  2 //status.Z
 #define RP0  5 //status.RP0
 #define RP1  6 //status.RP1
 #define IRP  7 //status.IRP

/////// PIR1 Bits //////////////////////////////////////////////////////////
#define EEIF                  0x0007 
#define ADIF                  0x0006 
#define RCIF                  0x0005 
#define C2IF                  0x0004 
#define C1IF                  0x0003 
#define OSFIF                 0x0002 
#define TXIF                  0x0001 
#define T1IF                  0x0000 
#define TMR1IF                0x0000 

/////// T1CON Bits /////////////////////////////////////////////////////////
#define T1GINV                0x0007 
#define TMR1GE                0x0006 
#define T1CKPS1               0x0005 
#define T1CKPS0               0x0004 
#define T1OSCEN               0x0003 
#define NOT_T1SYNC            0x0002 
#define TMR1CS                0x0001 
#define TMR1ON                0x0000 

 #define SCKP  4 //baudctl.SCKP
 #define BRG16  3 //baudctl.BRG16
 #define WUE  1 //baudctl.WUE
 #define ABDEN  0 //baudctl.ABDEN

/////// TXSTA Bits ////////////////////////////////////////////////////////
#define CSRC                  0x0007 
#define TX9                   0x0006 
#define TXEN                  0x0005 
#define SYNC                  0x0004 
#define SENDB                 0x0003 
#define BRGH                  0x0002 
#define TRMT                  0x0001 
#define TX9D                  0x0000 

/////// RCSTA Bits ////////////////////////////////////////////////////////
#define SPEN                  0x0007 
#define RX9                   0x0006 
#define SREN                  0x0005 
#define CREN                  0x0004 
#define ADDEN                 0x0003 
#define FERR                  0x0002 
#define OERR                  0x0001 
#define RX9D                  0x0000 

/////// WDTCON Bits ////////////////////////////////////////////////////////
#define WDTPS3                0x0004 
#define WDTPS2                0x0003 
#define WDTPS1                0x0002 
#define WDTPS0                0x0001 
#define SWDTEN                0x0000 

/////// OPTION Bits ////////////////////////////////////////////////////////
#define NOT_RAPU              0x0007 
#define INTEDG                0x0006 
#define T0CS                  0x0005 
#define T0SE                  0x0004 
#define PSA                   0x0003 
#define PS2                   0x0002 
#define PS1                   0x0001 
#define PS0                   0x0000 

/////// PIE1 Bits //////////////////////////////////////////////////////////
#define EEIE                  0x0007 
#define ADIE                  0x0006 
#define RCIE                  0x0005 
#define C2IE                  0x0004 
#define C1IE                  0x0003 
#define OSFIE                 0x0002 
#define TXIE                  0x0001 
#define T1IE                  0x0000 
#define TMR1IE                0x0000 

/////// OSCCON Bits ////////////////////////////////////////////////////////
#define IRCF2                 0x0006 
#define IRCF1                 0x0005 
#define IRCF0                 0x0004 
#define OSTS                  0x0003 
#define HTS                   0x0002 
#define LTS                   0x0001 
#define SCS                   0x0000 

#endif //def SDCC


//defaults for well-behaved compilers:
#ifndef SIZEOF
 #define SIZEOF(thing)  sizeof(thing)
#endif

#ifndef RCSTA
 #define INDF  1
 #define FSR  2
 #define TMR0  3
 #define STATUS  4
 #define TRISA  0x85
 #define PORTA  6
 #define ANSELA  7
 #define TRISB  0x88
 #define PORTB  9
 #define ANSELB  10
// #define SPBRGH_ADDR  11
// #define SPBRGL_ADDR  12
// #define CMCON0_ADDR  13
// #define OPTION_REG_ADDR  14
// #define INTCON_ADDR  15
 #define PIR1  16
 #define RCSTA  0x197
#endif

//eof
