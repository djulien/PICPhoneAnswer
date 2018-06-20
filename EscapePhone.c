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
//#define SCAN_DEBUG
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


////////////////////////////////////////////////////////////////////////////////
////
/// Keyboard scanning:
//

//cols (3) on Port A, rows (4) on Port B/C:
//#define LED_PIN  RC2
//#define _LED_PIN  0xC2
//#define _LED_MASK  PORTMAP16(_LED_PIN)
#define COL6_PIN  RA5
#define _COL6_PIN  0xA5
#define COL6_MASK  PORTMAP16(_COL6_PIN)

#define COL7_PIN  RA4
#define _COL7_PIN  0xA4
#define COL7_MASK  PORTMAP16(_COL7_PIN)

#define COL8_PIN  RA2
#define _COL8_PIN  0xA2
#define COL8_MASK  PORTMAP16(_COL8_PIN)

#define ROW0_PIN  RC0
#define _ROW0_PIN  0xC0
#define ROW0_MASK  PORTMAP16(_ROW0_PIN)

#define ROW1_PIN  RC1
#define _ROW1_PIN  0xC1
#define ROW1_MASK  PORTMAP16(_ROW1_PIN)

#define ROW2_PIN  RC2
#define _ROW2_PIN  0xC2
#define ROW2_MASK  PORTMAP16(_ROW2_PIN)

#define ROW3_PIN  RC3
#define _ROW3_PIN  0xC3
#define ROW3_MASK  PORTMAP16(_ROW3_PIN)

#define COLPORT  PORTA
#define ROWPORT  PORTBC
#define COLbits(bits16)  Abits(bits16)
#define ROWbits(bits16)  BCbits(bits16)

//combine for generic port logic:
//rows = output, cols = input (allows slightly more efficient scanning):
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
    scan_debug_val1 = COL6_MASK; //0b100000,000000 == 0x20,0
    scan_debug_val2 = ROW3_MASK; //0b000000,001000 == 0x0,08
//    scan_debug_val3 = ROWSOUT_MASK;
    scan_debug_val3 = Abits(ROWSOUT_MASK); //0
    scan_debug_val4 = BCbits(ROWSOUT_MASK); //0x0f
    scan_debug_val5 = ROWbits(ROW1_MASK); //0b000010 == 2
//    scan_debug_val6 = ROW1_MASK;
    scan_debug_val6 = COLbits(ROW1_MASK); //0
    scan_debug_val7 = 0xff & ~0; //0xff

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
//enable digital I/O, disable analog functions:
	IFANSELA(ANSELA = 0); //must turn off analog functions for digital I/O
	IFANSELBC(ANSELBC = 0);
//weak pull-ups for input pins are not floating:
    WPUA = PORTA_MASK & ~0;
    IFWPUBC(WPUBC = PORTBC_MASK & ~0);
//set output pins, leave non-output pins as hi-Z:
    TRISA = PORTA_MASK & ~(Abits(ROWSOUT_MASK) | Abits(SEROUT_MASK)); //0b111111; //Abits(COL_PINS) | Abits(pin2bits16(SERIN);
    TRISBC = PORTBC_MASK & ~(BCbits(ROWSOUT_MASK) | BCbits(SEROUT_MASK)); //0b010000; //BCbits(COL_PINS) | BCbits(pin2bits16(SERIN);
//    PORTC = 0xff;
    PORTA = PORTBC = 0;
//    IFCM1CON0(CM1CON0 = 0xff & ~0); //;configure comparator inputs as digital I/O (no comparators); overrides TRISC (page 63, 44, 122); must be OFF for digital I/O to work! needed for PIC16F688; redundant with POR value for PIC16F182X
//	TRISA = ~PORTA_BITS & TRISA_INIT;
//	TRISBC = PORTBC_BITS & TRISBC_INIT;
//	PORTA = 0;
//	PORTBC = 0;
	init(); //do other init after Ports (to minimize side effects on external circuits); NOTE: no race condition occur with cooperative event handling (no interrupts)
}
#undef init
#define init()  port_init() //function chain in lieu of static init


//check for key pressed:
//ZERO = key-pressed flag, WREG = key#
#define keypress(var)  { keypress_WREG(); var = WREG; }
non_inline void keypress_WREG()
{
//    CARRY = TRUE;
 //   ZERO = FALSE;
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
//    ZERO = TRUE;
    retlw(0); //no key
}


////////////////////////////////////////////////////////////////////////////////
////
/// MP3 player (controlled by serial port):
//

//DFPlayer commands (see datasheet):
#define TRACK_CMD  0x03 //select track
#define VOLUME_CMD  0x06 //set volume 0..30
#define RESET_CMD  0x0C
#define PLAYBACK_CMD  0x0D
#define PAUSE_CMD  0x0E
#define FOLDER_CMD  0x0F //select folder

#define Track(trk)  mp3_cmd(TRACK_CMD, trk)
#define Volume(vol)  mp3_cmd(VOLUME_CMD, vol)
#define Playback()  mp3_cmd(PLAYBACK_CMD)


//command buffer:
#define START_BYTE  0x7E //firsst byte
#define VER_BYTE  0xFF //second byte
#define CMDLEN_BYTE  6 //3rd byte = #bytes following
//command = 4th byte
//feedback flag = 5th byte
//param = 6th, 7th bytes (high, low)
//checksum = 8th, 9th bytes (high, low)
#define END_BYTE  0xEF


//sdcc generates poor code: volatile AT_NONBANKED(0) uint2x8_t checksum_put; //non-banked to reduce bank selects during I/O
//sdcc generates extra temps with struct access, so don't use struct!
//volatile AT_NONBANKED(0) uint8_t checksumL_put, checksumH_put; //non-banked to reduce bank selects during I/O
//volatile AT_NONBANKED(2) uint8_t param_put; //non-banked to reduce bank selects during I/O
//volatile AT_NONBANKED(0) struct
//{
//    uint8_t checksumL, checksumH;
//    uint8_t cmd, param;
//} mp3_data; //non-banked to reduce bank selects during I/O
//volatile AT_NONBANKED(0) uint8_t mp3_data[4];
//#define mp3checksumL  mp3_data[0]
//#define mp3checksumH  mp3_data[1]
//#define mp3cmd  mp3_data[2]
//#define mp3param  mp3_data[3]
volatile AT_NONBANKED(0) uint8_t mp3checksumL, mp3checksumH, mp3cmd, mp3param;


//generate checksum during I/O:
//BANK0 uint16_t checksum;
#define PutChar_chksum(ch)  { WREG = ch; PutChar_chksum_WREG(); }
non_inline void PutChar_chksum_WREG(void)
{
//SDCC generates poor code; help it out here:
//    checksum.as_uint16 -= WREG; //checksum is -ve of version .. param bytes; subtract along the way to avoid extra negation at end
    mp3checksumL -= WREG; if (BORROW) --mp3checksumH;
    PutChar(WREG);
}
//#undef PutChar
//#define PutChar  PutChar_chksum


//#define mp3_cmd(cmd, param)  { mp3_data.cmd = cmd; mp3_data.param = param; _mp3_cmd(); }
#define mp3_cmd(...)  USE_ARG3(__VA_ARGS__, mp3_cmd_2ARGS, mp3_cmd_1ARG) (__VA_ARGS__)
#define mp3_cmd_1ARG(cmdd)  { mp3cmd = cmdd; /*mp3_data.param = 0*/; mp3_send(); }
#define mp3_cmd_2ARGS(cmdd, paramm)  { mp3cmd = cmdd; mp3param = paramm; mp3_send(); }
non_inline void mp3_send(void)
{
//    uint8_t param = WREG;
//    BANK0 uint8_t param; param = WREG;
//sdcc no likey!    static AT_NONBANKED(2) uint8_t param; //non-banked to reduce bank selects during I/O
//    param_put = WREG;

    PutChar(START_BYTE);
//    checksum_put.as_uint16 = 0; //start new checksum; excludes start byte
    mp3checksumL = mp3checksumH = 0; //start new checksum; excludes start byte
    PutChar_chksum(VER_BYTE);
    PutChar_chksum(CMDLEN_BYTE);
    PutChar_chksum(mp3cmd);
    PutChar_chksum(FALSE); //no feedback
    PutChar_chksum(0); PutChar_chksum(mp3param); //high, low
//    PutChar(checksum_put.high /*>> 8*/); PutChar(checksum_put.low /*& 0xff*/);
    PutChar(mp3checksumH /*>> 8*/); PutChar(mp3checksumL /*& 0xff*/);
    PutChar(END_BYTE);
}


////////////////////////////////////////////////////////////////////////////////
////
/// Phone logic:
//


//sound file#s:
#define DIAL_TONE  xx

non_inline void dial_tone()
{
    Volume(10); //TODO: adjust volume?
    Track(DIAL_TONE);
    Playback();
}


//remember number dialed:
#define NUM_KEYS  4
BANK0 uint8_t dialed[NUM_KEYS];
//debounce:
volatile AT_NONBANKED(4) debounce_key, cur_key;

INLINE void phone_init(void)
{
	init(); //prev init first
//TODO: need delay here?
//    cur_key = new_key = 0;
//    for (Indirect(FSR0, &dialed[NUM_KEYS]);;) //clear dialed number buf
//    {
//        INDF0_PREDEC_ = 0; //^--FSR0 = 0;
//        if (FSR0L == &dialed[0]) break; //leave FSR0 pointing to first digit
//    }
    Indirect(FSR0, &dialed[0]); //leave FSR0 pointing to first digit
    dialed[NUM_KEYS - 1] = 0; //init for end-of-number checking
    cur_key = dialed[0] = 0; //init debounce/key-up check on first key press
    dial_tone(); //play dial tone when handset is lifted (power on)
}
#undef init
#define init()  phone_test() //function chain in lieu of static init


//keypress handler:
//debounce and play dtmf tone
INLINE void keypress_50msec(void)
{
	on_tmr_50msec(); //prev event handlers first
    debounce_key = cur_key; //new_key;
    keypress(cur_key); //new_key);
//    if (ZERO) return; //no key pressed
    if (cur_key != debounce_key) return; //debounce: discard transients
    if (cur_key == INDF0) return; //no change
    if (cur_key) Stop(); //key up: cancel previous tone/key
    if (INDF0) { Track(INDF0); Playback(); } //key down: play dtmf tone for new key
    cur_key = new_key;
    if (
}
#undef on_tmr_50msec
#define on_tmr_50msec()  keypress_50msec() //event handler function chain


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
/// basic hardware tests (blink led, play mp3):
//

#ifdef CALIBRATE_TIMER
//NOTE: uses port_init() defined above
#define LED_PIN  RC2
#define _LED_PIN  0xC2
#define _LED_MASK  PORTMAP16(_LED_PIN)

//toggle LED @1 Hz:
//tests clock timing and overall hardware
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
//play first MP3 file:
//tests serial port + DFPlayer
INLINE void mp3_test(void)
{
	init(); //prev init first; NOTE: no race condition with cooperative event handling (no interrupts)
    Volume(10); //volume level 0 to 30
    Track(1); //play first mp3 (named "0001*.mp3")
    Playback();
    --PCL; //don't do anything else
}
#undef init
#define init()  mp3_test() //function chain in lieu of static init
#endif //def MP3_TEST


////////////////////////////////////////////////////////////////////////////////
////
/// Main logic:
//

#include "func_chains.h" //finalize function chains; NOTE: this adds call/return/banksel overhead, but makes debug easier


//init + evt handler loop:
void main(void)
{
//	ONPAGE(LEAST_PAGE); //put code where it will fit with no page selects
//    test();

    debug(); //incl debug info (not executable)
	init(); //1-time set up of control regs, memory, etc.
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
