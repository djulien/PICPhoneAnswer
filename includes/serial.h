////////////////////////////////////////////////////////////////////////////////
////
/// Serial input port
//

#ifndef _SERIAL_H
#define _SERIAL_H

#include "compiler.h" //need HIGHBAUD_OK for BAUDCTL, BRSCALER, SPBRG values
#include "clock.h" //need (defaulted) CLOCK_FREQ for SPBRG value

#define KBAUD  *1000UL

//#define SPBRG_VALUE(baud)  (CLOCK_FREQ / INSTR_CYCLES / (baud) - 1)
//wrong! #define SPBRG_VALUE(baud)  (InstrPerSec(CLOCK_FREQ) / (baud) - 1)
#define SPBRG_VALUE(baud)   SPBRG_Preset(CLOCK_FREQ, baud)

#ifndef BAUD_RATE
 #define BAUD_RATE  115200UL //gives SPBRG == 39 @ 4.6 MIPS (18.4 MHz)
#endif


#define MY_BAUDCTL  \
(0  /*;default to all bits off, then turn on as needed*/ \
	| IIFNZ(HIGHBAUD_OK, /*1<<BRG16*/ _BRG16)  /*;16-bit baud rate generator; SEE ERRATA PAGE 2 REGARDING BRGH && BRG16; NOT needed since we are NOT using low baud rates*/ \
	| IIFNZ(FALSE, /*1<<SCKP*/ _SCKP)  /*;synchronous clock polarity select: 1 => xmit inverted; 0 => xmit non-inverted*/ \
	| IIFNZ(FALSE, /*1<<WUE*/ _WUE)  /*;no wakeup-enable*/ \
	| IIFNZ(FALSE, /*1<<ABDEN*/ _ABDEN)  /*;no auto-baud rate detect (not reliable - consumes first char, which must be a Break)*/ \
)


//;Transmit Status and Control (TXSTA) register (page 87, 100):
//;This register will be set at startup, but individual bits may be turned off/on again later to reset the UART.
#define MY_TXSTA  \
(0  /*;default to all bits off, then turn on as needed*/ \
	| IIFNZ(TRUE, /*1<<BRGH*/ _BRGH)  /*;high baud rates*/ \
	| IIFNZ(FALSE, /*1<<SYNC*/ _SYNC)  /*;async mode*/ \
	| IIFNZ(FALSE, /*1<<TX9*/ _TX9)  /*;don't want 9 bit data for parity*/ \
	| IIFNZ(TRUE, /*1<<TXEN*/ _TXEN)  /*;transmit enable (sets TXIF interrupt bit)*/ \
	| IIFNZ(DONT_CARE, /*1<<CSRC*/ _CSRC)  /*;clock source select; internal for sync mode (master); don't care in async mode; Errata for other devices says to set this even though it's a don't care*/ \
	| IIFNZ(FALSE, /*1<<SENDB*/ _SENDB)  /*;don't send break on next char (async mode only)*/ \
	| IIFNZ(FALSE, /*1<<TX9D*/ _TX9D)  /*;clear 9th bit*/ \
)


//;Receive Status and Control (RCSTA) register (page 90, 100):
//;This register will be set at startup, but individual bits may be turned off/on again later to reset the UART
#define MY_RCSTA  \
(0  /*;default to all bits off, then turn on as needed*/ \
	| IIFNZ(TRUE, /*1<<SPEN*/ /*_SPEN*/ 0x80UL)  /*;serial port enabled*/ \
	| IIFNZ(FALSE, /*1<<RX9*/ _RX9)  /*;9 bit receive disabled (no parity)*/ \
	| IIFNZ(DONT_CARE, /*1<<SREN*/ _SREN)  /*;single receive enable; don't care in async mode*/ \
	| IIFNZ(TRUE, /*1<<CREN*/ _CREN)  /*;continuous receive enabled*/ \
	| IIFNZ(DONT_CARE, /*1<<ADDEN*/ _ADDEN)  /*;address detect disabled (not used for async mode)*/ \
	| IIFNZ(FALSE, /*1<<RX9D*/ _RX9D)  /*;clear 9th bit*/ \
)


//;SPBRG register:
//;Microchip baud rate formulas on page 97 don't quite match charts on pages following in some cases,
//;  but I think it is supposed to be:
//;   when brgh=brg16=1   spbrg_value = (xtal_freq/(baudrate*4))-1
//;   when brgh=brg16=0   spbrg_value = (xtal_freq/(baudrate*64))-1
//;   when brgh!=brg16   spbrg_value = (xtal_freq/(baudrate*16))-1
//#define kbaud(freq)  #v((freq)/1000)kb  ;used for text messages only
//#define BRSCALER  ( ( (MY_BAUDCTL & (1<<BRG16))? 1: 4) * ( (MY_TXSTA & (1<<BRGH))? 1: 4) )  //;denominator portion that depends on BRGH + BRG16
//macro body too long #define BRSCALER  (IIF(MY_BAUDCTL & (1<<BRG16), 1, 4) * IIF(MY_TXSTA & (1<<BRGH), 1, 4))  //;denominator portion that depends on BRGH + BRG16
#define BRSCALER  (IIF(MY_BAUDCTL & /*(1<<BRG16)*/ _BRG16, 1UL, 4UL) * IIF(MY_TXSTA & /*(1<<BRGH)*/ _BRGH, 1UL, 4UL))  //;denominator portion that depends on BRGH + BRG16
#define SPBRG_Preset(clock, baud)  (rdiv(InstrPerSec(clock) / BRSCALER, baud) - 1)  //;s/w programmable baud rate generator value
#define BAUD_ERRPCT(clock_ignored, baud)  (ABS((100 * BRG_MULT) / ((baud)/100) - 10000 * (BRG_MULT / (baud))) / (BRG_MULT / (baud)))
//kludge: avoid macro body length errors:
//#if InstrPerSec(CLOCK_FREQ / BRSCALER) == 8 MHz
// #define BRG_MULT  8000000 //8 MIPS
//#elif InstrPerSec(CLOCK_FREQ / BRSCALER) == 5 MHz
// #define BRG_MULT  5000000 //5 MIPS
//#elif InstrPerSec(CLOCK_FREQ / BRSCALER) == 4608000 //4.6 MIPS (18.432 MHz clock)
// #define BRG_MULT  4608000 //4.6 MIPS
//#else
 #define BRG_MULT  InstrPerSec(CLOCK_FREQ / BRSCALER)
//#endif
//#endif
//#endif


#ifdef SERIAL_DEBUG //debug
 #ifndef debug
  #define debug() //define debug chain
 #endif
//define globals to shorten symbol names (local vars use function name as prefix):
    volatile AT_NONBANKED(0) uint8_t my_baudctl_debug; //= MY_BAUDCTL; //should be 3-1 for 2.67 Mbaud
    volatile AT_NONBANKED(0) uint8_t my_txsta_debug; //= MY_TXSTA; //should be 0x24 ;set TXEN (enable xmiter) and reset SYNC (set async mode) after setting SPBRG
    volatile AT_NONBANKED(0) uint8_t my_rcsta_debug; //= MY_RCSTA; //should be 0x90 ;set SPEN (enable UART and set TX pin for output + RX pin for input) and set CREN (continuous rcv) to enable rcv
    volatile AT_NONBANKED(0) uint32_t baud_debug; //= BAUD_RATE;
	volatile AT_NONBANKED(0) uint16_t spbrg16_debug; //= SPBRG_VALUE(BAUD_RATE); //MAX_BAUD); //will go do some other stuff after this, so don't need to wait for brg to stabilize
    volatile AT_NONBANKED(0) uint8_t brscalar_debug; //= BRSCALER;
    volatile AT_NONBANKED(0) uint16_t baud_errpct_debug; //= BAUD_ERRPCT(CLOCK_FREQ, BAUD_RATE);
 INLINE void serial_debug(void)
 {
    debug(); //incl prev debug info
    my_baudctl_debug = MY_BAUDCTL; //should be 0x08 for 16-bit BRG (high baud rate)
    my_txsta_debug = MY_TXSTA; //should be 0x24 ;set TXEN (enable xmiter) and reset SYNC (set async mode) after setting SPBRG
    my_rcsta_debug = MY_RCSTA; //should be 0x90 ;set SPEN (enable UART and set TX pin for output + RX pin for input) and set CREN (continuous rcv) to enable rcv
    baud_debug = BAUD_RATE; //115200 == 0x1,c200, 9600 == 0x2580
	spbrg16_debug = SPBRG_VALUE(BAUD_RATE); //MAX_BAUD); //should be 2 for 2.67 Mbaud @8 MIPS, 39 == 0x27 for 115,200 @4.6 MIPS, or 103 == 0x67 for 9,600 @1 MIPS
    brscalar_debug = BRSCALER;
    baud_errpct_debug = BAUD_ERRPCT(CLOCK_FREQ, BAUD_RATE); //0x10 == 0.16% (9,600 @1 MIPS)
 }
 #undef debug
 #define debug()  serial_debug()
#endif

#if 0
#if (BRSCALER == 1) && (DEVICE == PIC16F688) //high baud rates
// #ifdef PIC16F688
 #if HIGHBAUD_OK  //;only set this if you are using 16F688 rev A4 or later
  #warning YELLOW_MSG "[CAUTION] 16F688.A3 errata says NOT to use BRG16 and BRGH together: only safe on A4 and later silicon"  //;paranoid self-check
 #else //HIGHBAUD_OK
  #error RED_MSG "[ERROR] 16F688.A3 errata says NOT to use BRG16 and BRGH together: only safe on A4 and later silicon"  //;paranoid self-check
 #endif //HIGHBAUD_OK
// #endif //_PIC16F688
#endif //BRSCALER == 1
#endif


#if 0
#if MAX_BAUD == 500 KBAUD
 #define MAX_BAUD_tostr  500 kbaud
#elif MAX_BAUD == 250 KBAUD
 #define MAX_BAUD_tostr  250 kbaud
#elif MAX_BAUD == 115.2 KBAUD
 #define MAX_BAUD_tostr  115 kbaud
#elif MAX_BAUD == 57.6 KBAUD
 #define MAX_BAUD_tostr  57 kbaud
#else
 #define MAX_BAUD_tostr  MAX_BAUD
#endif
//#endif
//#endif
//#endif
 
#if BRSCALER == 1
 #define BRSCALER_tostr  1
#elif BRSCALER == 4
 #define BRSCALER_tostr  4
#elif BRSCALER == 16
 #define BRSCALER_tostr  16
#endif
//#endif
//#endif

//57k = 138 @32 MHz (.08% err), 86 @20 MHz (.22% err), 79 @18.432 MHz (0% err); send pad char 1x/400 bytes @20 MHz
//115k = 68 @32 MHz (.64% err), 42 @20 MHz (.94% err), 39 @18.432 MHz (0% err); send pad char 1x/100 bytes @20 MHz
//250k = 31 @32 MHz (0% err), 19 @20 MHz (0% err), 17 @18.432 MHz (2.34% err); send pad char 1x/40 bytes @18.432MHz

//#if MaxBaudRate(CLOCK_FREQ) < BAUD_RATE * 95/100 //;desired baud rate is not attainable (accurately), down-grade it
#if BAUD_ERRPCT(CLOCK_FREQ, MAX_BAUD) < 1 //250k baud @32 or 20 MHz, or 57k/115k @18.432 MHz
 #define BAUD_ERRPCT_tostr  < .01 //exact
#elif BAUD_ERRPCT(CLOCK_FREQ, MAX_BAUD) == 8 //57k baud @32 MHz
 #define BAUD_ERRPCT_tostr  .08
#elif BAUD_ERRPCT(CLOCK_FREQ, MAX_BAUD) == 22 //57k baud @20 MHz
 #define BAUD_ERRPCT_tostr  .22
#elif BAUD_ERRPCT(CLOCK_FREQ, MAX_BAUD) == 64 //115k baud @32 MHz
 #define BAUD_ERRPCT_tostr  .64
#elif BAUD_ERRPCT(CLOCK_FREQ, MAX_BAUD) == 94 //115k baud @20 MHz
 #define BAUD_ERRPCT_tostr  .94
#elif BAUD_ERRPCT(CLOCK_FREQ, MAX_BAUD) == 240 //234 //250k baud @18.432 MHz; kludge: arithmetic errors
 #define BAUD_ERRPCT_tostr  2.34
#else
 #define BAUD_ERRPCT_tostr  BAUD_ERRPCT(CLOCK_FREQ, MAX_BAUD)
#endif
//#endif
//#endif
//#endif
//#endif
//#endif
#endif


//define symbols for important status bits (makes debug easier):
//;check if EUSART input/output buffers are empty/full:
//If yesno is a const, compiler should optimize out the false case.
//#if _GCC_ //simulate data stream (for test/debug)
// #define IsCharAvailable(yesno)  ((yesno)? (instrptr != instreame): (instrptr == instreame))
// #define IsRoomAvailable(yesno)  (yesno)
//#else //_GCC_
// #define IsCharAvailable(yesno)  ((yesno)? bitof_r2l(pir1, RCIF): !bitof_r2l(pir1, RCIF))
// #define IsRoomAvailable(yesno)  ((yesno)? bitof_r2l(pir1, TXIF): !bitof_r2l(pir1, TXIF))
//#endif //_GCC_
//volatile bit IsCharAvailable @adrsof(pir1).RCIF;
#define IsCharAvailable  RCIF
//volatile bit IsRoomAvailable @PIR1_ADDR.TXIF;
#define IsRoomAvailable  TXIF
//volatile bit HasInbuf @PIR1_ADDR.RCIF; //char received
//volatile bit HasFramingError @adrsof(rcsta).FERR; //serial input frame error; input char is junk
#define HasFramingError  FERR //serial input frame error; input char is junk
//volatile bit HasOverrunError @adrsof(rcsta).OERR; //serial input buffer overflow; input char is preserved
#define HasOverrunError  OERR //serial input buffer overflow; input char is preserved
//#define HasAnyError  (rcsta & ((1<<FERR) | (1<<OERR)) //CAUTION: must check Frame error before read rcreg, Overrun error after
#define HasAnyError  (FERR || OERR) //CAUTION: must check Frame error before read rcreg, Overrun error after
//volatile bit HasOutbuf @PIR1_ADDR.TXIF; //space available in outbuf


//TODO: consolidate with other bit vars
BANK0 volatile union
{
    struct
    {
        uint8_t frerr: 1;
        uint8_t overr: 1;
        uint8_t unused: 6;
    } bits;
    uint8_t allbits;
} serial_state;
#define serial_frerr  serial_state.bits.frerr
#define serial_overr  serial_state.bits.overr


//turn serial port off & on:
//This is used after a BRG change or OERR.
//inline void Toggle_serio()
//	setbit(rcsta, SPEN, FALSE);  //;turn off receive to reset FERR (page 92)
//	setbit(rcsta, SPEN, TRUE);  //;turn on receive again
//	setbit(rcsta, CREN, FALSE); //turn off to reset OERR (page 92)
//	setbit(rcsta, CREN, TRUE); //re-enable receive


//turn serial port off & on:
//This is used after a BRG change or OERR.
//inline void Toggle_serio()
//	setbit(rcsta, SPEN, FALSE);  //;turn off receive to reset FERR (page 92)
//	setbit(rcsta, SPEN, TRUE);  //;turn on receive again
//	setbit(rcsta, CREN, FALSE); //turn off to reset OERR (page 92)
//	setbit(rcsta, CREN, TRUE); //re-enable receive
#define Toggle_serio(ctlbit)  \
{ \
	ctlbit = FALSE; \
	ctlbit = TRUE; \
}


//flush serial in port:
//;http://www.pic101.com/mcgahee/picuart.zip says to do this 3x; 2 are probably for the input buffer and 1 for an in-progress char
INLINE void Flush_serial(void)
{
    LABDCL(0x50);
	REPEAT(3, WREG = RCREG); //rcreg;
//	WREG = rcreg;
//	WREG = rcreg;
}
//#define Flush_serial()  REPEAT(3, WREG = RCREG) //rcreg;


//try to clear frame/overrun errors:
//overrun errors would be caused by bugs (not polling rcif frequently enough), so assume those won't happen 
//frame errors could be due to a variety of environmental or config issues, so check those more carefully
//only called on frame error
non_inline void SerialErrorReset(void)
{
//	ONPAGE(PROTOCOL_PAGE); //put on same page as protocol handler to reduce page selects

//	WREG = rcreg;
//	SerialWaiting = FALSE;
//	if (!HasFramingError && !HasOverrunError) return; //no errors
//	inc_nowrap(ioerrs);
	if (!HasOverrunError) //try to clear individual frame error; only way to clear overrun error is to reset serial port
	{
        serial_frerr = 1;
//		WREG = rcreg; if (!HasFramingError) return; //clear FERR on first char in FIFO
		REPEAT(2, { WREG = RCREG; if (!HasFramingError) return; }); //clear next FERR; frame error was successfully cleared
	}
    serial_overr = 1;
	Toggle(SPEN); //if all chars in FIFO have FERRs, this is the only way to clear it (page 92); NOTE: CREN will not clear FERR; SPEN or CREN will clear OERR
//	GetChar_WREG(); //this will clear FERR but not OERR (page 91)
	Flush_serial(); //FERR will clear as chars are read; this will also clear pir1.RCIF; is this needed?
//	NoSerialWaiting = TRUE; //clear char-available flag for SerialCheck
}


//check if serial data is waiting:
//serial data is infrequent (40 usec @250 kbaud, 87 usec @115 kbaud, 174 usec @57 kbaud), so this is optimized for the case of no serial data waiting
//frame errors occur if RS485 receiver line is open (EUSART sees low and starts async rcv process), so frame errors can be filtered out to avoid false interrupts to timers and demo sequence playback
//otherwise, treat frame/overrun errors as input so caller can take the appropriate action
//if there's no char available, then there shouldn't be frame or overrun errors to check either, so first check char available before other checks
#define RECEIVE_FRAMERRS  FALSE //TRUE //protocol handler (auto baud detect) wants to know about frame errors
#define FIXUP_FRAMERRS  TRUE //FALSE //protocol handler does not want frame errors, reset serial port and return to caller
#if 0
//generates "unreachable code" warnings
#define SerialCheck(ignore_frerrs, uponserial)  \
{ \
	/*WREG = pir1 & (1<<RCIF)*/; /*status.Z => no char available; 2 instr + banksel; NOTE: trashes WREG*/ \
	if (IsCharAvailable /*!NoSerialWaiting*/) \
	{ \
		if (ignore_frerrs && HasFramingError) SerialErrorReset(); /*NoSerialWaiting = TRUE*/ /*ignore frame errors so open RS485 line doesn't interrupt demo sequence*/ \
		else uponserial; \
	} \
}
#else
#define SerialCheck(ignore_frerrs, uponserial)  SerialCheck_inner(ignore_frerrs, uponserial) //kludge: force macro eval
#define SerialCheck_inner(ignore_frerrs, uponserial)  SerialCheck_##ignore_frerrs(uponserial)
#define SerialCheck_1(uponserial)  \
{ \
	/*WREG = pir1 & (1<<RCIF)*/; /*status.Z => no char available; 2 instr + banksel; NOTE: trashes WREG*/ \
	if (IsCharAvailable /*!NoSerialWaiting*/) \
	{ \
		if (HasFramingError) SerialErrorReset(); /*NoSerialWaiting = TRUE*/ /*ignore frame errors so open RS485 line doesn't interrupt demo sequence*/ \
		else uponserial; \
	} \
}
#define SerialCheck_0(uponserial)  \
{ \
	/*WREG = pir1 & (1<<RCIF)*/; /*status.Z => no char available; 2 instr + banksel; NOTE: trashes WREG*/ \
	if (IsCharAvailable /*!NoSerialWaiting*/) uponserial; \
}
#endif

//non_inline void protocol(void); //fwd ref
//handle frame errors in protocol() to reduce caller code space
//#define BusyPassthru(want_frerrs, ignore)  SerialCheck(/*FIXUP_FRAMERRS*/ /*RECEIVE_FRAMERRS*/ want_frerrs, protocol()) //nextpkt(WaitForSync)) //{ if (IsCharAvailable) protocol(where); } //EchoChar(); } //don't process any special chars; 2 instr if false, 6+5 instr if true (optimized for false)

//	SerialWaiting = FALSE; //already set; else no reason to call this function
//	if (IsCharAvailable /*pir1.RCIF*/) SerialWaiting = TRUE; //serial data received
//if there's no char available, then there shouldn't be frame or overrun errors to check either, so first check char available before other checks
//	if (!NoSerialWaiting /*HasFramingError*/ /*rcsta.FERR*/ && HasFramingError) NoSerialWaiting = TRUE; //ignore frame errors so open RS485 line doesn't interrupt demo sequence
//	SerialErrorReset(); //filter out frame/overrun errors; this will also clear pir1.RCIF
//#warning "FIX THIS"
//	NoSerialWaiting = TRUE;

//#undef SerialCheck
//#define SerialCheck(ignored)  NoSerialWaiting = TRUE
//#warning "FIX THIS @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"

//wait for char:
//filters out frame errors
inline void Wait4Char(void)
{
	for (;;)
	{
		SerialCheck(RECEIVE_FRAMERRS, return); //always return frame errors when caller wants serial data, so caller can decide how to handle it
//??		SerialCheck(FIXUP_FRAMERRS, return); //always return frame errors when caller wants serial data
//		if (!NoSerialWaiting) return;
		if (NEVER) break; //avoid dead code removal
	}
}


//get next char from serial port:
//echo is handled by caller
//also update i/o stats
//only called if next char is known to be available
//#define GetChar(buf)  { GetChar_WREG(); buf = WREG; WREG ^= RENARD_SYNC; } //status.Z => got SYNC
//#define GetCharNoEcho(buf)  { WantEcho = FALSE; GetChar(buf); }
#define GetChar(buf)  { GetChar_WREG(); buf = WREG; }
non_inline void GetChar_WREG(void)
{
//	ONPAGE(0);

//	while (!SerialWaiting)
	Wait4Char();
//	inc24(iochars.as24); //update I/O stats; count input rather than output
	WREG = RCREG; //return char to caller in WREG
}

//busy wait until char available:
//char is returned in WREG
//#define WaitChar(buf)  { WaitChar_WREG(); buf = WREG; }
//inline void WaitChar_WREG(void)
//{
//	Wait4Char(); //WANT_FRERR);
//		SerialErrorReset();
//	GetChar_WREG();
//}


//send byte down stream:
//assumes tx rate <= rx rate (no overruns)
#define PutChar(ch)  { WREG = ch; PutChar_WREG(); }
non_inline void PutChar_WREG(void)
{
//	if (!SerialWaiting) return;
	while (!IsRoomAvailable); //TODO: rework as event handler; CAUTION: for now this does a busy wait for output buffer space available
	TXREG = WREG; //rcreg;
//update I/O stats:
//	inc24(iochars);
//	SerialWaiting = FALSE;
}


#ifndef init
 #define init() //initialize function chain
#endif

//;initialize serial port:
INLINE void init_serial(void)
{
	init(); //prev init first

    LABDCL(0x51);
	BAUDCON = MY_BAUDCTL; //should be 0x08 for 16-bit BRG (high baud rate)
	/*rcsta.*/ SPEN = FALSE; //;disable serial port; some notes say this is required when changing the baud rate
//NOTE: will go do some other stuff after this, so don't need to wait for brg to stabilize
//NOTE: SPBRG is not contiguous on older PICs:
//	SPBRG_16 = SPBRG_VALUE(BAUD_RATE); //MAX_BAUD); //should be 39 == 0x27 for 115,200 @4.6 MIPS, 103 == 0x67 for 9,600 @1 MIPS
	SPBRGL = SPBRG_VALUE(BAUD_RATE) & 0xff; //MAX_BAUD);
	SPBRGH = SPBRG_VALUE(BAUD_RATE) >> 8; //MAX_BAUD);
//	spbrgH = SPBRG(BAUD_RATE) / 0x100;
//	SPBRGL = SPBRG_VALUE(BAUD_RATE); //MAX_BAUD);
//	SPBRGH = SPBRG_VALUE(BAUD_RATE) / 0x100;
//;turn serial port off/on after baud rate change:
	TXSTA = MY_TXSTA; //should be 0x24 ;set TXEN (enable xmiter) and reset SYNC (set async mode) after setting SPBRG
	RCSTA = MY_RCSTA; //should be 0x90 ;set SPEN (enable UART and set TX pin for output + RX pin for input) and set CREN (continuous rcv) to enable rcv
//;	setbit TXSTA, TXEN, TRUE  ;enable TX after RX is set
	Flush_serial(); //is this needed?
    serial_state.allbits = 0; //NOTE: caller might do this also; do it here just in case
}
#undef init
#define init()  init_serial() //function chain in lieu of static init


#ifndef on_rx
 #define on_rx() //initialize function chain
#endif


//NOTE: need macros here so "return" will exit caller
#define on_rx_check()  \
{ \
	if (!IsCharAvailable /*!NoSerialWaiting*/) return; \
    rx_frerr_check(); \
}
#ifdef REJECT_FRERRS
 #define rx_frerr_check()  \
	if (HasFramingError) { SerialErrorReset(); return; } //ignore frame errors so open RS485 line doesn't collect garbage
#else
 #define rx_frerr_check() //noop
#endif


#endif //ndef _SERIAL_H
//eof