////////////////////////////////////////////////////////////////////////////////
////
/// Establish common compile environment
//

#ifndef _COMPILER_H
#define _COMPILER_H

//xc8 automatically select correct device header defs
//#include <xc.h>
//define predictable types;
#include <stdint.h> //uint*_t


//initialize function chains:
//1 chain used for each peripheral event handler + 1 for init + 1 for debug
//init chain is a work-around for compilers that don't support static init
#define init()
//debug chain is a work-around for compiler not expanding expressions in #warning/#error/#pragma messages
//NOTE: debug chain *not* executed at run time
#ifdef debug
 #undef debug
 #define debug()
#endif
//#define on_zc()
//#define on_rx()
//#define on_tmr0()
//#define on_tmr1()


//keywords for readability:
#define VOID
#define non_inline  //syntax tag to mark code that must *not* be inlined (usually due to hard returns or multiple callers)
//NO: generates duplicate code; #define INLINE  extern inline //SDCC needs "extern" for inline functions, else reported as undefined
#define INLINE  inline


//SDCC:
//use "static" to avoid code page overhead
//also, put callee < caller to avoid page selects
//use one source file per code page
#define LOCAL  static //SDCC: avoid code page overhead


////////////////////////////////////////////////////////////////////////////////
////
/// Device headers, settings
//

#ifdef __SDCC
 #warning CYAN_MSG "[INFO] Using SDCC compiler"
// #warning "use command line:  sdcc --use-non-free [-E] -mpic14 -p16f688 -D_PIC16F688 -DNO_BIT_DEFINES -D_PIC16  thisfile.c"
 #define DEVICE  __SDCC_PROCESSOR
// #define inline  //not supported
// #define main  _sdcc_gsinit_startup

//#elif defined(__GNUC__)
// #warning CYAN_MSG "[INFO] Using GCC compiler (test only)"

#else
//TODO: check other compilers here *after* adding any necessary defines elsewhere
 #error RED_MSG "Unsupported/unknown compiler"
#endif


//SDCC:
#ifdef __SDCC_PIC16F1825 //set by cmdline or MPLAB IDE
 #define extern //kludge: force compiler to include defs
 #include <pic14/pic16f1825.h>
 #undef extern
//#ifdef __PIC16F1825_H__
 #define PIC16X //extended instr set
 #define HIGHBAUD_OK  TRUE //PIC16F688 rev A.3 or older should be FALSE; newer can be TRUE
 #define LINEAR_RAM  0x2000
// #define PIC16F1825
 #define _SERIN_PIN  _RC5 //PORTPIN(_PORTC, bit2inx(_RC5)) //0xC5C5 //;serial input pin
 #define _SEROUT_PIN  _RC4 //PORTPIN(_PORTC, bit2inx(_RC4)) //0xC4C4 //;serial output pin
// #define CLKIN_PIN  _RA5 //0xA5 //;ext clock pin; might be available for LED control
// #define _ZC_PIN  PORTPIN(_PORTA, bit2inx(_RA3)) //0xA3  //;this pin can only be used for input; used for ZC/config
 #define PORTA_MASK  0x3f
 #define PORTC_MASK  0x3f

#elif defined(__SDCC_PIC16F688) //set by cmdline or MPLAB IDE
 #define extern //kludge: force compiler to include defs
 #include <pic14/pic16f688.h>
 #undef extern
//#ifdef __PIC16F1825_H__
// #define PIC16X //extended instr set
 #define HIGHBAUD_OK  TRUE //PIC16F688 rev A.3 or older should be FALSE; newer can be TRUE
// #define LINEAR_RAM  0x2000
// #define PIC16F1825
// #define CMCON0_NEEDINIT  7 //;configure comparator inputs as digital I/O (no comparators); overrides TRISC (page 63, 44, 122); must be OFF for digital I/O to work!
 #undef init
 #define init()  { CMCON0 = 7; }
 #define _SERIN_PIN  _RC5 //PORTPIN(_PORTC, bit2inx(_RC5)) //0xC5C5 //;serial input pin
 #define _SEROUT_PIN  _RC4 //PORTPIN(_PORTC, bit2inx(_RC4)) //0xC4C4 //;serial output pin
// #define CLKIN_PIN  _RA5 //0xA5 //;ext clock pin; might be available for LED control
// #define _ZC_PIN  PORTPIN(_PORTA, bit2inx(_RA3)) //0xA3  //;this pin can only be used for input; used for ZC/config
 #define PORTA_MASK  0x3f
 #define PORTC_MASK  0x3f

#else
//TODO: check other processors here *after* adding any necessary defines elsewhere
 #error RED_MSG "Unsupported/unknown target device"
#endif
#define SERIN_MASK  PORTMAP16(_SERIN_PIN)
#define SEROUT_MASK  PORTMAP16(_SEROUT_PIN)


//startup code:
#ifdef __SDCC
void main(void); //fwd ref
//missing startup code
void _sdcc_gsinit_startup(void)
{
//    PCLATH = 0; //reset PCLATH 1x and avoid the need for page selects everywhere else
    main();
}
#endif


////////////////////////////////////////////////////////////////////////////////
////
/// Data, memory (generic defaults; individual devices might override)
//

//group related code according to purpose in order to reduce page selects:
//#define DEMO_PAGE  1 //free-running/demo/test code; non-critical
//#define IOH_PAGE  0 //I/O handlers: WS2811, GECE, chplex, etc; timing-critical
//#define PROTOCOL_PAGE  1 //protocol handler and opcodes; timing-sensitive
//#define LEAST_PAGE  0 //put less critical code in whichever page has the most space (initialization or rarely-used functions)
#define ONPAGE(ignored) //don't need this if everything fits on one code page

//_xdata/__far
//__idata
//__pdata
//__code
//__bit
//__sfr / __sfr16 / __sfr32 / __sbit
//TODO: fix these
#define BANK0  __data //__near
#define BANK1  __data //__near
//TODO: sizes > 1
#define NONBANKED  AT_NONBANKED(__COUNTER__) //__xdata

#define PAGE0  //TODO: what goes here?
#define PAGE1


//memory map within each bank:
//NOTE: PIC uses 7-bit addressing within each bank
#define NONBANKED_SIZE  0x10
#define GPR_SIZE  (BANK_SIZE - SFR_SIZE - NONBANKED_SIZE) //0x50
#define SFR_SIZE  0x20
#define BANK_SIZE  0x80

#define BANK_END  0x80
#define NONBANKED_START  (BANK_END - NONBANKED_SIZE) //0x70
#define GPR_START  (NONBANKED_START - GPR_SIZE) //0x20
#define SFR_START  (GPR_START - SFR_SIZE) //0x00
#define BANK_START  SFR_START
#if BANK_START //!= 0
 #warning RED_MSG "[ERROR] Bank map length incorrect: " TOSTR(BANK_START)
// #error
#endif


//misc address spaces:
#define LINEAR_ADDR(ofs)  (0x2000 + (ofs))
#define BANKED_ADDR(ofs)  (((ofs) / GPR_SIZE) * 0x100 + ((ofs) % GPR_SIZE) + GPR_START) //put bank# in upper byte, bank ofs in lower byte for easier separation of address
#define NONBANKED_ADDR(ofs)  (((ofs) % NONBANKED_SIZE) + NONBANKED_START)
#define AT_LINEAR(ofs)  __at(LINEAR_ADDR(ofs))
#define AT_BANKED(ofs)  __at(BANKED_ADDR(ofs))
#define AT_NONBANKED(ofs)  __at(NONBANKED_ADDR(ofs))


#ifdef COMPILER_DEBUG //debug
 #ifndef debug
  #define debug() //define debug chain
 #endif
//define globals to shorten symbol names (local vars use function name as prefix):
    AT_NONBANKED(0) volatile uint8_t bank_start_debug; //= BANK_START;
    AT_NONBANKED(0) volatile uint8_t sfr_start_debug; //= SFR_START;
    AT_NONBANKED(0) volatile uint8_t gpr_start_debug; //= GPR_START;
    AT_NONBANKED(0) volatile uint8_t nb_start_debug; //= NONBANKED_START;
    AT_NONBANKED(0) volatile uint8_t bank_end_debug; //= BANK_END;
    AT_NONBANKED(0) volatile uint16_t test1_debug; //= AT_BANKED(0x55); //should be 0x105
 INLINE void bank_debug()
 {
    debug(); //incl prev debug info
    bank_start_debug = BANK_START; //should be 0
    sfr_start_debug = SFR_START; //should be 0
    gpr_start_debug = GPR_START; //should be 0x20
    nb_start_debug = NONBANKED_START; //should be 0x70
    bank_end_debug = BANK_END; //should be 0x80
    test1_debug = BANKED_ADDR(0x55); //should be 0x125
 }
 #undef debug
 #define debug()  bank_debug()
#endif


////////////////////////////////////////////////////////////////////////////////
////
/// Pseudo-registers
//


//little endian byte order:
#define LOW_BYTE  0
#define HIGH_BYTE  1

//typedef uint8_t[3] uint24_t;
#define uint24_t  uint32_t //kludge: use pre-defined type and just ignore first byte
typedef union
{
    uint8_t bytes[2]; //little endian: low, high
    struct { uint8_t low, high; };
    uint16_t as_uint16;
    uint8_t* as_ptr;
} uint2x8_t;


//avoid typecast warnings:
//typedef union
//{
//    uint8_t bytes[2];
//    uint8_t low, high;
//    uint16_t as_int;
//    uint8_t* as_ptr;
//} uint16_ptr_t;
#define uint16_ptr_t  uint2x8_t


#ifndef WREG_ADDR
//#undef WREG //#def conflict
 #define WREG_ADDR  NONBANKED_ADDR(0) //0x70
 volatile __at(WREG_ADDR) uint8_t WREG; //dummy reg; will be removed by asm fixup
// volatile bit LEZ @0x70.0; //<= 0
#endif

//dummy flags to keep/remove code:
//(avoids compiler warnings or mishandling of unreachable code)
#if 0
volatile AT_NONBANKED(0)
struct
{
    unsigned NEVER: 1;
} dummy_bits;
#define NEVER  dummy_bits.NEVER
#define ALWAYS  !NEVER
#endif
typedef struct
{
//    unsigned      : 7;
    unsigned NEVER: 1; //lsb
//    unsigned ALWAYS: 1;
    unsigned :7; //unused
} FakeBits_t;
volatile AT_NONBANKED(0) FakeBits_t FAKE_BITS;
//volatile bit /*ALWAYS @0x70.0,*/ NEVER @0x70.0; //dummy flags to keep/remove code
#define NEVER  FAKE_BITS.NEVER
#define ALWAYS  !NEVER


//longer names (for reability/searchability):
#define DCARRY  DC
#define CARRY  C
#define BORROW  !CARRY
#define ZERO  Z


/////////////////////////////////////////////////////////////////////////////////
////
/// Generic port handling (reduces device dependencies):
//

#ifdef PORTB_ADDR
 #define TRISBC  TRISB
 #define TRISBC_ADDR  TRISB_ADDR
 #define PORTBC  PORTB
 #define PORTBC_ADDR  PORTB_ADDR
 #ifdef LATB_ADDR
  #define LATBC  LATB
  #define LATBC_ADDR  LATB_ADDR
 #else
  #define LATBC  PORTB
  #define LATBC_ADDR  PORTB_ADDR
 #endif
 #ifdef WPUB_ADDR
  #define WPUBC  WPUB
  #define WPUBC_ADDR  WPUB_ADDR
  #define IFWPUBC(stmt)  stmt
 #else
  #define IFWPUBC(ignored)  //nop
 #endif
 #ifdef ANSELB_ADDR
  #define ANSELBC  ANSELB
  #define ANSELBC_ADDR  ANSELB_ADDR
  #define IFANSELBC(stmt)  stmt
 #else
  #define IFANSELBC(ignored)  //nop
 #endif
 #define IFPORTBC(stmt)  stmt
 #define IFPORTB(stmt)  stmt
 #define IFPORTC(stmt)  //nop
 #define _PORTBC  0xB0
 #define _PORTB  0xB0
 #define isPORTBC  isPORTB
 #define PORTBC_MASK  PORTB_MASK
#elif defined(PORTC_ADDR)
 #define TRISBC  TRISC
 #define TRISBC_ADDR  TRISC_ADDR
 #define PORTBC  PORTC
 #define PORTBC_ADDR  PORTC_ADDR
 #ifdef LATC_ADDR
  #define LATBC  LATC
  #define LATBC_ADDR  LATC_ADDR
 #else
  #define LATBC  PORTC
  #define LATBC_ADDR  PORTC_ADDR
 #endif
 #ifdef WPUC_ADDR
  #define WPUBC  WPUC
  #define WPUBC_ADDR  WPUC_ADDR
  #define IFWPUBC(stmt)  stmt
 #else
  #define IFWPUBC(ignored)  //nop
 #endif
 #ifdef ANSELC_ADDR
  #define ANSELBC  ANSELC
  #define ANSELBC_ADDR  ANSELC_ADDR
  #define IFANSELBC(stmt)  stmt
 #else
  #define IFANSELBC(ignored)  //nop
 #endif
 #define IFPORTBC(stmt)  stmt
 #define IFPORTC(stmt)  stmt
 #define IFPORTB(stmt)  //nop
 #define _PORTBC  0xC0
 #define _PORTC  0xC0
 #define isPORTBC  isPORTC
 #define PORTBC_MASK  PORTC_MASK
#else
 #warning YELLOW_MSG "[INFO] No Port B/C?"
 #define IFPORTB(stmt)  //nop
 #define IFPORTC(stmt)  //nop
 #define IFPORTBC(stmt)  //nop
 #define IFANSELBC(ignored)  //nop
#endif

//all devices have PORTA?
#ifdef PORTA_ADDR
 #ifndef LATA_ADDR
  #define LATA  PORTA
  #define LATA_ADDR  PORA_ADDR
 #endif
// #ifndef WPUA_ADDR
//  #define WPUBC  WPUC
//  #define WPUBC_ADDR  WPUC_ADDR
// #endif
 #if !defined(ANSELA_ADDR) && defined(ANSEL_ADDR) //create aliases
  #define ANSELA  ANSEL
  #define ANSELA_ADDR  ANSEA_ADDR
  #define IFANSELA(stmt)  stmt
 #else
  #define IFANSELA(ignored)  //nop
 #endif
 #define IFPORTA(stmt)  stmt
 #define _PORTA  0xA0
#else
 #error RED_MSG "[ERROR] No Port A?"
 #define IFPORTA(stmt)  //nop
 #define IFANSELA(ignored)  //nop
#endif


/////////////////////////////////////////////////////////////////////////////////
////
/// Make reg names more consistent:
//

#ifdef SPBRG_ADDR //!defined(SPBRGL_ADDR) && defined(SPBRG_ADDR)
 #define SPBRGL  SPBRG
 #define SPBRGL_ADDR  SPBRG_ADDR
// #warning "SPBRGL equate"
#endif

#ifdef BAUDCTL_ADDR //!defined(BAUDCON_ADDR) && defined(BAUDCTL_ADDR) //create aliases
 #define BAUDCON  BAUDCTL
 #define BAUDCON_ADDR  BAUDCTL_ADDR
#endif

#ifdef INDF_ADDR //!defined(INDF0_ADDR) && defined(INDF_ADDR) //create fsr0/indf0 aliases for fsr/indf
 #define FSR0L  FSR
 #define FSR0H  IRP //CAUTION: 1 bit only
 #define INDF0  INDF
 #define FSR0L_ADDR  FSR_ADDR
// volatile bit FSR0 @STATUS_ADDR.IRP; //CAUTION: 1 bit only
 #define FSR0H_ADDR  IRP_ADDR //NOTE: isLE() must be false!
 #define INDF0_ADDR  INDF_ADDR
// #warning "FSR0/INDF0 equate"
#endif

#ifdef APFCON_ADDR //!defined(APFCON0) && defined(APFCON)
 #define APFCON0  APFCON
 #define APFCON0_ADDR  APFCON_ADDR
#endif

#ifndef OSCSTAT_ADDR
 #define OSCSTAT  OSCCON
 #define OSCSTAT_ADDR  OSCCON_ADDR
#endif

#ifndef EEADRL_ADDR //defined(EEADR) && !defined(EEADRL)
 #define EEADRL  EEADR
 #define EEADRL_ADDR  EEADR_ADDR
#endif

#ifndef EEDATL_ADDR //defined(EEDATA) && !defined(EEDATL)
 #define EEDATL  EEDATA
 #define EEDATL_ADDR  EEDATA_ADDR
#endif

#if !defined(CM1CON0_ADDR) && defined(CMCON0_ADDR) //= ~0; //;configure comparator inputs as digital I/O (no comparators); overrides TRISC (page 63, 44, 122); must be OFF for digital I/O to work! (for PIC16F688)
 #define CM1CON0  CMCON0
 #define CM1CON0_ADDR  CMCON0_ADDR
#endif
#ifdef CM1CON0_ADDR
 #define IFCM1CON0(stmt)  stmt
#else
 #define IFCM1CON0(stmt)  //nop
#endif


#ifndef CFGS
 #define CFGS  0 //no explicit User ID/Device ID space defined
#endif

#define BITADDR(adrs)  (adrs)/8.(adrs)%8 //for use with @ on bit vars
 
//misc bits:
//#ifdef _HTS
// #define HFIOFS  HTS
// #define _HFIOFS  _HTS
//#endif
#ifndef _HFIOFS
 #define HFIOFS  HTS
 #define _HFIOFS  _HTS
#endif

#ifdef _NOT_RAPU
 #define NOT_WPUEN  NOT_RAPU
 #define _NOT_WPUEN  _NOT_RAPU
#endif

#ifdef _TMR1CS
 #define TMR1CS0  TMR1CS
 #define _TMR1CS0  _TMR1CS
#endif

#ifdef _SCS //defined(SCS) && !defined(SCS0)
 #define SCS0  SCS
 #define _SCS0  _SCS
#endif


/////////////////////////////////////////////////////////////////////////////////
////
/// Alternate addressing:
//

//indirect addressing with auto inc/dec:
//the correct opcodes will be inserted during asm fixup
#ifdef PIC16X
__at (INDF0_ADDR) volatile uint8_t INDF0_POSTDEC;
__at (INDF0_ADDR) volatile uint8_t INDF0_PREDEC;
__at (INDF0_ADDR) volatile uint8_t INDF0_POSTINC;
__at (INDF0_ADDR) volatile uint8_t INDF0_PREINC;

__at (INDF1_ADDR) volatile uint8_t INDF1_POSTDEC;
__at (INDF1_ADDR) volatile uint8_t INDF1_PREDEC;
__at (INDF1_ADDR) volatile uint8_t INDF1_POSTINC;
__at (INDF1_ADDR) volatile uint8_t INDF1_PREINC;
#endif


//define composite regs:
//#error CYAN_MSG "TODO: __sfr16 ASM fixups"

//16-bit regs:
#define isLE16(reg)  (reg##L_ADDR + 1 == reg##H_ADDR)

//#define ADDR(reg)  reg##ADDR
#if isLE16(FSR0)
// extern __at(FSR0L_ADDR) __sfr16 FSR0_16;
// static __data uint16_t __at(FSR0L_ADDR) FSR0_16;
// /*extern volatile __data __at(FSR0L_ADDR)*/ uint16_t FSR0_16;
// extern /*volatile*/ __data __at(FSR1L_ADDR) uint16_t FSR1_16;
// static __data uint16_t __at (FSR1L_ADDR) FSR1_16;
// static __at(FSR1L_ADDR) __sfr FSR1_16;
 __at (FSR0L_ADDR) uint16_ptr_t FSR0_16;
 __at (FSR1L_ADDR) uint16_ptr_t FSR1_16;
// volatile uint16_t FSR0_16;
#else
 #warning YELLOW_MSG "[WARNING] FSR0_16 is not little endian/addressable as 16-bit reg"
#endif
// static __code uint16_t __at (_CONFIG2) cfg2 = MY_CONFIG2;

#if isLE16(TMR1)
// extern __at(TMR1L_ADDR) __sfr16 TMR1_16;
// /*extern volatile __data __at(TMR1L_ADDR)*/ uint16_t TMR1_16;
 __at (TMR1L_ADDR) uint16_t TMR1_16;
#else
 #error RED_MSG "[ERROR] TMR1_16 is not little endian/addressable as 16-bit reg"
#endif

#if isLE16(SPBRG)
// extern __at(SPBRGL_ADDR) __sfr16 SPBRG_16;
// /*extern volatile __data __at(SPBRGL_ADDR)*/ uint16_t SPBRG_16;
 __at (SPBRGL_ADDR) uint16_t SPBRG_16;
#else
 #warning YELLOW_MSG "[WARNING] SPBRG_16 is not little endian/addressable as 16-bit reg"
#endif

#if isLE16(EEADR)
// extern __at(SPBRGL_ADDR) __sfr16 SPBRG_16;
// /*extern volatile __data __at(SPBRGL_ADDR)*/ uint16_t SPBRG_16;
 __at (EEADRL_ADDR) uint16_t EEADR_16;
#else
 #warning YELLOW_MSG "[WARNING] EEADR_16 is not little endian/addressable as 16-bit reg"
#endif

#if isLE16(EEDATA) //|| defined(EEDATH)
 __at (EEDATL_ADDR) uint16_t EEDATA_16;
#else
 #warning YELLOW_MSG "[WARNING] EEDATA_16 is not little endian/addressable as 16-bit reg"
#endif


////////////////////////////////////////////////////////////////////////////////
////
/// Pseudo-opcodes
//

//allow access to vars, regs in asm:
//NOTE: needs to be wrapped within macro for token-pasting
#define ASMREG(reg)  _##reg


//put a "label" into compiled code:
//inserts a benign instruction to make it easier to find inline code
//NOTE: won't work if compiler optimizes out the benign instr
volatile AT_NONBANKED(0) uint8_t labdcl;
#define LABDCL(val)  \
{ \
    labdcl = val; /*WREG = val;*/ \
}


//move an instr down in memory:
volatile AT_NONBANKED(0) uint8_t swopcode;
#define SWOPCODE(val)  \
{ \
    swopcode = val; \
}


//no- examine code instead
//tell asm fixup how many banks to use (helps reduce banksel overhead):
//volatile AT_NONBANKED(0) uint8_t numbanks;
//#define NUMBANKS(val)  
//{ 
//    numbanks = val; 
//}

//actual opcodes:
INLINE void nop()
{
   __asm;
    nop; __endasm;
}

INLINE void idle()
{
   __asm;
    sleep; __endasm;
}

//SDCC doesn't seem to know about andlw, iorlw, xorlw, addlw, sublw on structs, so need to explicitly use those opcodes:
//NOTE: need to use macro here to pass const down into asm:
#define addlw(val)  \
{ \
    __asm; \
    addlw val; __endasm; \
}
#define sublw(val)  \
{ \
    __asm; \
    sublw val; __endasm; \
}
#define andlw(val)  \
{ \
    __asm; \
    andlw val; __endasm; \
}
#define iorlw(val)  \
{ \
    __asm; \
    iorwl val; __endasm; \
}
#define xorlw(val)  \
{ \
    __asm; \
    xorlw val; __endasm; \
}
#define retlw(val)  \
{ \
    __asm; \
    retlw val; __endasm; \
}

#define incfsz(...)  USE_ARG3(__VA_ARGS__, incfsz_2ARGS, incfsz_1ARG) (__VA_ARGS__)
#define incfsz_1ARG(val)  incfsz_2ARGS(val, F) //update target reg
#define incfsz_2ARGS(val, dest)  \
{ \
    __asm; \
    incfsz val, dest; __endasm; \
}
#define incf(...)  USE_ARG3(__VA_ARGS__, incf_2ARGS, incf_1ARG) (__VA_ARGS__)
#define incf_1ARG(val)  incf_2ARGS(val, F) //update target reg
#define incf_2ARGS(val, dest)  \
{ \
    __asm; \
    incf val, dest; __endasm; \
}
#define addwf(...)  USE_ARG3(__VA_ARGS__, addwf_2ARGS, addwf_1ARG) (__VA_ARGS__)
#define addwf_1ARG(val)  addwf_2ARGS(val, F) //update target reg
#define addwf_2ARGS(val, dest)  \
{ \
    __asm; \
    addwf val, dest; __endasm; \
}
#define subwf(...)  USE_ARG3(__VA_ARGS__, subwf_2ARGS, subwf_1ARG) (__VA_ARGS__)
#define subwf_1ARG(val)  subwf_2ARGS(val, F) //update target reg
#define subwf_2ARGS(val, dest)  \
{ \
    __asm; \
    subwf val, dest; __endasm; \
}
#define addwfc(...)  USE_ARG3(__VA_ARGS__, addwfc_2ARGS, addwfc_1ARG) (__VA_ARGS__)
#define addwfc_1ARG(val)  addwfc_2ARGS(val, F) //update target reg
#ifdef PIC16X
 #define addwfc_2ARGS(val, dest)  \
 { \
    __asm; \
    addwfc val, dest; __endasm; \
 }
#else
 #define addwfc_2ARGS(val, dest)  \
 { \
    if (CARRY) WREG += 1; /*need to add Carry separately; preserve WREG value and leave Carry set correctly afterward*/ \
    __asm; \
    addwf val, dest; __endasm; \
 }
#endif
#define andw(...)  USE_ARG3(__VA_ARGS__, andwf_2ARGS, andwf_1ARG) (__VA_ARGS__)
#define andwf_1ARG(val)  andwf_2ARGS(val, F) //update target reg
#define andwf_2ARGS(val, dest)  \
{ \
    __asm; \
    andwf val, dest; __endasm; \
}
#define iorwf(...)  USE_ARG3(__VA_ARGS__, iorwf_2ARGS, iorwf_1ARG) (__VA_ARGS__)
#define iorwf_1ARG(val)  iorwf_2ARGS(val, F) //update target reg
#define iorwf_2ARGS(val, dest)  \
{ \
    __asm; \
    iorwf val, dest; __endasm; \
}
#define xorwf(...)  USE_ARG3(__VA_ARGS__, xorwf_2ARGS, xorwf_1ARG) (__VA_ARGS__)
#define xorwf_1ARG(val)  xorwf_2ARGS(val, F) //update target reg
#define xorwf_2ARGS(val, dest)  \
{ \
    __asm; \
    xorwf val, dest; __endasm; \
}


//prevent SDCC from using temps for addressing:
//#define adrs_low_WREG(reg) 
//{ 
//    __asm; 
//    MOVLW low(ASMREG(reg)); 
//    __endasm; 
//}
//#define adrs_high_WREG(reg) 
//{ 
//    __asm; 
//    MOVLW high(ASMREG(reg)); 
//    __endasm; 
//}

//minimum of 2 values:
//a - (a - b) == b
//b + (a - b) == a
//if one value is an expr, put it first so it is only evaluated 1x
//consr or vars allowed as second arg
//    WREG += -b; 
#define min_WREG(a, b)  \
{ \
    WREG = a - b; \
    if (!BORROW) WREG = b; /*CAUTION: don't change BORROW here*/ \
    if (BORROW) WREG += b; /*restore "a" without recalculating*/ \
}


//rotate left, don't clear Carry:
//inline void rl_nc(uint8& reg)
//    __asm__("rlf _reg, F");
//LSLF is preferred over RLF; clears LSB
//need to use a macro here for efficient access to reg (don't want another temp or stack)
//#define rl_nc(reg)  
#define lslf(...)  USE_ARG3(__VA_ARGS__, lslf_2ARGS, lslf_1ARG) (__VA_ARGS__)
#define lslf_1ARG(val)  lslf_2ARGS(val, F) //update target reg
#define lslf_2ARGS(reg, dest)  \
{ \
    __asm; \
    lslf reg, dest; __endasm; \
}
#if 0 //broken: need to pass reg by ref
INLINE void rl_nc(uint8_t reg)
{
    __asm
    rlf _##reg, F;
    __endasm;
}
#endif
#if 0 //macro to look like inline function
INLINE VOID rl_nc(reg)  \\
 { \\
    __asm \\
    rlf _##reg, F; \\
    __endasm; \\
 }
#endif
//NOTE: use CARRY as-is; caller set already
#define rlf(...)  USE_ARG3(__VA_ARGS__, rlf_2ARGS, rlf_1ARG) (__VA_ARGS__)
#define rlf_1ARG(val)  rlf_2ARGS(val, F) //update target reg
#define rlf_2ARGS(reg, dest)  \
{ \
	__asm; \
    rlf reg, dest; __endasm; \
}


#if 0
 #define swap(reg)  __asm__(" swapf " #reg ",F")
 #define swap_WREG(reg)  __asm__(" swapf " #reg ",W")
#else
 #define swap(...)  USE_ARG3(__VA_ARGS__, swap_2ARGS, swap_1ARG) (__VA_ARGS__)
 #define swap_1ARG(val)  swap_2ARGS(val, F) //update target reg
 #define swap_2ARGS(reg, dest)  \
 { \
	__asm; \
    swapf reg, dest; __endasm; \
 }
//#define swap_WREG(reg)  
//{ 
//	__asm; 
//    swapf reg, W; __endasm; 
//}
#endif


//turn a bit off + on:
//need to use macro for efficient access to reg as lvalue
#define Toggle(ctlbit)  \
{ \
	ctlbit = FALSE; \
	ctlbit = TRUE; \
}


//set fsr high + low to address:
#define Indirect(fsr, addr)  \
{ \
    fsr##L = (addr) & 0xff; \
    fsr##H = (addr) / 0x100; /*CAUTION: 1 bit on 16F688*/ \
}

#ifdef COMPILER_DEBUG //debug
 #ifndef debug
  #define debug() //define debug chain
 #endif
//define globals to shorten symbol names (local vars use function name as prefix):
    AT_NONBANKED(0) volatile uint8_t serin_debug; //= _SERIN_PIN;
    AT_NONBANKED(0) volatile uint8_t serout_debug; //= _SEROUT_PIN;
//    AT_NONBANKED(0) volatile uint16_t zcpin_debug; //= _ZC_PIN;
 INLINE void device_debug(void)
 {
    debug(); //incl prev debug info
//use 16 bits to show port + pin:
    serin_debug = _SERIN_PIN;
    serout_debug = _SEROUT_PIN;
//    zcpin_debug = _ZC_PIN;
 }
 #undef debug
 #define debug() device_debug()
#endif


#include "helpers.h"

#endif //ndef _COMPILER_H
//eof
