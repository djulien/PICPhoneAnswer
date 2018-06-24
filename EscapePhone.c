//Escape room phone player:
//ASM-fixup script (C) 2010-2018 djulien

//detect:
//pk2cmd -p -i
//pk2cmd -?v
//to pgm:
//MPLAB build
//pk2cmd -PPIC16F688 -M -Y -Fbuild/EscapePhone.HEX
//testing:
//run code: pk2cmd -PPIC16F88 [-A5] -T 
//reset: pk2cmd -PPIC16F688 -R
//1. code gen
//look at LST file and check for correct opcodes
//step thru code using MPSIM
//2. DFPlayer
//ground pin IO 2 to play successive sound files
//NOTE: connect DFPlayer RX to PIC TX; DF Player datasheet refers to DF Player's UART, not other device's
//3. serial port
//connect FTDI USB-to-serial cable from PC to PIC
//run "dmesg | grep tty" to show which port to use (Linux)
//lsusb | grep -i serial
//run "sudo stty -F /dev/ttyUSB0 -a" to show baud rate
//run "sudo putty" at 9600 baud and examine DF Player commands sent by PIC
//(optional: recompile code with PUTTY_DEBUG to get displayable chars)

//TODO:
//- SDCC expand macro values in warning/error messages
//- SDCC banksel BUG (non-banked check in pcode.c)
//- SDCC BUG: loses "volatile" on bit vars within structs
//- SDCC poor code gen, reduce temps, allow non-banked stack vars
//- SDCC better bank select optimization (track entry/exit banks)
//- U16FIXUP not needed with UL? (clock consts)

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
//#define SERIAL_DEBUG
//#define TIMER1_DEBUG
//#define CLOCK_DEBUG
//#define SCAN_DEBUG
//#define MP3_TEST
//#define PUTTY_DEBUG


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


/////////////////////////////////////////////////////////////////////////////////
////
/// Misc timing:
//

//volatile uint2x8_t svtimer;
//void wait_50msec()
//{
//    reset(TMR1); //start fresh timer interval
////    svtimer.as_uint16 = tmr1_16;
//    while (!T1IF);
//}


//volatile NONBANKED uint8_t loop;
//void wait_1sec()
//{
//    for (loop = 0; loop < 20; ++loop)
//        wait_50msec();
//}


////////////////////////////////////////////////////////////////////////////////
////
/// Keyboard scanning:
//

//cols (3) on Port A, rows (4) on Port B/C:
//#define LED_PIN  RC2
//#define _LED_PIN  0xC2
//#define _LED_MASK  PORTMAP16(_LED_PIN)
//NOTE: VPP (RA3) in-only pin does not have WPU when used as I/O pin, so don't use it for column detect
#define COL6_PIN  RA2
#define _COL6_PIN  0xA2
#define COL6_MASK  PORTMAP16(_COL6_PIN)

#define COL7_PIN  RA4
#define _COL7_PIN  0xA4
#define COL7_MASK  PORTMAP16(_COL7_PIN)

#define COL8_PIN  RA5
#define _COL8_PIN  0xA5
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

//#define COLPORT  PORTA
#define ROWPORT  PORTBC
#define ROWPORT_MASK  PORTBC_MASK
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
//use weak pull-ups so open input pins don't float:
    NOT_RAPU = FALSE; //use WPUA
//    WPUA = PORTA_MASK & ~0;
//    IFWPUBC(WPUBC = PORTBC_MASK & ~0);
//set output pins, leave non-output pins as hi-Z:
    TRISA = PORTA_MASK & ~(Abits(ROWSOUT_MASK) | Abits(SEROUT_MASK)); //0b111111; //Abits(COL_PINS) | Abits(pin2bits16(SERIN);
    WPUA = WREG;
//NOTE: SPEN overrides TRIS
    TRISBC = PORTBC_MASK & ~(BCbits(ROWSOUT_MASK) | BCbits(SEROUT_MASK)); //0b010000; //BCbits(COL_PINS) | BCbits(pin2bits16(SERIN);
    IFWPUBC(WPUBC = WREG);
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
//set one row low at a time; keypress to ground will overcome weak pullups:
        ROWPORT = ROWPORT_MASK & ~ROWbits(ROW0_MASK);
        if (!COL6_PIN) retlw(7); //retlw(1);
        if (!COL7_PIN) retlw(9); //retlw(2);
        if (!COL8_PIN) retlw(8); //retlw(3);

        ROWPORT = ROWPORT_MASK & ~ROWbits(ROW1_MASK);
        if (!COL6_PIN) retlw(11); //"*"; //retlw(4);
        if (!COL7_PIN) retlw(12); //"#"; //retlw(5);
        if (!COL8_PIN) retlw(0+10); //retlw(6);

        ROWPORT = ROWPORT_MASK & ~ROWbits(ROW2_MASK);
        if (!COL6_PIN) retlw(1); //retlw(7);
        if (!COL7_PIN) retlw(3); //retlw(8);
        if (!COL8_PIN) retlw(2); //retlw(9);

        ROWPORT = ROWPORT_MASK & ~ROWbits(ROW3_MASK);
        if (!COL6_PIN) retlw(4); //retlw(11); //* key
        if (!COL7_PIN) retlw(6); //retlw(10); //0 key; avoid confusion with null/0
        if (!COL8_PIN) retlw(5); //retlw(12); //# key
//    }
//    CARRY = FALSE;
//    RETLW(0);
//    ZERO = TRUE;
    retlw(0); //no key pressed
}


////////////////////////////////////////////////////////////////////////////////
////
/// MP3 player (controlled by serial port):
//

//NOTE: VPP (RA3) does not have WPU, so use it for Busy detect
#define NBUSY_PIN  RA3
#define _NBUSY_PIN  0xA3
#define NBUSY_MASK  PORTMAP16(_NBUSY_PIN)

//DFPlayer commands (see datasheet):
#define NEXT_CMD  0x01 //next track; TEST ONLY
#define TRACK_CMD  0x03 //select track
#define VOLUME_CMD  0x06 //set volume 0..30
//#define LOOP_CMD  0x08
//#define RESET_CMD  0x0C
#define PLAYBACK_CMD  0x0D
//#define PAUSE_CMD  0x0E
//#define FOLDER_CMD  0x0F //select folder
#define STOP_CMD  0x16


#define NextTrack()  mp3_cmd(NEXT_CMD) //debug/test
#define Track(trk)  mp3_cmd(TRACK_CMD, trk)
#define Volume(vol)  mp3_cmd(VOLUME_CMD, vol)
//??#define Loop(trk)  mp3_cmd(LOOP_CMD, trk)
#define Playback()  mp3_cmd(PLAYBACK_CMD)
//#define Pause()  mp3_cmd(PAUSE_CMD)
#define Stop()  mp3_cmd(STOP_CMD)


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
//volatile NONBANKED uint8_t checksumL_put, checksumH_put; //non-banked to reduce bank selects during I/O
//volatile NONBANKED uint8_t param_put; //non-banked to reduce bank selects during I/O
//volatile NONBANKED struct
//{
//    uint8_t checksumL, checksumH;
//    uint8_t cmd, param;
//} mp3_data; //non-banked to reduce bank selects during I/O
//volatile NONBANKED uint8_t mp3_data[4];
//#define mp3checksumL  mp3_data[0]
//#define mp3checksumH  mp3_data[1]
//#define mp3cmd  mp3_data[2]
//#define mp3param  mp3_data[3]
//NOTE: SDCC doesn't inc address with mult-declares!
//sdcc wrong: volatile AT_NONBANKED(0) uint8_t mp3checksumL, mp3checksumH, mp3cmd, mp3param;
volatile NONBANKED uint8_t mp3cmd;
volatile NONBANKED uint8_t mp3param;
volatile NONBANKED uint8_t mp3checksumL;
volatile NONBANKED uint8_t mp3checksumH;


#ifdef PUTTY_DEBUG
//save original function:
 #define PutChar_putty(ch)  { WREG = ch; PutChar_original_WREG(); }
 INLINE void PutChar_original_WREG() { PutChar(WREG); }
//make chars displayable:
 volatile NONBANKED uint8_t putty_save;
 non_inline void PutChar_putty_WREG(void)
 {
    putty_save = WREG;
    PutChar(' ');
    swap(_putty_save, W); //NOTE: use ASM symbols here
    hexchar(WREG);
    PutChar(WREG);
    hexchar(putty_save);
    PutChar(WREG);
 }
 #undef PutChar
 #define PutChar(ch)  { WREG = ch; PutChar_putty_WREG(); }
#else
 #define PutChar_putty(ignored)  //noop
#endif


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


//send a command to MP3 player:
//#define mp3_cmd(cmd, param)  { mp3_data.cmd = cmd; mp3_data.param = param; _mp3_cmd(); }
#define mp3_cmd(...)  USE_ARG3(__VA_ARGS__, mp3_cmd_2ARGS, mp3_cmd_1ARG) (__VA_ARGS__)
#define mp3_cmd_1ARG(cmdd)  mp3_cmd_2ARGS(cmdd, 0) //; /*mp3_data.param = 0*/; mp3_send(); }
#define mp3_cmd_2ARGS(cmdd, paramm)  { mp3param = paramm; mp3cmd = cmdd; mp3_send(); } //NOTE: save param first, in case it's in WREG
non_inline void mp3_send(void)
{
//    uint8_t param = WREG;
//    BANK0 uint8_t param; param = WREG;
//sdcc no likey!    static AT_NONBANKED(2) uint8_t param; //non-banked to reduce bank selects during I/O
//    param_put = WREG;
//TODO:  while (now() < prev_cmd + 5) yield(); //give DF Player some time between each command
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
//#ifdef PUTTY_DEBUG
    PutChar_putty('\r'); //only for debug; easier to decipher DF Player commands
    PutChar_putty('\n');
//#endif
//    wait_50msec();
}


////////////////////////////////////////////////////////////////////////////////
////
/// Phone logic:
//

//sound file#s:
#define FUD  0 //-7 //kludge: custom sound files messed up
#define DTMF_TONE(n)  (n) // (WREG = n, if (ZERO) WREG = 10, WREG) //IIF(n, n, 10)  //1..12 (10 == "0", 11 == star, 12 == hash)
#define ANS_CORRECT  13
#define ANS_INCORRECT(n)  ((n) + 14-1) //14..18 for incorrect#1..5
#define DIAL_TONE  19
#define VOICE_DIGIT(n)  ((n) + 20 + FUD) //20..30 for "0" thru "10"
#define VOICE_LETTER(ch)  ((ch) - 'A' + 1 + 31-1 + FUD) //31..56 for "A" thru "Z"


//volatile uint2x8_t svtimer;
void wait_50msec()
{
    reset(TMR1); //start fresh timer interval
//    svtimer.as_uint16 = tmr1_16;
    while (!T1IF);
}


//wait after sending a command:
INLINE void mp3_send_wait()
{
    mp3_send();
    wait_50msec();
}
#ifdef mp3_send
 #undef mp3_send
#endif
#define mp3_send()  mp3_send_wait()


volatile NONBANKED uint8_t loop;
void wait_1sec()
{
    for (loop = 0; loop < 20; ++loop)
        wait_50msec();
}


#if 0
//wait even longer after starting playback:
non_inline void Playback_wait()
{
    Playback();
    wait_1sec();
//wait for playback to finish:
//    while (NBUSY_PIN);
//    while (!NBUSY_PIN);
}
#ifdef Playback
 #undef Playback
#endif
#define Playback()  Playback_wait()
#endif


volatile NONBANKED uint8_t mp3loop;
non_inline void dial_tone()
{
//    NextTrack();
//    RETURN;
    wait_1sec(); //give DF Player time to init
    Volume(20); //15 is kind of faint; 30 is loud; TODO: adjust volume?
    Track(DIAL_TONE); Playback();
//    Track(DTMF_TONE(0));
//    Track(VOICE_LETTER('A'));
#if 0
//test all sounds:
    for (mp3loop = 1; mp3loop < 60; ++mp3loop)
    {
//        Track(VOICE_DIGIT(mp3loop)); Playback();
        NextTrack(); REPEAT(4, wait_1sec());
    }
#endif
#if 0
//    Track(VOICE_DIGIT(0));
    Track(VOICE_DIGIT(1)); Playback();
    Track(VOICE_DIGIT(2)); Playback();
    Track(VOICE_DIGIT(3)); Playback();
    Track(VOICE_DIGIT(4)); Playback();
    Track(VOICE_DIGIT(5)); Playback();
    Track(VOICE_DIGIT(6)); Playback();
    Track(VOICE_DIGIT(7)); Playback();
    Track(VOICE_DIGIT(8)); Playback();
    Track(VOICE_DIGIT(9)); Playback();
    Track(VOICE_DIGIT(10)); Playback();
#endif
}


//dialed number buffer + debounce:
//#define NUM_KEYS  4
//#define prev_key  INDF0
//NOTE: SDCC doesn't inc address with mult-declares!
//sdcc wrong: volatile AT_NONBANKED(4) uint8_t debounce, cur_key, prev_key; //, dialed[NUM_KEYS];
//volatile NONBANKED uint8_t prev_key;
//volatile NONBANKED uint8_t delay;

//get next key press:
volatile NONBANKED uint8_t cur_key;
volatile NONBANKED uint8_t debounce;
#define getkey(var)  { getkey_WREG(); var = WREG; }
void getkey_WREG()
{
//TODO: timeout
    cur_key = 0; //no key pressed
    for (;; wait_50msec())
    {
        debounce = cur_key;
        keypress(cur_key);
        WREG = debounce ^ cur_key; if (!ZERO) continue; //key not stable; check again
        if (!cur_key) continue; //no key pressed
        if (cur_key == 10) cur_key = 0; //kludge: map back to "0"
        break;
    }
    WREG = VOICE_DIGIT(cur_key); //"talking phone"; easier debug
//    WREG = DTMF_TONE(debounce);
    Track(WREG); Playback(); //correct ~ 7 sec, incorrect ~ 10 sec
    WREG = cur_key; //return key press to caller
}


//kludge: define SDCC bit var
typedef struct
{
//    unsigned      : 7;
    unsigned Result: 1; //lsb
//    unsigned ALWAYS: 1;
    unsigned :7; //unused
} MyBits_t;
volatile NONBANKED MyBits_t MY_BITS;
#define Result  MY_BITS.Result


//correct ph# is (714)593-3900
//volatile NONBANKED uint8_t result;
void accept_call()
{
    Result = TRUE; //assume correct until proven otherwise
    getkey(WREG); xorlw(7); if (!ZERO) Result = FALSE; //wrong
    getkey(WREG); xorlw(1); if (!ZERO) Result = FALSE; //wrong
    getkey(WREG); xorlw(4); if (!ZERO) Result = FALSE; //wrong
    getkey(WREG); xorlw(5); if (!ZERO) Result = FALSE; //wrong
    getkey(WREG); xorlw(9); if (!ZERO) Result = FALSE; //wrong
    getkey(WREG); xorlw(3); if (!ZERO) Result = FALSE; //wrong
    getkey(WREG); xorlw(3); if (!ZERO) Result = FALSE; //wrong
    getkey(WREG); xorlw(9); if (!ZERO) Result = FALSE; //wrong
    getkey(WREG); xorlw(0); if (!ZERO) Result = FALSE; //wrong
    getkey(WREG); xorlw(0); if (!ZERO) Result = FALSE; //wrong
//wait until all digits dialed before revealing result:
    wait_1sec(); //allow time for call to go thru
//TODO    Track(RING_TONE); Playback(); REPEAT(4, wait_1sec());
//only play one result, then quit (caller must hang up and dial again):
    WREG = ANS_CORRECT;
    if (!Result) WREG = ANS_INCORRECT(1);
    Track(WREG); Playback(); //correct ~ 7 sec, incorrect ~ 10 sec
    idle(); //uses less power than busy loop; //--PCL;
}


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
//    Indirect(FSR0, &dialed[0]); //leave FSR0 pointing to first digit
//    dialed[NUM_KEYS - 1] = 0; //init for end-of-number checking
//    cur_key = prev_key = 0; //init debounce/key up check on first key press
    dial_tone(); //play dial tone when handset is lifted (power on)
    accept_call();
}
#undef init
#define init()  phone_init() //function chain in lieu of static init


#if 0
INLINE void wait_4sec()
{
	on_tmr_1sec(); //prev event handlers first
//    WREG = delay; addlw(0x30); PutChar_putty(WREG); PutChar_putty(' ');
    --delay;
    WREG = delay;
    if (!ZERO) return;
    dial_tone();
    --PCL;
}
#undef on_tmr_1sec
#define on_tmr_1sec()  wait_4sec() //event handler function chain
#endif


#if 0
//keypress handler:
//debounce and play dtmf tone
INLINE void keypress_50msec(void)
{
	on_tmr_50msec(); //prev event handlers first
    debounce = cur_key; //new_key;
    keypress(cur_key); //new_key);
//    if (ZERO) return; //no key pressed
//sdcc wrong code    if (cur_key != debounce) return; //debounce: discard transients
    WREG = cur_key ^ debounce; if (!ZERO) return; //debounce: discard transients
//sdcc wrong code    if (/*(FSR0L != &dialed[0])? */ cur_key == prev_key) return; //no change
    WREG = cur_key ^ prev_key; if (ZERO) return; //no change
//    if (prev_key) Stop(); //cancel previous tone/key; TODO: is Stop() needed before another Playback()?
//  if (!_NBUSY_PIN)
#if 0
    RETURN; //TODO
//sdcc poor/wrong code    if (!cur_key) //key released
    WREG = cur_key; if (ZERO) //key released
    {
//sdcc poor/wrong code        if (prev_key) Stop(); //cancel previous tone/key; TODO: is Stop() needed before another Playback()?
        WREG = prev_key; if (!ZERO) Stop(); //cancel previous tone/key; TODO: is Stop() needed before another Playback()?
//        state
//        if (dialed[NUM_KEYS - 1]) return; //wait for more; TODO: timeout?
//TODO: check for correct number dialed
    }
//sdcc poor/wrong code    if (cur_key) { Track(DTMF_TONE(cur_key)); Playback(); } //INDF0_POSTINC = cur_key; return; } //key down: play dtmf tone and store new key
    WREG = cur_key; if (!ZERO) { Track(DTMF_TONE(cur_key)); Playback(); } //INDF0_POSTINC = cur_key; return; } //key down: play dtmf tone and store new key
    prev_key = cur_key;
//    if (dialed[0] == )
#endif
}
#undef on_tmr_50msec
#define on_tmr_50msec()  keypress_50msec() //event handler function chain
#endif


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
#define LED_PIN  BUSY_PIN
#define _LED_PIN  _BUSY_PIN
#define LED_MASK  BUSY_MASK //PORTMAP16(_LED_PIN)

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
//    dial_tone(); //already playing dial tone
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
//no    NUMBANKS(2); //reduce banksel overhead
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
