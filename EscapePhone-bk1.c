//WS281X-compatible SSR controller:
//Serial in daisy chained from WS281X data line (800 KHz).
//8 parallel out controlling AC or DC SSRs (dedicated or chipiplexed)
//This code makes use of the following IP:
//Adaptive ZC/DC detection (C) 2010-2017 djulien
//Super-PWM loop (C) 2010-2017 djulien
//ASM-fixup script (C) 2010-2017 djulien
//WS281X 5-8 MIPS PIC bit banging (C) 2014-2017 djulien
//Chipiplexing encoder (C) 2014-2017 djulien
//TODO: put complete history here, add this SDCC/WS281X transport rewrite
//12/8/17 DJ move Ch*plexing encoder onto PIC

#define CLOCK_FREQ  (32 MHz) //max osc freq (with PLL); PIC will use int osc if ext clock is absent
//#define CLOCK_FREQ  18432000 //20 MHz is max ext clock freq for older PICs; comment this out to run at max int osc freq; PIC will use int osc if ext clock is absent
//#define Timer0_range  (100 usec)
//#define Timer1_halfRange  (50 msec/2) //kludge: BoostC gets /0 error with 50 msec (probably 16-bit arith overflow), so use a smaller value
//receive at 5/6 WS281X bit rate (which is 800 KHz); this gives 2 bytes per WS281X node (8 data bits + 1 start/stop bit per byte)
#define BAUD_RATE  (8 MHz / 12) ///10 * 5/6) //no-needs to be a little faster than 3x WS281X data rate (2.4 MHz), or a multiple of it; this is just about the limit for an 8 MIPS PIC16X
// 2/3 Mbps => 15 usec/char; can rcv 2 bytes/WS281X node
//#define BAUD_RATE  (10 MHz) //needs to align with visual bit-banging from GPU; //be a little faster than 3x WS281X data rate (2.4 MHz); this is just about the limit for an 8 MIPS PIC16X
#define REJECT_FRERRS //don't process frame errors (corrupted data could mess up display)
#define NUMSLOTS  256 //256 dimming slots [0..255] (8 bits) ~= 32.5 usec per slot @ 60 Hz AC
#define NUM_SSR  8 //#IO pins for chipiplexed/charlieplexed SSRs; 8 gives 8 * 7 == 56 channels
//#define ALL_OFF  0 //for active low (Common Anode) SSRs, use 0xFF or ~0; for active high (Common Cathode), use 0
//#define RGB_FMT  0x123 //set this to 0x213 if WS281X on front panel for R <-> G swap
#define debug  //include compile-time value checking


//#include <xc.h> //automatically selects correct device header defs
#include "helpers.h"
#include "compiler.h"
#include "clock.h" //override default clock rate
#include "config.h"
#include "timer_dim.h"
//#include "timer_50msec.h"
//#include "timer_1sec.h"
//#include "wdt.h" //TODO?
#include "serial.h"
#include "zc.h"
//#include "outports.h"


#if 0
struct BitVar1
{
    uint8_t bitvar1: 1;
} bits1;
struct BitVar2
{
    struct BitVar1 other;
    uint8_t bitvar2: 1;
} bits2;
struct BitVar3
{
    struct BitVar2 other;
    uint8_t bitvar3: 1;
} bits3;
#define bitvar3  bits3.bitvar3
#define bitvar2  bits3.other.bitvar2
#define bitvar1  bits3.other.other.bitvar1
#endif
//#define test()
//INLINE void test(void)
//{
//    LABDCL(0xff);
//    FSR0_16.as_int16 = AT_LINEAR(0);
//    FSR0L = AT_LINEAR(1) % 0x100;
//    FSR0H = AT_LINEAR(2) / 0x100;
//}

#if 0 //def debug
//extern __at(0x0006) __sfr FSR1L;
// volatile BANK0 uint8_t myreg[2];
 volatile BANK0 union
 {
    uint8_t bytes[2];
    uint8_t value;
 } myreg;
 INLINE void test_debug(void)
 {
    debug(); //incl prev debug info
    WREG = 0x12;
    myreg.bytes[0] += WREG;
    myreg.value += WREG;
//    myreg[0] |= WREG;
//    myreg[0] &= WREG;
//    myreg[0] ^= WREG;
//    myreg[0] -= WREG;
 }
 #undef debug
 #define debug()  test_debug()
#endif


////////////////////////////////////////////////////////////////////////////////
////
/// Memory layout
//


#define FRBUF_LEN  (3 * (NUM_SSR * (NUM_SSR - 1) + 1) + 1) //172 bytes
//TODO: for 12F1840:
//#define NUM_SSR  4
//#define FRBUF_LEN  (2 * (NUM_SSR * (NUM_SSR - 1) + 1)) //26 bytes

//TODO: consolidate bit vars, use non-banked
//TODO: move other bank0 vars to non-banked, use banked for frbufs only


//overall memory layout:
//NOTE: hard-coded for PIC16F1825 with 1K RAM
//0x23f0..0x23ff = reserved for non-banked ram; 16 bytes
//0x2344..0x23ef = second frame buf; length 3 * (8 * 7 + 1) = 171 bytes
//nope-0x229a..0x2344 = first frame buf (overlaid with fifo); length 169 bytes
//0x2244..0x22ef = first frame buf; length 171 bytes; NOTE: use same bottom byte of address to reduce addressing overhead
//nope-0x2036..0x229d = encoder buf; length 11 * 8 * 7 = 616 bytes
//0x2000..0x2035 = misc banked vars; length 54 bytes
//start at end and work backwards to leave lower addresses for banked addressing
//this also allows sizes to be defined on same line as item (reduce coupling between src lines)
#define RAM_END  (1024 - NONBANKED_SIZE) //exclude non-banked RAM
//#define FRBUF_START  (RAM_END - 2 * MAX(2 * NUM_SSR * NUM_SSR, 3 * NUM_SSR * (NUM_SSR - 1) + 1)) //always uses indirect addressing so put it at end and leave lower addresses for direct (banked) addressing
#define FRBUF1_START  (RAM_END - FRBUF_LEN) //always uses indirect addressing so put it at end and leave lower addresses for direct (banked) addressing
#define FRBUF0_START  (FRBUF1_START - 0x100) //always uses indirect addressing so put it at end and leave lower addresses for direct (banked) addressing
//NO-#define FRBUF0_START  (FRBUF1_START - 3 * (NUM_SSR * (NUM_SSR - 1) + 1)) //always uses indirect addressing so put it at end and leave lower addresses for direct (banked) addressing
//#define FRBUF_START  (RAM_END - (3 * NUM_SSR * (NUM_SSR - 1) + 1)) //always uses indirect addressing so put it at end and leave lower addresses for direct (banked) addressing
//#define ENC_START  (FRBUF_START - 11 * NUM_SSR * (NUM_SSR - 1)) //encoder also uses indirect addressing
#define MISC_VARS  (FRBUF0_START - 16) //TODO: expand this; only used to verify there is enough left for other purposes
#define RAM_LOWEST  MISC_VARS
#if RAM_LOWEST < 0
 #error RED_MSG "[ERROR] Memory map overflow"
#else
 #warning BLUE_MSG "[INFO] Estimated remaining RAM available: " TOSTR(RAM_LOWEST)
#endif
//addressing logic assumes the following (makes frbuf addressing more efficient):
#if LINEAR_ADDR(FRBUF0_START) % 0x100 != LINEAR_ADDR(FRBUF1_START) % 0x100
 #error RED_MSG "[ERROR] Bottom address byte of frbufs don't match"
#endif
#if LINEAR_ADDR(FRBUF0_START) / 0x100 != LINEAR_ADDR(FRBUF0_START + FRBUF_LEN) / 0x100
 #error RED_MSG "[ERROR] Frbuf must lie within 256 byte address"
#endif


#ifdef debug
 volatile AT_NONBANKED(0) uint16_t ram_end_debug;
 volatile AT_NONBANKED(0) uint16_t frbuf1_start_debug;
 volatile AT_NONBANKED(0) uint16_t frbuf0_start_debug;
 volatile AT_NONBANKED(0) uint16_t frbuf_len_debug;
 volatile AT_NONBANKED(0) uint16_t frbuf_ents_debug;
 volatile AT_NONBANKED(0) uint16_t misc_start_debug;
 volatile AT_NONBANKED(0) uint16_t ram_lowest_debug;
 INLINE void memory_debug(void)
 {
    debug(); //incl prev debug
    ram_end_debug = RAM_END;
    frbuf1_start_debug = FRBUF1_START;
    frbuf0_start_debug = FRBUF0_START;
    frbuf_len_debug = FRBUF_LEN;
    frbuf_ents_debug = NUM_SSR * (NUM_SSR - 1);
    misc_start_debug = MISC_VARS;
    ram_lowest_debug = RAM_LOWEST;
 }
 #undef debug
 #define debug()  memory_debug()
#endif


//Frame buffers:
//NOTE: this is for reference purposes only; code uses indirect byte addressing rather than the symbols below
/*data*/
volatile AT_LINEAR(FRBUF0_START) //uses linear addressing to simplify address arithmetic
//union
//{
//    uint8_t FIFO[2 * NUM_SSR * NUM_SSR]; //NOTE: includes ch*plexing diagonal; wastes a little space, but makes addressing simpler
struct //I/O pin display list
{
    uint8_t delay; //#timer ticks until next I/O pin update
    uint8_t rowmap; //bitmap of row pins
    uint8_t colmap; //bitmap of column pins
//    uint8_t dimrows[NUM_ROWPAIRS]; //chipiplexed row index for each dimming slot (3 bits + flag bit each)
//    uint8_t dimcols[NUMSLOTS]; //chipiplexed columns for each dimming slot
} frbuf[2][NUM_SSR * (NUM_SSR - 1) + 1]; //NOTE: one extra entry used for eof marker
//} buffers[2]; //double buffered (input + output); shares space with input FIFO
//#define FIFO(n)  buffers[n].FIFO
//#define displist(n)  buffers[n].displist

//set up indirect addressing:
//NOTE: must always use indirect addressing on buffers due to linear address space (compiler complains/mishandles about banking)
//NOTE: macros used here so arg can be a const
//don't need this if bottom byte of address is the same: if (which) WREG = AT_LINEAR(FRBUF1_START) % 0x100; /*&frbuf[1][0].delay*/
//PERF: 6 instr (0.75 usec)
#define frbuf_indirect(reg16, which, ofs)  \
{ \
    WREG = LINEAR_ADDR(FRBUF0_START + ofs) / 0x100; /*&frbuf[0][0].delay*/ \
    /*CAUTION: bcf instr might be inserted here during front panel I/O*/ \
    if (which) WREG = LINEAR_ADDR(FRBUF1_START + ofs) / 0x100; /*&frbuf[1][0].delay*/ \
    reg16.high = WREG; \
    /*CAUTION: bcf instr might be inserted here during front panel I/O*/ \
    WREG = LINEAR_ADDR(FRBUF0_START + ofs) % 0x100; /*&frbuf[0][0].delay*/ \
    reg16.low = WREG; \
}

//row pair:
//INLINE void rowpair_indirect0(void)
//#define rowpair_indirect0(inx)  
//{ 
//    FSR0_16.as_ptr = &buffers[fbactive].dimrows[0]; 
//	FSR0_16.as_int += slot >> 1; 
//}
//column:
//INLINE void col_indirect0(void)
//#define col_indirect0(slot)  
//{ 
//    FSR0_16.as_ptr = &buffers[fbactive].dimcols[0]; 
//	FSR0_16.as_int += slot; 
//}


//#define IN_FIRST  1
//#define OUT_FIRST  0
//NOTE: "fbactive" doens't need to be initialized - code should work either way
//#if 1 //kludge: reclaim unused front panel bit
// #define fbactive  FrontPanel.fbactive
//#else
//BANK0 volatile union
//{
//    struct
//    {
//        uint8_t unused: 7;
//        uint8_t fbactive: 1; //which buffer is active (output); other one is input
//    } bits;
//    uint8_t allbits;
//} buf_state;
//#endif


////////////////////////////////////////////////////////////////////////////////
////
/// Front panel (status LEDs); not essential, but helpful for dev/debug and troubleshooting
//

//clock pin is unused, so connect it to some WS281X for use as front panel:
#define FRPANEL_PIN  RA5
#define _FRPANEL_PIN  PORTPIN(_PORTA, bit2inx(_RA5))

//front panel info to display:
//TODO: consolidate bits
//BANK0 union
//{
volatile BANK0 struct
{
    uint8_t debug; //: 3; //0..7; kludge: give it a separate address so SDCC won't add extra opcodes to process it
    struct
    {
        uint8_t comm_badpkt: 1; //remember serial in packet errors
//        uint8_t comm_uart: 1; //remember parity or frame errors
//        uint8_t zc_present: 1; //zc present
//        uint8_t zc_60hz: 1; //zc >= 55 Hz
        uint8_t fbactive: 1; //unused: 1; //kludge: reclaim unused bit for frame buffer swapping
    } parts;
//    uint8_t all;
//    uint8_t disp_state;
} FrontPanel;
#define fbactive  FrontPanel.parts.fbactive //kludge: reclaim unused front panel bit
#define comm_badpkt  FrontPanel.parts.comm_badpkt
//BANK0 uint8_t fbactive: 1; //unused: 1; //kludge: reclaim unused bit for frame buffer swapping

#if 0 //SDCC generates poor code for structs
BANK0 union
{
    struct
    {
        uint8_t comm: 3; //SDCC: lsb
        uint8_t zc: 3;
        uint8_t debug: 3;
        uint8_t unused: 7; //SDCC: msb
    } parts; //which colors to send
    uint2x8_t all;
} frpanel_colors;
//    BANK0 uint8_t loop;
#else
volatile BANK0 uint8_t frp_colors[2];
#endif


//test front panel + 1 sec timer:
INLINE void heartbeat_debug(void)
{
    ++FrontPanel.debug;
}
#undef on_tmr1
#define on_tmr1()  heartbeart_debug()


//clear sticky status bits prior to re-discovery:
INLINE void FrontPanel_reset(void)
{
//reset status flags (sticky):
//trade-off: maintainability vs. #instr; only 2 instr different, so favor maintainability
//    FrontPanel.all &= 0xF0; //leave fbactive and debug value intact
//NOTE: zc static is sticky and timer0 uses it for preset; don't reset here!
//no    FrontPanel.parts.zc_60hz = 0;
//no    FrontPanel.parts.zc_present = 0;
//    FrontPanel.parts.comm_uart = 0;
    serial_frerr = FALSE;
    serial_overr = FALSE;
    comm_badpkt = FALSE;
}


//send a WS281X "0" or "1" data bit:
//use macros so "nop" can be replaced with useful code
//bits are hard-coded, so this only uses 20% CPU (unconditional, 2 transitions per bit)
//TODO: add assert() in here
//2/3/5 instr split @ 8 MIPS conforms to WS281X timing spec of 1.25 usec per bit (30 usec per node)
//#if 1
#define SPARE_NOPS  8
//TODO: change these to use INDF0 to relax banksel restrictions
#define send_0(nop8)  \
{ \
    FRPANEL_PIN = 1; /*start bit (2 instr)*/ \
    SWOPCODE(+1); /*move following instr down 1 word at assembly time*/ \
    FRPANEL_PIN = 0; /*data + stop bit (8 instr)*/ \
    nop8; \
}
#define send_1(nop8)  \
{ \
    FRPANEL_PIN = 1; /*start + data bit (5 instr)*/ \
    SWOPCODE(+4); /*move following instr down 4 words at assembly time*/ \
    FRPANEL_PIN = 0; /*stop bit (5 instr)*/ \
    nop8; \
}
//#else
//#define SPARE_0  7
//#define SPARE_1  4
//#define send_0(padding7)  
//{ 
//    FRPANEL_PIN = 1; NOP(1); /*start bit (2 instr)*/ 
//    FRPANEL_PIN = 0; padding7; /*data + stop bit (8 instr); NOTE: call/return uses 4 instr*/ 
//}
//#define send_1(padding4)  
//{ 
//    FRPANEL_PIN = 1; NOP(4); /*start + data bit (5 instr)*/ 
//    FRPANEL_PIN = 0; padding4; /*stop bit (5 instr); NOTE: call/return uses 4 instr*/ 
//}
//#endif


//RGB color components:
//only primary RGB colors are needed, so each bit here represents 8 output bits
//#define RGB_FMT  0x123 //set this to 0x213 if WS281X on front panel for R <-> G swap
#define BLACK_BYTES  0b000
//#if RGB_FMT == 0x123
// #define RED_BYTES  0b100
// #define GREEN_BYTES  0b010
 #define BLUE_BYTES  0b001
 #define YELLOW_BYTES  0b110
// #define CYAN_BYTES  0b011
// #define MAGENTA_BYTES  0b101
//allow caller to select R <-> G swap on these:
volatile BANK0 uint8_t RED_BYTES, GREEN_BYTES, CYAN_BYTES, MAGENTA_BYTES;
//#elif RGB_FMT == 0x213
// #define RED_BYTES  0b010
// #define GREEN_BYTES  0b100
// #define BLUE_BYTES  0b001
// #define YELLOW_BYTES  0b110
// #define CYAN_BYTES  0b101
// #define MAGENTA_BYTES  0b011
//#else
// #error RED_MSG "[ERROR] Unknown/unhandled RGB format: " TOSTR(RGB_FMT)
//#endif
#define WHITE_BYTES  0b111


#if 0
//send next byte of a color:
//CARRY selects whether to send "1"s or "0"s
//only primary RGB colors are supported
//NOTE: this adds 4 instr overhead (call/return)
non_inline void send_byte(void)
{
    send_0(3); //allow room for branch at end of first bit
    if (CARRY) //send "1"s (actually only 4)
    {
        nop(); //make execution path uniform
        REPEAT(3, send_0(0)); //limit brightness + power consumption by setting some bits low
        REPEAT(8-5, send_1(0));
        send_1(4); //compensate for call/return during last "1" bit
        return;
    }
    REPEAT(8-1, send_0(0));
    send_0(4); //compensate for call/return during last "0" bit
}

non_inline void send_black(void)
{
    REPEAT(3*8-1, send_0(0));
    send_0(4); //compensate for call/return during last bit
}
non_inline void send_red(void)
{
    REPEAT(8, send_1(0));
    REPEAT(2*8-1, send_0(0));
    send_0(4); //compensate for call/return during last bit
}
non_inline void send_green(void)
{
    REPEAT(8, send_0(0));
    REPEAT(8, send_1(0));
    REPEAT(8, send_0(4));
    send_0(4); //compensate for call/return
}
non_inline void send_blue(void)
{
    REPEAT(2*8, send_0(0));
    REPEAT(8-1, send_1(0));
    send_0(4); //kludge: make last bit 0 to free up consolidate available instr()
}
non_inline void send_yellow(void)
{
    REPEAT(2*8, send_1);
    REPEAT(8, send_0);
}
non_inline void send_cyan(void)
{
    REPEAT(8, send_0);
    REPEAT(2*8-1, send_1);
    send_0(); //kludge: make last bit 0 to free up consolidate available instr()
}
non_inline void send_magenta(void)
{
    REPEAT(8, send_1);
    REPEAT(8, send_0);
    REPEAT(8-1, send_1);
    send_0(); //kludge: make last bit 0 to free up consolidate available instr()
}
non_inline void send_white(void)
{
    REPEAT(3*8-1, send_1);
    send_0(); //kludge: make last bit 0 to free up consolidate available instr()
}

//color indices:
//NOTE: must match order of jump table below
//TODO: R <-> G swap?
#define BLACK_INX  0
#define RED_INX  1
#define GREEN_INX  2
#define BLUE_INX  3
#define YELLOW_INX  4
#define CYAN_INX  5
#define MAGENTA_INX  6
#define WHITE_IXN  7

//NOTE: this adds 12 instr overhead
non_inline void send_color(uint8_t color_WREG)
{
    WREG = (color_WREG << 1) & 0xF;
    PCL += WREG; //GOTOC; CAUTION: following jump tbl must all be within same 256 code block
    send_black(); return;
    send_red(); return;
    send_green(); return;
    send_blue(); return;
    send_yellow(); return;
    send_cyan(); return;
    send_magenta(); return;
    send_white(); return;
}
#endif

//use macro for more efficient parameter handling:
#define rgswap(want_swap)  \
{ \
    if (want_swap) { RED_BYTES = 0b100; GREEN_BYTES = 0b010; CYAN_BYTES = 0b011; MAGENTA_BYTES = 0b101; } \
    else { RED_BYTES = 0b010; GREEN_BYTES = 0b100; CYAN_BYTES = 0b101; MAGENTA_BYTES = 0b011; } \
}


//;initialize front panel data:
INLINE void frpanel_init(void)
{
	init(); //prev init first
    LABDCL(0xF0);
//	FrontPanel.comm_uart = 0;
//    FrontPanel.comm_badpair = 0;
//    FrontPanel.zc_present = 0;
//    FrontPanel.zc_60hz = 0;
//    FrontPanel.debug = 0;
//    FrontPanel.all = 0;
    FrontPanel_reset();
//    FrontPanel.disp_state = 0;
    rgswap(FALSE); //assume RGB format until caller overrides
}
#undef init
#define init()  frpanel_init() //function chain in lieu of static init


void frpanel_refresh(void); //fwd ref

//refresh front panel display:
INLINE void on_zc_tick_frpanel(void)
{
	on_zc_rise(); //prev event handlers first
//	on_zc_rise(); //prev event handlers first
    LABDCL(0xF1);

//choose all colors *before* start sending:
//this relaxes timing restrictions, and allows one simple loop for sending
//    frpanel_colors.all.as_int16 = 0; //SDCC is generating & instr, so just clear it 1x at start instead
    frp_colors[0] = frp_colors[1] = 0;
//first pixel: blue == none, green == good, red == frame error, yellow == overflow error, pink == checksum error
//    frpanel_colors.parts.comm = BLUE_BYTES; //no data
//    FSR0_16.as_ptr = &frbuf[fbactive][0].delay;
//    adrs_low_WREG(frbuf[0][0].delay);
//    WREG = AT_LINEAR(FRBUF0_START) % 0x100;
//    if (fbactive) WREG = AT_LINEAR(FRBUF1_START) % 0x100; //adrs_low_WREG(frbuf[1][0].delay);
//    FSR0L = WREG;
//    adrs_high_WREG(frbuf[0][0].delay);
//    WREG = AT_LINEAR(FRBUF0_START) / 0x100;
//    if (fbactive) WREG = AT_LINEAR(FRBUF1_START) / 0x100; //adrs_high_WREG(frbuf[1][0].delay);
//    FSR0H = WREG;
    frbuf_indirect(FSR0_16, fbactive, 2);
#warning RED_MSG "[ERROR]: 3x SDCC bug: doesn't treat \"WREG\" as \"W\" here, trashes WREG when copying to (unneeded) temp"
//    if (INDF0) frpanel_colors.parts.comm = GREEN_BYTES;
//    frpanel_colors.parts.comm = 0;
//    WREG = INDF0; if (!ZERO) frpanel_colors.parts.comm = GREEN_BYTES;
    WREG = INDF0; //pre-set ZERO
    WREG = BLUE_BYTES; //no data
    if (!ZERO) WREG = GREEN_BYTES;
    if (serial_overr) WREG = YELLOW_BYTES; //frpanel_colors.parts.comm = YELLOW_BYTES;
    if (serial_frerr) WREG = RED_BYTES; //frpanel_colors.parts.comm = RED_BYTES;
    if (comm_badpkt) WREG = MAGENTA_BYTES; //frpanel_colors.parts.comm = MAGENTA_BYTES;
//NOTE: SDCC is generating bad code here, so use explicit opcodes:
//    frpanel_colors.parts.comm = WREG;
//    frpanel_colors.all.low |= WREG;
//    iorwf(_frpanel_colors + 0); //kludge: force SDCC to use correct opcodes
    iorwf(_frp_colors + 0); //[0] |= WREG;
//    swap(pixels);
//second pixel: blue == none, green == 60 Hz AC, cyan == 50 Hz AC
//    WREG = RED_INX;
//    frpanel_colors.parts.zc = BLUE_BYTES;
//    frpanel_colors.parts.zc = 0;
    WREG = BLUE_BYTES;
    if (zc_present) WREG = CYAN_BYTES; //frpanel_colors.parts.zc = CYAN_BYTES;
    if (zc_60hz) WREG = GREEN_BYTES; //frpanel_colors.parts.zc = GREEN_BYTES;
//    frpanel_colors.parts.zc = WREG;
//    frpanel_colors.all.low |= WREG;
//    iorwf(_frpanel_colors + 0); //kludge: force SDCC to use correct opcodes
    WREG += WREG; WREG += WREG; WREG += WREG; //<< 3
    iorwf(_frp_colors + 0); //[0] |= WREG;
    LABDCL(0xf2);
//    pixels[0] |= WREG;
//third pixel: set color according to debug code
//    frpanel_colors.parts.debug = FrontPanel.parts.debug;
//    WREG = (FrontPanel.parts.debug /*& 7&/) << 6;
//    WREG = FrontPanel.parts.debug
//NOTE: R <-> G swap will not be applied to debug value
    swap_WREG(_FrontPanel + 0); andlw(0xF0); //<< 4
    WREG += WREG; WREG += WREG; //<< 6
//    frpanel_colors.parts.debug = WREG;
//    frpanel_colors.parts.unused = 0;
//    iorwf(_frpanel_colors + 0); //kludge: force SDCC to use correct opcodes
    iorwf(_frp_colors + 0); //[0] |= WREG;
    if (CARRY) incf(_frp_colors + 1); //++frpanel_colors.all.high; //kludge: tell SDCC how to handle this
//4th pixel: show first SSR value (bottom 3 bits)
    WREG = INDF0; //re-load first byte of display list
    WREG += WREG; //<< 1
    iorwf(_frp_colors + 1);
//now send data to WS281X (timing critical)
    frpanel_refresh(); //moved front panel refresh below FIFO defs so inline enqueue() logic could be reused
}
#undef on_zc_rise
#define on_zc_rise()  on_zc_tick_frpanel() //event handler function chain


////////////////////////////////////////////////////////////////////////////////
////
/// Output ports (chipiplexed SSRs)
//

//BANK0 uint16_t outptr9; //need >= 9 bits (8 bits for BUFLEN + 1 bit to select between buffers)
//BANK0 int_or_ptr_t outptr9; //need >= 9 bits (8 bits for BUFLEN + 1 bit to select between buffers)
//BANK0 uint8_t phase_angle; //keep track of where we are in AC/DC dimming cycle
//chipiplexing buffers
volatile BANK0 uint8_t portbuf[2];
/*data*/
volatile NONBANKED uint8_t trisbuf[2]; //NOTE: non-banked to reduce bank selects during output
volatile BANK0 uint16_ptr_t outptr; //ptr to next display event
volatile BANK0 uint8_t evtdelay; //remaining duration for current display event
//BANK0 uint8_t ALL_OFF; //for active low (Common Anode) SSRs, use 0xFF or ~0; for active high (Common Cathode), use 0

BANK0 volatile union
{
    struct
    {
        uint8_t unused: 5; //lsb
        uint8_t RGswap: 1;
        uint8_t VerifyChksum: 1;
        uint8_t ActiveHigh: 1; //msb
    } bits;
    uint8_t allbits;
} ssr_cfg;
#define isActiveHigh  ssr_cfg.bits.ActiveHigh //!ALL_OFF //Common Anode is active low (all off = 0xFF or ~0); Common Cathode is active high (all off = 0)
#define VerifyChksum  ssr_cfg.bits.VerifyChksum
#define RGswap  ssr_cfg.bits.RGswap

//set TRIS to handle output-only pins:
//kludge: macro body too long for MPASM; use shorter macros
#if isPORTA(_FRPANEL_PIN)
 #define _FRPANEL_TRISA  PINOF(_FRPANEL_PIN)
#else
 #define _FRPANEL_TRISA  0
#endif
#if isPORTBC(_FRPANEL_PIN)
 #define _FRPANEL_TRISBC  PINOF(_FRPANEL_PIN)
#else
 #define _FRPANEL_TRISBC  0
#endif
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

//#define TRISA_INIT_CHPLEX  ~(PORTAPIN(_FRPANEL_PIN) | PORTAPIN(_SEROUT_PIN)) // /*0x28*/
#define TRISA_INIT_CHPLEX  ~(_FRPANEL_TRISA | _SEROUT_TRISA) // /*0x28*/
//#define TRISBC_INIT_NODES  IIF(PORTOF(SERIN_PIN) != 0xA, 1<<PINOF(SERIN_PIN), 0) | IIF(PORTOF(ZC_PIN) != 0xA, 1<<PINOF(ZC_PIN), 0) /*0x3B*/
//#define TRISBC_INIT_PWM  TRISBC_INIT_NODES
//#define TRISBC_INIT_CHPLEX  ~(PORTBCPIN(_SEROUT_PIN) | PORTBCPIN(_FRPANEL_PIN)) /*0x3B*/
#define TRISBC_INIT_CHPLEX  ~(_SEROUT_TRISBC | _FRPANEL_TRISBC) /*0x3B*/
// TRISA_INIT_CHPLEX; //set row pin to Output ("0"), columns to Input (Hi-Z) = "1", special output pins remain as-is (SEROUT), and A4 is hard-wired to Input
// TRISBC_INIT_CHPLEX; //set row pin to Output ("0"), columns to Input, special output pins remain as-is


#ifdef debug
 volatile AT_NONBANKED(0) uint8_t trisa_debug;
 volatile AT_NONBANKED(0) uint8_t trisbc_debug;
 INLINE void tris_debug(void)
 {
    debug(); //incl prev debug info
    trisa_debug = TRISA_INIT_CHPLEX;
    trisbc_debug = TRISBC_INIT_CHPLEX;
 }
 #undef debug
 #define debug()  tris_debug()
#endif


//set buffers to turn off all SSRs:
INLINE void AllOff()
{
//NOTE: when TRIS all off, portbuf[] doesn't matter
//	portbuf[0] = portbuf[1] = ALL_OFF;
//set all rows to high-Z:
	trisbuf[0] = TRISA_INIT_CHPLEX;
	trisbuf[1] = TRISBC_INIT_CHPLEX;
}


//set up indirect addressing:
//NOTE: macro used here so arg can be a const
//INLINE void fifo_indirect0(void)
//#define outbuf_indirect0(reset)  
//{ 
//    if (reset) FSR0_16.as_ptr = outptr.as_ptr = &frbuf[fbactive]; 
//    else FSR0_16.as_ptr = &frbuf[fbactive]; /*FSR0_16.as_int += ofs*/; } 
//}


//;initialize output (display) buffer:
INLINE void outbuf_init(void)
{
	init(); //prev init first
    LABDCL(0xF6);
    ssr_cfg.allbits = 0; //default values; enough to get up and running
//    FSR1_16.as_ptr = 
//    outptr9.as_ptr = &buffers[OUT_FIRST].dimcols[0];
//kludge: SDCC doesn't handle indirect addressing well (uses temps and erroneous loads), so use FSR + INDF instead
//    buffers[OUT_FIRST].dimcols[0] = 0; //clear output buffer (display list)
//    *outptr9 = 0;
//    phase_angle = NUMSLOTS - 1; //set to end of dimming cycle until next zc signal
//    FSR0_16.as_ptr = outptr.as_ptr = &frbuf[fbactive][0].delay;
//    adrs_low_WREG(frbuf[0][0].delay);
//    WREG = AT_LINEAR(FRBUF0_START) % 0x100;
//    if (fbactive) WREG = AT_LINEAR(FRBUF1_START) % 0x100; //adrs_low_WREG(frbuf[1][0].delay);
//    FSR0L = outptr.low = WREG;
//    adrs_high_WREG(frbuf[0][0].delay);
//    WREG = AT_LINEAR(FRBUF0_START) / 0x100;
//    if (fbactive) WREG = AT_LINEAR(FRBUF1_START) / 0x100; //adrs_high_WREG(frbuf[1][0].delay);
//    FSR0H = outptr.bytes[1] = WREG;
    frbuf_indirect(FSR0_16, fbactive, 2);
//create empty display list:
//    outbuf_indirect0(TRUE);
//    FSR0_16.as_int = output.as_int;
//    INDF0_POSTINC = 0; //set dummy config; value doesn't matter
//    INDF0_POSTING = 0; //set dummy checksum byte; value doesn't matter
    INDF0 = 0; //set eof marker; //obs-NOTE: only need to clear one entry here since phase_angle will be clamped at cycle end; this avoids an expensive memory clear (~ 64 usec @ 8 MIPS), which would mess up other init timing
//    uint8_t init_loop = divup(NUMSLOTS, 2);
//    WREG = 0;
//    while (init_loop--) INDF0_POSTINC = 0; //clear row inx table; expensive: 128 * 4 instr == 64 usec @ 8 MIPS
//set first I/O pin update all off:
//	WREG = 0;
//	if (!ActiveHigh) WREG = ~0; //off is high instead of low
//	all_off = WREG;
//	trisbuf[0] = TRISA_INIT_CHPLEX; //set all rows to high-Z
//	trisbuf[1] = TRISBC_INIT_CHPLEX;
//	portbuf[0] = portbuf[1] = ALL_OFF;
	evtdelay = 0; //set max delay on first event; this will give zc time to stabilize (if signal is present), but falls back to continuous loop otherwise
    AllOff(); //preset buffers to all SSRs off
//not needed: TRIS starts hi-Z:
//	TRISA = trisbuf[0];
//	TRISC = trisbuf[1];
}
#undef init
#define init()  outbuf_init() //function chain in lieu of static init


//#ifndef on_zcfall
// #define on_zc_fall() //initialize function chain
//#endif

//output buffer event handler:
//reset dimming cycle to sync with zc signal
INLINE void on_zc_fall_outbuf(void)
{
	on_zc_fall(); //prev zc handlers first
    LABDCL(0xF7);
//    if (zc_state.bits.edge) return; //rising edge; wait for falling edge (not enough voltage to turn on Triacs)
//    ++phase_angle; //keep track of place ("angle") within dimming cycle
//    phase_angle = 0; //NUM_SLOTS; //reset for new dimming cycle
//    outptr.as_ptr = &frbuf[fbactive][0].delay; //start next dimming cycle
//    adrs_low_WREG(frbuf[0][0].delay);
//    WREG = AT_LINEAR(FRBUF0_START) % 0x100;
//    if (fbactive) WREG = AT_LINEAR(FRBUF1_START) % 0x100; //adrs_low_WREG(frbuf[1][0].delay);
//    outptr.low = WREG;
//    adrs_high_WREG(frbuf[0][0].delay);
//    WREG = AT_LINEAR(FRBUF0_START) / 0x100;
//    if (fbactive) WREG = AT_LINEAR(FRBUF1_START) / 0x100; //adrs_high_WREG(frbuf[1][0].delay);
//    outptr.bytes[1] = WREG;
//    outbuf_rewind();
    frbuf_indirect(outptr, fbactive, 2); //skip config and checksum (already processed)
//    ssr_cfg = INDF0_POSTINC; //set config (SSR polarity)
//    WREG = INDF0_POSTINC; //skip checksum byte
//    outptr.low = FSR0L; outptr.high = FSR0H;
//    outbuf_indirect0(TRUE); //start next dimming cycle
//	evtdelay = 0; //set max delay on first event; zc falling edge will (should?) occur first
    evtdelay = 1; //set min delay until first event
    AllOff(); //NOTE: could do this on rising edge, but line voltage is not high enough yet anyway so there's no rush
}
#undef on_zc_fall
#define on_zc_fall()  on_zc_fall_outbuf() //event handler function chain


//#ifndef on_tmr_dim
// #define on_tmr_dim() //initialize function chain
//#endif

//#define upper_bit2mask_WREG(reg)  
//{ 
//	WREG = 0x01; 
//	if (reg & 0b0100000) WREG = 0x04; 
//	if (reg & 0b0010000) WREG += WREG; 
//	if (reg & 0b1000000) swap(WREG); 
//}


//dimming slot event handler:
//on data: ~60 instr (~ 8 usec @ 8 MIPS)
//on no data: ~26 instr (~ 3 usec @ 8 MIPS)
//best case (1 slot per row): 8/256 data * 60 instr + 248/256 * 26 instr ~= 28 instr (3.5 usec @ 8 MIPS)
//worst case (7 slots per row): 56/256 data * 60 instr + 200/256 * 26 instr ~= 34 instr (4 usec @ 8 MIPS)
//called ~ every 32.5 usec (60 Hz AC) => best 10% CPU, worst =~ 15% CPU consumption
//NOTE: put this ahead of ch*plexing dimming timer event handler in order to reduce timing jitter
INLINE void on_tmr_dim_outbuf(void)
{
//    uint8_t nextrow;
	on_tmr_dim(); //prev tmr handlers first
    LABDCL(0xF8);
//    if (zc_state.bits.edge) return; //rising edge; wait for falling edge (not enough voltage to turn on Triacs)
//NOTE: I/O pin values are ready immediately; to reduce timing jitter, I/O pin values are pre-set during idle time from previous cycle
//kludge (anti-ghosting): set all PORT pins off before turning on TRIS to avoid extraneous output signals; safe for dedicated/pwm as well as chplex
//    if (!dirty) return;
	WREG = 0;
	if (!isActiveHigh) WREG = ~0; //off is high instead of low
	PORTA = PORTC = WREG; //ALL_OFF;
//with all pins off, it's safe to set TRIS:
	TRISA = trisbuf[0];
	TRISC = trisbuf[1];
//with TRIS set correctly, now set data:
	PORTA = portbuf[0];
	PORTC = portbuf[1];
//generate data for next cycle:
//this makes data available immediately at start of next dimming slot and reduces timing delay/jitter
    AllOff(); //turn off SSRs next time; significantly reduces controller power and prolongs SSR opto LED life; NOTE: can't do this with pseudo-PWM on dumb LEDs
//	--evtdelay; //current display event is still active
//	if (!ZERO) return; //current display event is still active
	if (--evtdelay) return; //current display event is still active
//	{
//			if (IsFallingEdge(zc_filter)) goto next_cycle; //{ iobusy = TRUE; break; } //cancel current event and start new dimming cycle
//		if (chplex) //turn outputs off again after 1 cycle (requires latching SSRs); sender could force this, but event list size would double do it automatically instead
//NOTE: don't do this for muxed pwm of dumb LEDs; select pwm I/O type and set a row
//		trisbuf.bytes[0] = TRISA_INIT(CHPLEX); //set all rows to high-Z
//		trisbuf.bytes[1] = TRISBC_INIT(CHPLEX);
//        return;
//    }
//    FSR0_16.as_ptr = outptr.as_ptr;
    FSR0L = outptr.low; FSR0H = outptr.high;
//    outbuf_indirect0(FALSE);
    WREG = INDF0;
	if (ZERO) //end of list => rewind display event list; NOTE: 0 delay for first event => max wait, not rewind
	{
//        AllOff(); //turn off until next cycle
		if (zc_present) return; //don't rewind until ZC falling edge; NOTE: falling edge is assumed to occur before evtdelay 255 expires, but if it doesn't this will prevent next display event and avoid random dimming/flicker due to insufficient Triac voltage
//        FSR0_16.as_ptr = outptr.as_ptr = &frbuf[fbactive]; //rewind
//        outbuf_indirect0(TRUE); //rewind
//        FSR0_16.as_ptr = outptr.as_ptr = &frbuf[fbactive][0].delay;
//        adrs_low_WREG(frbuf[0][0].delay);
//        WREG = AT_LINEAR(FRBUF0_START) % 0x100;
//        if (fbactive) WREG = AT_LINEAR(FRBUF1_START) % 0x100; //adrs_low_WREG(frbuf[1][0].delay);
//        FSR0L = outptr.low = WREG;
//        adrs_high_WREG(frbuf[0][0].delay);
//        WREG = AT_LINEAR(FRBUF0_START) / 0x100;
//        if (fbactive) WREG = AT_LINEAR(FRBUF1_START) / 0x100; //adrs_high_WREG(frbuf[1][0].delay);
//        FSR0H = outptr.bytes[1] = WREG;
        frbuf_indirect(FSR0_16, fbactive, 2); //just keep repeating the cycle
        outptr.high = FSR0H; outptr.low = FSR0L;
    }
	evtdelay = INDF0_POSTINC; //get duration of next event; can be "0" for max delay on first event only; otherwise "0" means end of list
    if (ZERO) return; //empty list; already reset output above so just wait for next dimming slot
//    ++phase_angle; //keep track of place ("angle") within dimming cycle
//    --phase_angle;
//    if (BORROW) ++phase_angle; //don't let it wrap
//    if (CARRY) --phase_angle; //don't let it wrap; clamp to end of dimming cycle
//PERF: 16 instr (2 usec) to here
//    rowpair_indirect0();
//    WREG = INDF0; //even slots are in upper nibble
//    if (phase_angle & 1) swap_WREG(INDF0);
//#if 0
//    if (!(phase_angle & 1)) //even slots are in upper nibble of row table
//    {
//        if (!(INDF0 & 0x80)) return; //no changes
//        upper_bit2mask_WREG(INDF0);
//    }
//    else //odd slots are in lower nibble
//    {
//        if (!(INDF0 & 0x08)) return; //no changes
//        bit2mask_WREG(INDF0);
//    }
//#else
//    swap(INDF0); //put row# for this phase angle parity into lower nibble, preserve adjacent (must come thru here 2x)
//    if (!(INDF0 & OCCUPIED)) return; //no changes; only update if data is present (pseudo-PWM)
//PERF: 26 instr to here (3.25 usec); remaining code executes to up 22% (max 56/256)
//??    if (!(INDF0 & 0x08)) { AllOff(); return; } //true ch*plexing always needs update:
//    bit2mask_WREG(INDF0); //0..7 => row bit mask 0x80..0x01
//#endif
//    nextrow = WREG;
//common anode: invert columns (on = low), leave row transistor as-is (on = high)
//common cathode: columns as-is (on = high), invert row transistor (on = low)
//get row output (no row changes for pwm), split bits off by port:
#warning RED_MSG "TODO: fix this; SDCC all wrong here; uses temps and wrong opcodes"
//TODO: fix this; add opc operand swapping to asm-fixup
	swap_WREG(_INDF0); andlw(0x0F); //WREG &= 0x0F; //port A row bit
	/*if (indf & 0x80)*/ addlw(8); //WREG += 8; //A3 is input-only; shift it to A4
	xorlw(TRISA_INIT_CHPLEX); //WREG ^= TRISA_INIT_CHPLEX; //set row pin to Output ("0"), columns to Input (Hi-Z) = "1", special output pins remain as-is (SEROUT), and A4 is hard-wired to Input
	trisbuf[0] = WREG;
//    trisbuf[0] = ((INDF0 >> 4) + 8) ^ TRISA_INIT_CHPLEX;
	WREG = INDF0_POSTINC; andlw(0x0F); //WREG &= 0x0F; //port C row bit
	xorlw(TRISBC_INIT_CHPLEX); //WREG ^= TRISBC_INIT_CHPLEX; //set row pin to Output ("0"), columns to Input, special output pins remain as-is
	trisbuf[1] = WREG;
//    trisbuf[1] = (INDF0_POSTINC & 0xF) ^ TRISBC_INIT_CHPLEX;
//get column outputs, split bits off by port:
//    col_indirect0();
//NOTE: true ch*plexing (used here) overwrites prev row; pseudo-PWM cumulatively sets new rows
	swap_WREG(_INDF0); andlw(0x0F); //WREG &= 0x0F; //port A column bits
	/*if (indf & 0x80)*/ addlw(8); //WREG += 8; //A3 is input-only; shift it to A4
//    WREG = (INDF0 >> 4) + 8;
//	trisbuf[0] ^= WREG; //set column pins also to Output (should have been set to Input when row was set above); A4 will be incorrect, but it's hard-wired to Input so it doesn't matter
    xorwf(_trisbuf + 0); //set column pins also to Output (should have been set to Input when row was set above); A4 will be incorrect, but it's hard-wired to Input so it doesn't matter
	if (!isActiveHigh) xorlw(0xFF); //WREG ^= 0xFF; //also update row + col pins with inverted values; //compl(portbuf.bytes[0]); //= ~portbuf.bytes[0]; //rows are low, columns are high; BoostC doesn't seem to know about the COM instr
	portbuf[0] = WREG; //set column output pins high (CA) or low (CC), row low (CA) or high (CC); these are inverted for common cathode
	WREG = INDF0_POSTINC; andlw(0x0F); //WREG &= 0x0F; //port C column bits
//    WREG = INDF0_POSTINC & 0xF;
//	trisbuf[1] ^= WREG; //set column pins also to Output (should have been set to Input when row was set above)
	xorwf(_trisbuf + 1); //set column pins also to Output (should have been set to Input when row was set above)
	if (!isActiveHigh) xorlw(0xFF); //WREG ^= 0xFF; //also update row + col pins with inverted values; //compl(portbuf.bytes[1]); //= ~portbuf.bytes[1]; //rows are low, columns are high; BoostC doesn't seem to know about the COM instr
	portbuf[1] = WREG; //set column output pins high (CA) or low (CC), row low (CA) or high (CC)
//PERF: additional 26 instr (3.25 usec) to here
//    outptr.as_ptr = FSR0_16.as_ptr;
    outptr.low = FSR0L; outptr.high = FSR0H;
}
#undef on_tmr_dim
#define on_tmr_dim()  on_tmr_dim_outbuf() //event handler function chain


////////////////////////////////////////////////////////////////////////////////
////
/// Serial interface (FIFO + ch*plex encoding)
//

//a stream of bytes will come from GPU 30x - 60x / second == at most every 16.7 msec
//this will be converted to a list of I/O update events
//unfortunately, RPi GPU does not allow sync with external ZC signal :(

//#define CHPLEX_NUMCH  (8 * 7) //#ch*plexed channels

//1 byte (10 bits) @2.67 MHz == 12.5 usec, but WS281X node time is 30 usec (24 bits), so each channel is sent 2x
//1 byte pair will arrive every ~ 30 usec (to match WS281X pixel timing) == 240 instr @ 8 MIPS
//transmitting 56 byte pairs will take 56 * 30 usec ~= 1.7 msec ~= 10% of frame time
//WS281X 3 bytes @800 KHz == 30 usec, 10 usec/byte, 1.25 usec/bit == 3 serial bits
//#define IN_TIMEOUT  TMR0_TICKS(50 usec) //activate I/O changes after period of inactivity


//no #define OCCUPIED  8 //since ch*plex row/col only needs 3

#if 0
//serial in fifo:
//assigning nodes to dimming slots can be expensive, so input chars are stored in a fifo
//fifo is drained during ch*plexing; cooperative multi-tasking avoids interfering with ch*plexing timing
__at(AT_LINEAR(ENC_START)) //always uses indirect addressing so put it at end and leave lower addresses for direct (banked) addressing
//uint8_t FIFO[2 * NUM_SSR * NUM_SSR]; //no-NOTE: one extra byte for overflow; NOTE: including the charliplexing/chipiplexing diagonal wastes a little space, but addressing is simpler so leave it in
struct
{
    uint8_t sorted[NUM_SSR * (NUM_SSR - 1)]; //in order to reduce memory shuffling, only indices are sorted; NOTE: better perf is this is all within the same 256 address block (8-bit address arithmetic)
//statically allocated I/O pin update info:
//never moves after allocation, but could be updated
    struct //DimRowEntry
    {
        uint8_t brightness;
//    uint8_t rownum_numcols; //#col upper, row# lower; holds up to 16 each [0..15]
//    uint8_t colmap; //bitmap of columns for this row
//   uint8_t rowmap; //bitmap of rows
        uint8_t numrows;
        uint8_t colmaps[8]; //bitmap of columns for each row
    } updates[NUM_SSR * (NUM_SSR - 1)];
} encoder;

//NOTE: fifo contains duplicated (redundant) data for comm error checking
//this puts row# in upper nibble, column# in lower nibble
//FIFO also contains unused ch*plex diagonal row/col address
//this allows ch*pexed diagonal address to be detected based on FIFO offset, avoids /7 arithmetic
//uint8_t rcinx = 0; //raw (row, col) address; ch*plex diagonal address will be skipped
BANK0 uint8_t fifo_head; //used as input count and parity
BANK0 uint8_t fifo_tail; //used as dequeue state

BANK0 uint8_t dim_count; //#dimming slots requested
BANK0 uint8_t total_rows; //actual #dimming slots needed
//uint8_t numrows[NUM_SSR * (NUM_SSR - 1)];
#endif

volatile BANK0 uint8_t inbuf_len; //#bytes received
volatile BANK0 uint8_t inbuf_chksum;

//use byte instead of bit so decsz instr can be used
volatile BANK0 uint8_t in_timeout; //apply updates after inactivity; TODO: can be single bit
//BANK0 struct //TODO: find an unused bit somewhere for this
//{
//    uint8_t in_timeout: 1; //apply updates after inactivity (1+ time slot)
////    uint8_t in_parity: 1; //input byte redundancy
//    uint8_t unused: 6+1;
//} inbuf_state;
//#define in_timeout  inbuf_state.in_timeout


//#define OCCUPIED  (1 << CHPLEX_RCSIZE) //top bit indicates slot is assigned to a row
//#define CHPLEX_RCSIZE  NumBits(NUM_SSR - 1) //#bits needed to hold ch*plex row#/col#; should be 3
//#define CHPLEX_RCMASK  (1 << CHPLEX_RCSIZE - 1) //ch*plex row/col mask

//#define ROWPAIR_CHUNKLEN(n)  ((n) << 5)
//#define ROWPAIR_CHUNKS  divup(NUM_ROWPAIRS, ROWPAIR_CHUNKLEN(1)) //frame buffer (rowpair buf) is too big to clear during 1 dimming slot, so split it up


INLINE void inbuf_reset(void)
{
    inbuf_len = inbuf_chksum = 0;
//    fifo_head = fifo_tail = 0;
//    in_parity = 0;
//    fifo_tail = -ROWPAIR_CHUNKS; //reserve multiple time slots to clear frame buffer; value should be -4 for chunk len of 32
//    bradjust = 0;
//    in_timeout = 0;
//    dim_count = total_rows = 0;
//    total_rows = 0;
//    struct DimRowEntry* ptr = &DimRowList[0];
//    ptr->delay = 0;
//    ptr->numrows = 0;
//    sorted[0] = 0;
//    count = 1;
//    rcinx = 0;
}


//;initialize input buffer:
INLINE void inbuf_init(void)
{
	init(); //prev init first
    LABDCL(0xF3);
    inbuf_reset();
//    inptr9 = &buffers[IN_FIRST].dimcols[0]; //SDCC does okay with simple address assignment :)
//    in_rc.byte = 0;
//    in_rc.col = 1;
}
#undef init
#define init()  inbuf_init() //function chain in lieu of static init


//set up indirect addressing:
//NOTE: macro used here so arg can be a const
//INLINE void fifo_indirect0(void)
//#define inbuf_indirect1(ofs)  
//{ 
//    /*FSR1_16.as_ptr = &frbuf[!fbactive][0].delay*/; 
//    /*adrs_low_WREG(frbuf[0][0].delay)*/; 
//    WREG = AT_LINEAR(FRBUF0_START) % 0x100; 
//    if (!fbactive) WREG = AT_LINEAR(FRBUF1_START) % 0x100; /*adrs_low_WREG(frbuf[1][0].delay)*/; 
//    FSR1L = WREG; 
//    /*adrs_high_WREG(frbuf[0][0].delay)*/; 
//    WREG = AT_LINEAR(FRBUF0_START) / 0x100; 
//    if (!fbactive) WREG = AT_LINEAR(FRBUF1_START) / 0x100; /*adrs_high_WREG(frbuf[1][0].delay)*/; 
//    FSR1H = WREG; 
//    WREG = ofs; 
//	FSR1_16.as_int += WREG; 
//}


//dissected enqueue() code to interleave with front panel I/O:
//look at ASM to verify instr overhead; TODO: add logic to asm-fixup script
//TODO: caller use INDF0 to relax banksel restrictions below
#define FIFO_ADRS_OVERHEAD  8 //verify by looking at ASM
INLINE void enqueue_adrs()
{
//    inbuf_indirect1(inbuf_len);
    frbuf_indirect(FSR1_16, !fbactive, 0); //PERF: 6 instr
//    FRS1_16.as_ptr = &frbuf[!fbactive];
//    FSR1_16.as_ptr = &FIFO[0];
//    FSR1_16.as_int += fifo_head;
//    WREG = inbuf_len; //fifo_head;
//    FSR1_16.as_ptr += WREG;
//    WREG = fifo_head - 2 * NUM_SSR * NUM_SSR; //pre-set BORROW
//    FSR1_16.as_int16 += inbuf_len; //SDCC uses temps for this :(
    WREG = inbuf_len;
    FSR1L += WREG; //CAUTION: assumes frbuf lies within 256 byte block so CARRY is not needed
}
#define FIFO_RCV_OVERHEAD  8 //verify by looking at ASM
//CAUTION: assumes BORROW still valid
INLINE void enqueue_rcv()
{
//    if (CARRY) ++FSR1H; //not needed :)
//    WREG += -3 * NUM_SSR * (NUM_SSR - 1); //compare to buf size; pre-set BORROW; CAUTION: must use += -ve in order to preserve WREG correctly (SUBLW opc is reversed)
//TODO: change this to PktLen, -3 * NUM_SSR -2 for plain SSRs
    addlw(-3 * NUM_SSR * (NUM_SSR - 1) - 2); //1 extra for config and checksum; compare to buf size; pre-set BORROW; CAUTION: must use += -ve in order to preserve WREG correctly (SUBLW opc is reversed)
//CAUTION: extra instr inserted here, can't split bit test instr
    WREG = RCREG; //CAUTION: banksel on RCREG takes 1 additional instr
    if (!BORROW) TXREG = WREG; //overflow: pass to down-stream SSRs
//TODO: check FRERR?
//    in_timeout = 1; //wait 1+ dimming slot for serial idle; CAUTION: banksel here
    in_timeout = 0; //CAUTION: banksel takes 1 additional instr; must do this within idle period
    in_timeout |= 2; //CAUTION: must preserve WREG here, so use BSF instr
}
#define FIFO_ADV_OVERHEAD  7 //verify by looking at ASM
INLINE void enqueue_adv()
{
    nop();
//CAUTION: extra instr inserted here, can't split bit test instr
    if (BORROW) INDF1 = WREG; //store as-is and do validation later when there's more time
    if (BORROW) inbuf_chksum ^= WREG; //only update checksum with non-overflow bytes
    if (BORROW) ++inbuf_len; //no overflow: move to next byte; CAUTION: do this last to preserve BORROW
//    swap_WREG(fifo_head); WREG <<= 1; WREG ^= fifo_head; WREG &= (CHPLEX_RCMASK << 1);
//    WREG = 2;
//    if (ZERO) fifo_head += WREG; //skip diagonal ch*plex row/col address
}
//#define FIFO_CHPLEX_OVERHEAD  4 //verify by looking at ASM
//INLINE void enqueue_chplex()
//{
//    if (!(fifo_head & CHPLEX_RCMASK)) ++fifo_head; //skip ch*plex diagonal address
//}

//add incoming byte to FIFO for processing later:
//need to call this at least every 15 usec
INLINE void enqueue(void)
{
//    if (fifo_head >= 2 * CHPLEX_NUMCH) { TXREG = RCREG; return; } //overflow: just pass to down-stream SSRs
//    FSR1_16.as_ptr = &FIFO[0];
//    FSR1_16.as_int += fifo_head++;
//    INDF1 = RCREG; //store as-is and do validation later when there'e more time
//    in_timeout = 2; //first interval might be partial, so wait one extra
//    if (++fifo_head == CMD_MAX - CMD_FIRST) fifo = 0; //circular
//restructured for uniform execution path:
//    FSR1_16.as_ptr = &FIFO[0];
//    FSR1_16.as_int += fifo_head++;
//    in_timeout = 2; //first interval might be partial, so wait one extra; TODO: wait if overflow?
//    WREG = fifo_head - 2 * CHPLEX_NUMCH; //pre-set BORROW
//    INDF1 = RCREG; //store as-is and do validation later when there'e more time
//    if (!BORROW) TXREG = WREG; //overflow: pass to down-stream SSRs
//    if (!BORROW) --fifo_head; //overflow: allow another overflow byte
//reuse dissected code:
//    enqueue_chplex();
    enqueue_adrs();
    enqueue_rcv(); //CAUTION: assumes WREG still valid
    enqueue_adv(); //CAUTION: assumes WREG and BORROW still valid
}


//#ifndef on_rx
// #define on_rx() //initialize function chain
//#endif

//check for serial data:
//need to call this at least every 12.5 usec (== 100 instr), == 10 WS281X bit times (since that is also used for baud rate)
INLINE void on_rx_serial(void)
{
	on_rx(); //prev serial handlers first
    LABDCL(0xF4);
    enqueue();
}
#undef on_rx
#define on_rx()  on_rx_serial() //function chain


//dequeue FIFO during dimming slots:
//dequeue processing is broken up into smaller chunks to fit inside dimming time slots so it can be done during idle time
INLINE void on_tmr_dim_serial(void)
{
    on_tmr_dim(); //prev tmr_dim event handlers first
    LABDCL(0xF5);
//    if (!fifo_head) return; //empty
//    if (fifo_tail >= fifo_head) //no more data to process; NOTE: dequeue() will always run behind enqueue(), so this really means eof
//    {
//    if (in_timeout) { in_timeout = FALSE; return; } //wait another dimming slot for idle (this one might be partial)
    if (--in_timeout) return; //wait another dimming slot for idle (this one might be partial)
//TODO: use FRERR if not inverting input
//    in_timeout = 0; //revert status in order to continue next time
//BANK0 uint8_t bradjust; //brightness adjustment (for dimming slot fitting)
//        resolve_conflicts();
//    inbuf_indirect1(inbuf_len);
//idle period has ended; start processing new frame buf
    inbuf_reset(); //prep for next time (does not touch frbuf bytes)
    frbuf_indirect(FSR1_16, !fbactive, 0);
//    if (!(INDF1 & 2)) //verify checksum
    WREG = inbuf_chksum;
    if (!ZERO) comm_badpkt = TRUE; //remember checksum error
    if (VerifyChksum && !ZERO) return; //NOTE: set in *previous* framebuf
    ssr_cfg.allbits = INDF1; //save new config for later processing
    rgswap(RGswap); //update color fmt for front panel
    FSR1L += inbuf_len;
//make sure display list is null-terminated:
//write 3 bytes in case last entry was incomplete
    WREG = 0;
    INDF1_POSTINC = WREG;
    INDF1_POSTINC = WREG;
    INDF1_POSTINC = WREG;
//SDCC uses temp when this is within a struct :(
//    fbactive = !fbactive; //swap buffers
//    FrontPanel.parts ^= 1<<1;
//swap buffers:
//kludge: avoid poor SDCC code gen here; use awkward code and put this last
//    if (fbactive) fbactive = FALSE;
//    else fbactive = TRUE;
    WREG = 1<<1; xorwf(_FrontPanel + 1); //.parts);
//    return;
//    }
#if 0
//allow dequeue to start as soon as data arrives:
//    if (in_timeout--) return; //wait longer
//    in_timeout = 0; //revert status in order to continue next time
#if 0
//    if (!((fifo_tail + 1) & CHPLEX_RCMASK)) ++fifo_tail; //skip col 7 to compensate for diagonal ch*plex row/col
//        if (fifo_tail < fifo_head) return; //more fifo processing next time
//start dequeue:
//don't need to handle incoming chars again for another ~ 15 msec
//just need to break up dequeue into smaller chunks to cooperate with ch*plexing timing (~ 32 usec slots)
    if (fifo_tail < 0) //clear row pair buffer (in 32 byte chunks to prevent timing interference)
    {
//        FSR1_16.as_ptr = &buffers[!fbactive].dimrows[0];
//	    FSR1_16.as_int +=  ROWPAIR_CHUNKLEN(ROWPAIR_CHUNKS + tail);
        rowpair_indirect0(ROWPAIR_CHUNKLEN(ROWPAIR_CHUNKS + fifo_tail));
        min_WREG(NUM_ROWPAIRS - WREG << 1, ROWPAIR_CHUNKLEN(1));
        for (uint8_t loop = WREG; loop--; INDF1_POSTINC = 0); //32 * 4 = 128 instr == 16 usec @ 8 MIPS
        ++fifo_tail; //next chunk during next dimming slot
        return; //share CPU with other tasks
    }
//check for valid data received:
//    uint8_t row = (tail >> 1) / 7, col = (tail >> 1) % 7; //TODO: avoid 
//    FSR1_16.as_ptr = &FIFO[0];
//    FSR1_16.as_int += tail; //tail += 2;
//    if (fifo_tail in [0x00, 0x12, 0x24, 0x36, 0x48, 0x5a, 0x6c, 0x7e]) fifo_tail += 2; //skip diagonal ch*plex row/col address
#endif
    swap_WREG(fifo_tail); //ch*plex row#
    WREG <<= 1; WREG ^= fifo_tail; WREG &= (CHPLEX_RCMASK << 1); //compare to col#
    if (ZERO) { fifo_tail += 2; return; } //skip diagonal ch*plex row/col address
    fifo_indirect1(fifo_tail);
    WREG = INDF1_POSTINC; //brightness of this SSR
    WREG -= INDF1; //TODO: WREG &= INDF1 instead? (take smaller of 2 values)
    if (!ZERO) //redundant data check failed
    {
//            WREG >>= 1; //TODO: take average and try to recover?
//            brightness += WREG;
        FrontPanel.parts.comm_badpair = 1; //remember byte pair errors
//        fifo_reset(); //TODO: discard or continue?
        return;
//continue processing next byte pair
    }
    if (!INDF1) return; //don't need to store off SSRs; NOTE: do this after redundant data check
//PERF: 26 instr (~3.25 usec) to here
#if 0
//assign col to dimming slot:
//    uint8_t brightness = INDF1;
//    FSR0_16.as_ptr = &buffers[!fbactive].dimrows[0];
//    FSR0_16.as_int += INDF1 >> 1;
//        uint8_t row = ((fifo_tail << (4 - CHPLEX_RCSIZE)) & 0xF0) | (OCCUPIED << 4); //desired value; assume upper nibble
            rowpair_indirect0(INDF1);
            WREG = INDF0;
            if (INDF1 & 1) swap_WREG(INDF0); //for consistent/simpler logic below, put row for odd rows into upper nibble while checking
            if (((WREG ^ (OCCUPIED << 4) ^ fifo_tail) & 0xF0) //row# mismatch
            {
//approx 40 instr to here (5 usec)
                if (!(WREG & (OCCUPIED << 4))) goto reassign(); //row conflict: need to choose another timeslot
                WREG = fifo_tail; //row#
                if (INDF1 & 1) swap_WREG(fifo_tail); //row); //move row map for odd rows into correct (lower) nibble before updating
                INDF0 |= WREG | OCCUPIED;
            }
            col_indirect0(INDF1);
            WREG = (fifo_tail >> 1) & CHPLEX_RCMASK; //col#
            bit2mask_WREG(WREG);
            INDF0 |= WREG;
        }
#endif
//aggregate row/col changes by dimming slot:
    uint8_t newvalue = INDF1, row = INDF1 >> 4, col = (INDF1 >> 1) & 7;
    uint8_t start, end;
//check if entry already exists using binary search:
    for (start = 0, end = dim_count; start < end;)
    {
        uint8_t mid = (start + end) >> 1;
        FSR0_16 = &encoder.sorted[0];
        FSR0_16 += mid;
//    struct //DimRowEntry
//    {
//        uint8_t brightness;
//        uint8_t numrows;
//        uint8_t colmaps[8]; //bitmap of columns for each row
//    } updates[NUM_SSR * (NUM_SSR - 1)];
//        struct DimRowEntry* ptr = &DimRowList[sorted[mid]];
//        int cmpto = ptr->delay;
        FSR1_16 = &encoder.updates[0];
        FSR1_16 += (INDF0 << 1) + (WREG << 4); //x10
//NOTE: sort in descending order
//        if (newvalue > ptr->delay) { end = mid; continue; } //search first half
//        if (newvalue < ptr->delay) { start = mid + 1; continue; } //search second half
        if (newvalue > INDF1( { end = mid; continue; } //search first half
        if (newvalue < INDF1) { start = mid + 1; continue; } //search second half
//collision:
//        if (!ptr->colmaps[ROW(rcinx)]) { ++ptr->numrows; ++total_rows; }
//        ptr->colmaps[ROW(rcinx)] |= 0x80 >> COL(rcinx);
        FSR0_16 = FSR1_16;
        FSR0_16 += 2 + row;
        if (!INDF0) { ++FSR1_16; ++INDF1; ++total_rows; }
        INDF0 |= 0x80 >> col;
        fifo_tail += 2; //advance to next col/row
        return;
    }
//create a new entry, insert into correct position:
//    for (int i = count; i > start; --i) sorted[i] = sorted[i - 1];
//    sorted[start] = count;
    FSR0_16 = &encoder.sorted[0];
    FSR0_16 += dim_count;
    uint8_t loop = dim_count - start;
    while (loop--) { WREG = INDF0_PREDEC; INDF0_PREINC = WREG; --FSR0_16; }
//    write(newvalue);
//    if (count >= NUM_SSR * (NUM_SSR - 1)) { printf(RED_LT "overflow" ENDCOLOR); return; }
//    struct DimRowEntry* ptr = &DimRowList[count];
//    ptr->delay = val;
//    ptr->numrows = 1;
//    for (int i = 0; i < 8; ++i) ptr->colmaps[i] = 0; //TODO: better to do this 1x at start, or incrementally as needed?
//    ptr->colmaps[ROW(rcinx)] = 0x80 >> COL(rcinx);
//    ++total_rows;
//    ++count;
//    ++rcinx;
    FSR1_16 = &encoder.updates[0];
    FSR1_16 += (dim_count << 1) + (WREG << 4); //x10
    INDF1_POSTINC = newvalue;
    INDF1_POSTINC = 1;
    REPEAT(8, INDF1_POSTINC = 0); //TODO: do this at init?
    FSR1_16 -= 8;
    FSR1_16 += row;
    INDF1 |= 0x80 >> col;
    ++total_rows;
    ++dim_count;
    fifo_tail += 2;
//PERF: additional 48 instr (6 usec) + 330 instr (42 usec) worst case
#endif
}
#undef on_tmr_dim
#define on_tmr_dim()  on_tmr_dim_serial() //function chain


////////////////////////////////////////////////////////////////////////////////
////
/// Front panel (continued)
//

//refresh front panel display:
//"front panel" consists of 3 WS281X nodes
//WS281X is used because it only requires one I/O pin for any number of WS281X pixels :)
//refreshing 3 WS281X pixels takes 3 x 30 usec == 90 usec; too long to block (ch*plexing slots are ~ 32 usec)
//*however*, SSRs can't operate during ZC (which is ~ 240 usec), so always update front panel while ZC is high (start on rising edge)
//just needs to save serial data that might come in during that time (~20 instr ~= 3 usec @ 8 MIPS)
//split up the work for cooperative multi-tasking
//INLINE void on_zc_tick_frpanel(void)
volatile BANK0 uint8_t frloop; //put this at global scope to avoid SDCC temp names (until all temps have been eliminated)
non_inline void frpanel_refresh(void)
{
//PORTA = 1;
//    frloop = 1;
//    SWOPCODE(4);
//    frloop = 2;
//    NOP(6);

//    uint8_t frloop;
//now start sending:
//sends one byte per iteration; CARRY selects whether to send "1"s or "0"s
//NOTE: one WS281X node takes 24 * 1.25 == 30 usec, so serial port must be checked >= 2x per node
//check serial port 1x / byte to prevent overruns
//TODO: move vars to unbanked RAM and use INDF0 to relax banksel restrictions
    for (frloop = 4 * 3; /*frloop*/; --frloop)
    {
        WREG = frloop;
        if (ZERO) break;
//CAUTION: banksel occurs here at top of loop
//limit brightness + power consumption by keeping upper bits low
//kludge: this also consolidates idle instr more conveniently for interleaved code :)
//        REPEAT(3, send_0(0)); 
        send_0({ enqueue_adrs(); NOP(SPARE_NOPS - FIFO_ADRS_OVERHEAD); });
        send_0({ enqueue_rcv(); NOP(SPARE_NOPS - FIFO_RCV_OVERHEAD); }); //CAUTION: assumes WREG still valid
        send_0({ enqueue_adv(); NOP(SPARE_NOPS - FIFO_ADV_OVERHEAD); }); //CAUTION: assumes WREG and BORROW still valid
#define BRANCH_OVERHEAD_IN  6 //CAUTION: look at ASM to verify; TODO: add verification to asm fixup script
#define BRANCH_OVERHEAD_OUT  3 //CAUTION: look at ASM to verify; TODO: add verification to asm fixup script
        send_0(NOP(SPARE_NOPS - BRANCH_OVERHEAD_IN)); //compensate for branching overhead
//        frpanel_colors.all.as_int16 <<= 1; //need to deduct loop overhead to this point from previous bit time
//        frp_colors[0] <<= 1; rlf(_frp_colors + 1);
        lslf(_frp_colors + 0); rlf(_frp_colors + 1);
        if (CARRY) //send "1"s (actually a mix of "1"s and "0"s, to reduce brightness)
        {
            NOP(2); //make execution path uniform; 1 goto + 1 banksel
            send_1(NOP(SPARE_NOPS));
            send_1(NOP(SPARE_NOPS));
            send_1(NOP(SPARE_NOPS - BRANCH_OVERHEAD_OUT));
            NOP(0); //make execution path uniform
        }
        else
        {
//CAUTION: banksel occurs here after jump
            NOP(0); //make execution path uniform
            send_0(NOP(SPARE_NOPS));
            send_0(NOP(SPARE_NOPS));
            send_0(NOP(SPARE_NOPS - BRANCH_OVERHEAD_OUT));
            NOP(2); //make execution path uniform
        }
//CAUTION: banksel occurs here after jump
#define LOOP_OVERHEAD  (3 + 4) //CAUTION: look at ASM to verify; TODO: add verification to asm fixup script
//NOTE: extra banksel at start of loop (due to entry point label)
        send_0(NOP(SPARE_NOPS - LOOP_OVERHEAD)); //compensate for loop overhead during last bit; make it "0" to consolidate spare time
    }
    FrontPanel_reset(); //reset sticky data for next display cycle
}


////////////////////////////////////////////////////////////////////////////////
////
/// Main logic
//

#include "func_chains.h" //finalize function chains; NOTE: this adds call/return/banksel overhead, but makes debug easier
#ifndef debug
 #define debug()  //nop
#endif


void main(void)
{
//	ONPAGE(LEAST_PAGE); //put code where it will fit with no page selects
//    test();

    debug(); //incl debug info (not executable)
	init(); //1-time set up of control regs, memory, etc.
    for (;;) //poll for events
    {
        on_tmr_dim();
        on_tmr_50msec();
        on_tmr_1sec();
//these should probably come last (they use above timers):
        on_rx();
        on_zc_rise(); //PERF: best: 8 instr (1 usec), worst: 36 instr + I/O func (4.5 usec + 120+ usec)
        on_zc_fall(); //PERF: best: 12 instr (1.5 usec), worst: 28 instr (3.5 usec)
    }
}


//eof
///////////////////////////////////////////////////////////////////////////////
////
/// Chipiplexing encoder
//

#if 0
	Private Sub ChipiplexedEvent(ByRef channelValues As Byte())
		on error goto errh
		If channelValues.Length <> Me.m_numch Then Throw New Exception(String.Format("event received {0} channels but configured for {1}", channelValues.Length, Me.m_numch))
		Me.total_inlen += channelValues.Length
		Me.total_evts += 1
''TODO: show more or less data
		If Me.m_trace Then LogMsg(String.Format("Chipiplex[{0}] {1} bytes in: ", Me.total_evts, channelValues.Length) & DumpHex(channelValues, channelValues.Length, 80))
''format output buf for each PIC (groups of up to 56 chipiplexed channels):
		Dim buflen As Integer, protolen As Integer 
		Dim overflow_retry As Integer, overflow_adjust As Integer, merge_retry As Integer 
		Dim brlevel As Integer, brinx As Integer, row As Integer, col As Integer 
		Dim i As Integer
		Dim pic As Integer
''NOTE to style critics: I hate line numbers but I also hate cluttering up source code with a lot of state/location info
''I''m only using line#s because they are the most compact way to show the location of errors (kinda like C++''s __LINE__ macro), and there is no additional overhead
12:
''send out data to PICs in *reverse* order (farthest PICs first, closest PICs last):
''this will allow downstream PICs to get their data earlier and start processing while closer PICs are getting their data
''if we send data to closer PICs first, they will have processed all their channels before downstream PICs even get theirs, which could lead to partially refreshed frames
''at 115k baud, 100 chars take ~ 8.7 mec, which is > 1 AC half-cycle at 60 Hz; seems like this leads to slight sync problems
''TODO: make forward vs. reverse processing a config option
		For pic = Me.m_numch - (Me.m_numch - 1) Mod 56 To 1 Step -56 ''TODO: config #channels per PIC?
			Dim status As String = "" ''show special processing flags in trace file
			Dim numbr As Integer = 0 ''#brightness levels
			Dim numrows As Integer = 0, numcols As Integer ''#rows, columns occupied for this frame
			For i = 0 To 255 ''initialize brightness level index to all empty
				Me.m_brindex(i) = 0 ''empty
			Next i
14:
			Dim ch As Integer
			For ch = pic To pic+55 ''each channel for this PIC (8*7 = 56 for a regular Renard PIC); 1-based
				If ch > Me.m_numch Then Exit For
				brlevel = channelValues(ch-1)
16:
				If brlevel < Me.m_minbright Then ''don''t need to send this channel to controller (it''s off)
					If brlevel = 0 then
						Me.total_realnulls += 1
					Else
						Me.total_nearnulls += 1
					End If
				Else ''need to send this channel to the controller (it''s not off)
18:
					Dim overflow_limit As Integer = Me.m_closeness ''allow overflow into this range of values
					If brlevel >= Me.m_maxbright Then ''treat as full on
						If brlevel < 255 Then Me.total_nearfulls += 1
						overflow_limit += Me.m_fullbright - Me.m_maxbright ''at full-on, expand the allowable overflow range
						brlevel = Me.m_fullbright ''255
					End If
					row = (ch - pic)\7 ''row# relative to this PIC (0..7); do not round up
					col = (ch - pic) Mod 7 ''column# relative to this row
					If col >= row Then col += 1 ''skip over row address line (chipiplexing matrix excludes diagonal row/column down the middle)
#If BOARD1_INCOMPAT Then ''whoops; wired the pins differently between boards; rotate them here rather than messing with channel reordering in Vixen
''					Dim svch As Integer = ch - pic: ch = 0
''					If svch And &h80 Then ch += &h10
''					If svch And &h40 Then ch += 1
''					If svch And &h20 Then ch += &h40
''					If svch And &h10 Then ch += &h20
''					If svch And 8 Then ch += &h80
''					If svch And 4 Then ch += 8
''					If svch And 2 Then ch += 4
''					If svch And 1 Then ch += 2
''					ch += pic
					Select Case row
						Case 0: row = 4''3
						Case 1: row = 2''7
						Case 2: row = 3''1
						Case 3: row = 0''2
						Case 4: row = 5''0
						Case 5: row = 6''4
						Case 6: row = 7''5
						Case 7: row = 1''6
					End Select
					Select Case col
						Case 0: col = 4''3
						Case 1: col = 2''7
						Case 2: col = 3''1
						Case 3: col = 0''2
						Case 4: col = 5''0
						Case 5: col = 6''4
						Case 6: col = 7''5
						Case 7: col = 1''6
					End Select
#End If
20:
					For overflow_retry = 0 To 2 * overflow_limit
						If (overflow_retry And 1) = 0 Then ''try next higher (brighter) level
							overflow_adjust = brlevel + overflow_retry\2 ''do not round up
							If overflow_adjust > Me.m_fullbright Then Continue For
						Else ''try next lower (dimmer) value
							overflow_adjust = brlevel - overflow_retry\2 ''do not round up
							If overflow_adjust < Me.m_minbright Then Continue For
						End If
22:
						brinx = Me.m_brindex(overflow_adjust)
						If brinx = 0 Then ''allocate a new brightness level
							If Me.IsRenardByte(overflow_adjust) Then Me.num_avoids += 1: status &= "@": Continue For ''avoid special protocol chars
							numbr += 1: brinx = numbr ''entry 0 is used as an empty; skip it
							Me.m_brindex(overflow_adjust) = brinx ''indexing by brightness level automatically sorts them by brightness
''							Me.m_levels(brinx).level = brlevel ''set brightness level for this entry
							Me.m_rowindex(brinx) = 0 ''no rows yet for this brightness level
''							Me.m_levels(brinx).numrows = 0
							For i = 0 To 7 ''no columns yet for this brightness level
								Me.m_columns(brinx, i) = 0
							Next i
						End If
24:
						If Me.m_columns(brinx, row) = 0 Then ''allocate a new row
''							If Me.m_levels(brinx).numrows >= Me.m_maxrpl Then Continue For ''this level is full; try another one that is close (lossy dimming)
''enforce maxRPL here to avoid over-fillings brightness levels; lossy dimming will degrade with multiple full levels near each other
							If Me.NumBits(Me.m_rowindex(brinx)) >= Me.m_maxrpl Then Continue For ''this level is full; try another one that is close (lossy dimming)
''maxRPL can''t be more than 2 in this version, so we don''t need to check for reserved bytes here (the only one that matters has 7 bits on)
''							If Me.IsRenardByte(overflow_adjust) Then Me.num_avoids += 1: status &= "@": Continue For ''avoid special protocol chars
							Me.m_rowindex(brinx) = Me.m_rowindex(brinx) Or 1<<(7 - row)
''							Me.m_levels(brinx).numrows += 1
							numrows += 1
						End If
						Dim newcols As Byte = Me.m_columns(brinx, row) Or 1<<(7 - col)
						If Me.IsRenardByte(newcols) Then Me.num_avoids += 1: status &= "@": Continue For ''avoid special protocol chars; treat as full row
						Me.m_columns(brinx, row) = newcols ''Me.m_columns(brinx, row) Or 1<<(7 - col)
						If overflow_retry <> 0 Then Me.total_rowsoverflowed += 1: status &= "*"
						Exit For
					Next overflow_retry
26:
					If overflow_retry > 2 * overflow_limit Then
						If Me.m_nodiscard Then Throw New Exception(String.Format("Unable to overflow full row after {0} tries.", 2 * overflow_limit))
						Me.total_rowsdropped += 1
						status &= "-"
					End If
				End If
			Next ch
28:
#If False Then ''obsolete merge/reduction code
''			Dim brlimit As Integer = Me.m_maxbright + 8\Me.m_maxrpl - 1 ''allow for overflow of max bright level
''			If brlimit > 255 Then brlimit = 255 ''don''t overshoot real top end of brightness range
			For merge_retry = 0 To Me.m_closeness
				If 2+2 + 2*numbr + numrows <= Me.m_outbuf.Length Then Exit For ''no need to merge brightness levels
				If merge_retry = 0 Then Continue For ''dummy loop entry to perform length check first time
				For brlevel = Me.m_minbright To Me.m_fullbright 
					Dim tobrinx As Byte = Me.m_brindex(brlevel)
					If tobrinx = 0 Then Continue For
					Dim otherlevel As Integer 
					Dim mergelimit As Integer = brlevel + merge_retry
					if brlevel >= Me.m_maxbright Then mergelimit = 255
30:
					For otherlevel = brlevel - merge_retry To merge_limit Step 2*merge_retry
						If (otherlevel < Me.m_minbright) Or (otherlevel > brlimit) Then Continue For
						Dim frombrinx As Byte = Me.m_brindex(otherlevel)
						If (frombrinx = 0) or (Me.numbits(Me.m_rowindex(tobrinx) And Me.m_rowindex(frombrinx)) = 0) Then continue for ''can''t save some space by coalescing
						numrows -= Me.numbits(Me.m_rowindex(tobrinx) And Me.m_rowindex(frombrinx))
						Me.m_rowindex(tobrinx) = Me.m_rowindex(tobrinx) Or Me.m_rowindex(frombrinx) ''merge row index
						For row = 0 To 7 ''merge columns
							Me.m_columns(tobrinx, row) = Me.m_columns(tobrinx, row) Or Me.m_columns(frombrinx, row)
						Next row
32:
						Me.m_brindex(otherlevel) = 0 ''hide entry so it won''t be used again
						numbr -= 1
						Me.total_levelsmerged += 1
						status &= "^"
						If 2+2 + 2*numbr + numrows <= Me.m_outbuf.Length Then Exit For ''no need to coalesce more brightness levels
						Continue For
					Next otherlevel
				Next brlevel
			Next merge_retry
34:
			If merge_retry > Me.m_closeness Then
				If Me.m_nodiscard Then Throw New Exception(String.Format("Unable to merge brightness level after {0} tries.", 2*Me.m_closeness))
				Me.total_levelsdropped += 1
				status &= "X"
			End If
#End If
			Dim prevlevel As Integer = 0, prevbrinx As Integer 
''TODO: use MaxData length here
			While 3+2 + 2*numbr + numrows > Me.m_outbuf.Length ''need to merge brightness levels
''this is expensive, so config params should be set to try to avoid it
''TODO: this could be smarter; prioritize and then merge rows that are closest or sparsest first
				For brlevel = Me.m_minbright To Me.m_fullbright 
					brinx = Me.m_brindex(brlevel)
					If brinx = 0 Then Continue For
					If prevlevel < 256 Then ''merge row with previous to save space
30:
						If prevlevel = 0 Then prevlevel = brlevel: prevbrinx = brinx: Continue For ''need 2 levels to compare
						If (prevlevel + Me.m_closeness < brlevel) And (prevlevel + Me.m_closeness < Me.m_maxbright) Then Continue For ''too far apart to merge
						Dim newrows As Byte = Me.m_rowindex(brinx) Or Me.m_rowindex(prevbrinx) ''merge row index
						If Me.NumBits(newrows) > Me.m_maxrpl Then
''							If Not String.IsNullOrEmpty(Me.m_logfile) Then LogMsg("row " & brlevel & "->" & brinx & " full; can''t merge")
							Continue For ''can''t merge; row is already full
						End If
						If Me.IsRenardByte(newrows) Then Me.num_avoids += 1: status &= "@": Continue For ''can''t merge (would general special protocol chars)
						Dim newcols(8 - 1) As Byte
						For row = 0 To 7 ''merge columns
							newcols(row) = Me.m_columns(brinx, row) Or Me.m_columns(prevbrinx, row)
							If Me.IsRenardByte(newcols(row)) Then Me.num_avoids += 1: status &= "@": Exit For ''can''t merge (would general special protocol chars)
						Next row
						If row <= 7 Then Continue For ''can''t merge (would general special protocol chars)
						numrows -= Me.NumBits(Me.m_rowindex(brinx) And Me.m_rowindex(prevbrinx))
						Me.m_rowindex(brinx) = newrows ''Me.m_rowindex(brinx) Or Me.m_rowindex(prevbrinx) ''merge row index
						For row = 0 To 7 ''merge columns
							Me.m_columns(brinx, row) = newcols(row) ''Me.m_columns(brinx, row) Or Me.m_columns(prevbrinx, row)
						Next row
						If Me.m_trace Then LogMsg(String.Format("merged level {0} with {1}", prevlevel, brlevel))
						Me.m_brindex(prevlevel) = 0 ''hide this entry so it won''t be used again
						Me.total_levelsmerged += 1
						status &= "^"
					Else ''last try; just drop the row
						If Me.m_trace Then LogMsg(String.Format("dropped level {0}", brlevel))
						Me.m_brindex(brlevel) = 0 ''hide this entry so it won''t be used again
						numrows -= Me.NumBits(Me.m_rowindex(brinx))
						Me.total_levelsdropped += 1
						status &= "!"
					End If
32:
					numbr -= 1
					If 3+2 + 2*numbr + numrows <= Me.m_outbuf.Length Then Exit While ''no need to merge/drop more brightness levels
				Next brlevel
				If Me.m_nodiscard Then Throw New Exception(String.Format("Unable to merge brightness level after {0} tries.", 2*Me.m_closeness))
				prevlevel = 256 ''couldn''t find rows to merge, so start dropping them
34:
			End While
''TODO: resend data after a while even if it hasn''t changed?
''no, unless dup:			If numbr < 1 Then Continue For ''no channels to send this PIC
''format data to send to PIC:
''TODO: send more than 1 SYNC the first time, in case baud rate not yet selected
			buflen = 0
			protolen = 0
''			If pic = 1 Then ''only needed for first PIC in chain?  TODO
				Me.m_outbuf(buflen) = Renard_SyncByte: buflen += 1 ''&h7E
				protolen += 1
''			End If
			Me.m_outbuf(buflen) = Renard_CmdAdrsByte + pic\56: buflen += 1 ''&h80
''			Me.m_outbuf(buflen) = Renard_PadByte: buflen += 1 ''&h7D ''TODO?
			Me.m_outbuf(buflen) = Me.m_cfg: buflen += 1 ''config byte comes first
			protolen += 2
			For brlevel = Me.m_fullbright To Me.m_minbright Step -1 ''send out brightess levels in reverse order (brightest first)
36:
				brinx = Me.m_brindex(brlevel)
				If brinx = 0 Then Continue For
38:
				If (brlevel = 0) Or (Me.m_rowindex(brinx) = 0) Then SentBadByte(IIf(brlevel = 0, brlevel, Me.m_rowindex(brinx)), buflen, pic\56) ''paranoid
				If Me.IsRenardByte(brlevel) Then SentBadByte(brlevel, buflen, pic\56) ''paranoid
				If buflen < m_outbuf.Length Then Me.m_outbuf(buflen) = brlevel ''send brightness level; avoid outbuf overflow
				buflen += 1 ''maintain count, even if outbuf overflows (so we know how bad is was)
				If Me.IsRenardByte(Me.m_rowindex(brinx)) Then SentBadByte(Me.m_rowindex(brinx), buflen, pic\56) ''paranoid
				If buflen < m_outbuf.Length Then Me.m_outbuf(buflen) = Me.m_rowindex(brinx) ''send row index byte; avoid outbuf overflow
				buflen += 1 ''maintain count, even if outbuf overflows (so we know how bad is was)
40:
				For row = 0 To 7
''					If buflen >= m_outbuf.Length Then LogMsg("ERROR: outbuf overflow at row " & row & " of brinx " & brinx & ", brlevel " & brlevel & ", thought I needed 4+2*" & numbr & "+" & numrows & "=" & (2+2 + 2*numbr + numrows) & ", outbuf so far is " & buflen & ":" & DumpHex(Me.m_outbuf, buflen, 80))
''					If Not String.IsNullOrEmpty(Me.m_logfile) Then LogMsg("err 9 debug: brinx " & brinx & ", row " & row & ", buflen " & buflen)
					If Me.m_columns(brinx, row) <> 0 Then ''send columns for this row
						If Me.IsRenardByte(Me.m_columns(brinx, row)) Then SentBadByte(Me.m_columns(brinx, row), buflen, pic\56) ''paranoid
						If buflen < m_outbuf.Length Then Me.m_outbuf(buflen) = Me.m_columns(brinx, row) ''avoid outbuf overflow
						buflen += 1 ''maintain count, even if outbuf overflows (so we know how bad is was)
					End If
					numcols += Me.NumBits(Me.m_columns(brinx, row))
				Next row
			Next brlevel
42:
			If buflen < m_outbuf.Length Then Me.m_outbuf(buflen) = 0: ''end of list indicator; avoid outbuf overflow
			buflen += 1 ''maintain count, even if outbuf overflows (so we know how bad is was)
''			If pic = 1 Then ''last packet; send sync to kick out of packet receive loop (to allow console/debug commands); send this on all, in case last one is dropped as a dup
''TODO: trailing Sync might not be needed any more
				If buflen < m_outbuf.Length Then Me.m_outbuf(buflen) = Renard_SyncByte: ''&h7E; avoid outbuf overflow
				buflen += 1 ''maintain count, even if outbuf overflows (so we know how bad is was)
				protolen += 1
''			End If
			If buflen > m_outbuf.Length Then ''outbuf overflowed; this is a bug if it happens
				Throw New Exception(String.Format("ERROR: outbuf overflow, thought I needed 5+2*{0}+{1}={2}, but really needed {3}:", numbr, numrows, 3+2 + 2*numbr + numrows, buflen) & DumpHex(Me.m_outbuf, Math.Min(buflen, Me.m_outbuf.Length), 80))
			End If
			If (pic\56 = Me.m_bufdedup) And (buflen = Me.m_prevbuflen) Then ''compare current buffer to previous buffer
				For i = 1 To buflen
					If Me.m_outbuf(i-1) <> Me.m_prevbuf(i-1) Then Exit For ''need to send output buffer
				Next i
				If i > buflen Then ''outbuf was same as last time; skip it
					Me.num_dups += 1
					If Me.m_trace Then LogMsg(String.Format("Chipiplex[{0}]= duplicate buffer on {1} discarded", Me.total_evts, Me.m_bufdedup))
					Continue For
				End If
			End If
			Me.total_outlen += buflen
			Me.protocol_outlen += protolen
			Me.total_levels += numbr
			Me.total_rows += numrows
			Me.total_columns += numcols
			If (numbr > 0) And (numbr < Me.min_levels) Then Me.min_levels = numbr
			If numbr > Me.max_levels Then Me.max_levels = numbr
			If (numrows > 0) And (numrows < Me.min_rows) Then Me.min_rows = numrows
			If numrows > Me.max_rows Then Me.max_rows = numrows
			If (numcols > 0) And (numcols < Me.min_columns) Then Me.min_columns = numcols
			If numcols > Me.max_columns Then Me.max_columns = numcols
			If (buflen > 0) And (buflen < Me.min_outlen) Then Me.min_outlen = buflen
			If buflen > Me.max_outlen Then Me.max_outlen = buflen
44:
''TODO: append multiple bufs together, and only write once
			Me.m_selectedPort.Write(Me.m_outbuf, 0, buflen)
			If pic\56 = Me.m_bufdedup Then ''save current buffer to compare to next time
				For i = 1 To buflen 
					Me.m_prevbuf(i-1) = Me.m_outbuf(i-1)
				Next i
				Me.m_prevbuflen = buflen
			End If
46:
			If Me.m_trace Then LogMsg(String.Format("Chipiplex[{0}]{1} {2}+{3} bytes out: ", Me.total_evts, status, protolen, buflen - protolen) & DumpHex(Me.m_outbuf, buflen, 80))
		Next pic
		Exit Sub
	errh:
		ReportError("ChipiplexedEvent")
	End Sub
#End If
#endif

