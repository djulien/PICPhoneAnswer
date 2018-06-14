//;Misc definitions and arithmetic helper macros.
//NOTE: many of these are compiler-dependent.
//;==================================================================================================

#define FALSE  0
#define TRUE  1 //(-1) ;"-1" is safer, but "1" avoids loss-of-precision warnings when assigning to a bit var
#define MAYBE  2  //;not FALSE and not TRUE; used for tri-state logic
#define DONT_CARE  FALSE  //;arbitrary, but it's safer to turn off a don't-care feature than to leave it on

//convert #def symbols to bool value:
#define ASBOOL(symbol)  concat(BOOLVAL_, symbol) //CAUTION: nested macro must be used here to force macro value to be substituted for name
#define concat(pref, suff)  pref##suff //inner macro level to receive macro values rather than names
#define concat3(pref, middle, suff)  pref##middle##suff //inner macro level to receive macro values rather than names
#define BOOLVAL_  TRUE //default to TRUE if symbol is defined but doesn't have a value
#define BOOLVAL_1  TRUE
#define BOOLVAL_0  FALSE

#define EMPTY  //kludge: allows an empty parameter to be passed to macro; avoids "missing parameter" errors
#define eval(thing)  thing //inner macro level to receive macro values rather than names
#define NOPE(stmt)  //alternate notation to indicate there is a definite reason for not using stmt

#define ToHex(str)  0x0##str

//dummy bits:
//non-existent pins can be equated to these to simplify condition code
//#define FalseBit FALSE
//#define TrueBit TRUE
//bit JunkBit;

#define non_inline  //syntax tag to mark code that must *not* be inlined (usually due to hard returns or multiple callers)
#define ccp_for  //syntax tag for compile-time loop; handled by Cpreprocessor or scripting (not implemented yet)
//#define NOPARAM  //BoostC does not like missing macro params, so define a dummy symbol
#define non_volatile  //syntax tag to mark vars that are not volatile (ie, they only change under software control)

//main() return type:
//Different C compilers want different return types for main().
//This macro allows the type to be changed without editing main() itself.
#ifndef maintype
 #define maintype  void  //most compilers want this one
#endif
//allow global code to be wedged into main:

//;If we jumped back or fell thru to 0, something's seriously wrong (probably a coding bug or assembler error).
//;see STATUS.TO and STATUS.PD description (page 115, 118):
//;The only valid resets here should be power-on or brown-out; report all others.
//;Power-on reset: STATUS = 00011???, PCON = 0001000?
//;WDT reset: STATUS = 0000????, PCON = 000?00??
//;Brown-out: 00011???, PCON = 00010010
//;PK1 comes up initially with STATUS = 0x1D, or 0x1F after reset or 0xB?
#if 0
uint8 stup_flags;

inline void stup_status(void)
{
	init(); //prev init first
	stup_flags = status; //& ((1<<NOT_TO) | (1<<NOT_PD));
//;CAUTION: the PIC does not appear to reset while on the PK1 board if the COM port is still connected (due to stray Vss thru RC4/RC5?)
//#if PK1DEV+PK1SIM
//	iorlw 1<<NOT_TO  ;KLUDGE: PK1 board reset (power off/on via MPLAB) seems to behave like wdt reset?
//#endif
//	CONSTANT STARTUP_BITS = _LITERAL(0<<IRP | 0<<RP1 | 0<<RP0 | 1<<NOT_TO | 1<<NOT_PD)  ;line too long
//	if8 WREG, NE, STARTUP_BITS, FATAL(0xf0, STATUS, "restart: firmware bug or hardware error"),, BIGSTMT_HINT  ;check power-up reset (page 20, 117 of PIC spec)
//	if (!pcon.not_por) FATAL(0xf1, PCON, "restart: firmware bug or hardware error"),, BIGSTMT_HINT  ;check power-up reset (page 20, 117 of PIC spec)
	pcon.NOT_POR = TRUE; //allow non-power up to be detected next time
}
#undef init
#define init()  stup_status()
#endif


//Dummy initialization to avoid compiler warnings.
//Compiler switch doesn't seem to work, so use this instead.
//No dummy init is supplied if DEBUG is off, because it adds needless run-time overhead.
//NOTE: this must not alter WREG
//#ifdef DEBUG
// #define DummyInit(var)  var = 0
//#else
// #define DummyInit(var)  var
//#endif


//inline debug info:
#ifndef DEBUG2
 #define DEBUG2 //hard-code this one On for now
#endif

#if defined(DEBUG) && ASBOOL(DEBUG)
 #undef DEBUG
 #define DEBUG  TRUE //allow #if and #ifdef to both work
 #define IFDEBUG(stmt)  stmt
 #warning "[INFO] COMPILED FOR DEBUG.  USE ONLY FOR DEVELOPMENT/TEST, NOT FOR LIVE SHOWS."
// #define INLINE  //don't in-line code; easier debug
#else
// #warning "NO DEBUG"
 #ifdef DEBUG
  #undef DEBUG
 #endif
 #define IFDEBUG(stmt)
// #define INLINE  inline
#endif


#if defined(DEBUG2) && ASBOOL(DEBUG2)
 #undef DEBUG2
 #define DEBUG2  TRUE //allow #if and #ifdef to both work
 #define IFDEBUG2(stmt)  stmt
 #warning "[INFO] COMPILED FOR EXTRA DEBUG.  USE ONLY FOR DEVELOPMENT/TEST, NOT FOR LIVE SHOWS."
// #define INLINE  //don't in-line code; easier debug
#else
// #warning "NO DEBUG2"
 #ifdef DEBUG2
  #undef DEBUG2
 #endif
 #define IFDEBUG2(stmt)
// #define INLINE  inline
#endif


//stats control:
#ifndef WANT_STATS2
 #define WANT_STATS2 //hard-code this one On for now
#endif

#if defined(WANT_STATS) && ASBOOL(WANT_STATS)
 #undef WANT_STATS
 #define WANT_STATS  TRUE //allow #if and #ifdef to both work
 #define IFSTATS(stmt)  stmt
 #warning "[INFO] COMPILED FOR STATS.  DIMMING ACCURACY WILL BE DEGRADED."
#else
 #ifdef WANT_STATS
  #undef WANT_STATS
 #endif
 #define IFSTATS(stmt)
#endif

#if defined(WANT_STATS2) && ASBOOL(WANT_STATS2)
 #undef WANT_STATS2
 #define WANT_STATS2  TRUE //allow #if and #ifdef to both work
 #define IFSTATS2(stmt)  stmt
 #warning "[INFO] COMPILED FOR EXTRA STATS.  DIMMING ACCURACY WILL BE DEGRADED."
#else
 #ifdef WANT_STATS2
  #undef WANT_STATS2
 #endif
 #define IFSTATS2(stmt)
#endif

#ifndef Trace
 #define Trace(params)  //strip out trace info
#endif


//in-line IF:
//BoostC compiler (and maybe others) doesn't handle "?:" tenary operator at compile time, even with constants (it should).
//This macro gives equivalent functionality, but results in a constant expression that can be reduced at compile time.
//This generates *a lot* more efficent code, especially for * / % operators.
//CAUTION: when used with #defines these macros may generate long statements that could overflow preprocessors's macro body limit.
//#define IIF(tfval, tval, fval)  //((((tfval)!=0)*(tval)) + (((tfval)==0)*(fval))) //this one repeats tfval so there could be side effects ot macro body could get too long
#define IIF(tfval, tval, fval)  (((tfval)!=0) * ((tval)-(fval)) + (fval)) //this one assumes fval is short and tries to minimize macro body size and side effects by expaning tfval only once
//#define IIF0(tfval, tval, dummy)  (((tfval)!=0)*(tval))  //;shorter version if fval == 0; helps avoid macro body length errors
//#define IIFNZ(tfval, dummy, fval)  (((tfval)==0)*(fval))  //;shorter version if tval == 0; helps avoid macro body length errors
#if ((3 == 3) != 1) || ((3 == 4) != 0)  //paranoid check; IIF relies on this behavior
 #error "IIF broken: TRUE != 1 or FALSE != 0"
#endif

//simpler version if value is a simple const (TRUE or FALSE):
//#define CASEOF(tfval, tval, fval)  CASEOF_##tfval(tval, fval)
//#define CASEOF_TRUE(tval, fval)  tval //kludgey way to avoid syntax error
//#define CASEOF_FALSE(tval, fval)  fval //kludgey way to avoid syntax error


//;data type helpers:
#define byteof(value, n)  (((value) >> (8*(n))) & 0xff) //;extract a byte out of a value or register
//;#define BYTEOF(value, n)  (((byte*)&(value))[n]) //((bits) & (0xff<<(n*8)))
#define nibbleof(value, n)  (((value) >> (4*(n))) & 0xf) //;extract a nibble out of a value or register
//;#define s(regvar, bitvar)  ((unsigned)(&bitvar) - 8*(unsigned)(&regvar)) //bit position within a register
#define PutLower(reg, val)  { reg &= 0xF0; if (val) reg |= (val) & 0x0F; } //4 instr when used with const; only 2 instr if val == 0
#define PutUpper(reg, val)  { reg &= 0x0F; if (val) reg |= (val) & 0xF0; } //4 instr when used with const; only 2 instr if val == 0

//toggle a bit:
//takes 2 instr (movlw + xorwf), but overwrites WREG
//NOTE: requires ADRSOF #def
#define bit_flip(bitvar)  \
{ \
	volatile uint8 byte @adrsof(bitvar) / 8; \
	byte ^= (1 << adrsof(bitvar) % 8); \
}


//#ifdef _GCC_ //faked results for debug
// #define bitof_r2l(value, n)  \
// ( \
//	(&(value) == &intcon)? TRUE: \
//	(&(value) == &osccon)? TRUE: \
//	((value) & (1<<(n))) \
// )
//#else
 #define bitof_r2l(value, n)  ((value) & (1<<(n)))
 #define bitof8_r2l(value, n)  ((value) & (1<<((n)&7)))
 #define bitof_l2r(value, n)  ((value) & (0x80>>(n)))
 #define bitof8_l2r(value, n)  ((value) & (0x80>>((n)&7)))
//#endif
#define L2R8(bit)  (7 - ((bit) & 7))

//BoostC and HTC-Lite don't support shifting more than 8 bits, so take it a byte at a time:
#define BitOf(val, bit)  \
( \
	IIF(((bit) >> 3) == 3, ((val) / 0x1000000) & (1<<((bit)&7)), 0) + \
	IIF(((bit) >> 3) == 2, ((val) / 0x10000) & (1<<((bit)&7)), 0) + \
	IIF(((bit) >> 3) == 1, ((val) / 0x100) & (1<<((bit)&7)), 0) + \
	IIF(((bit) >> 3) == 0, (val) & (1<<(bit)), 0) + \
0)

//;NOTE: HTC-Lite doesn't support shifting more than 8 bits; replace these if needed:
#define shift32(val, bits)  IIF((bits) < 0, rshift32(val, 0-(bits)), lshift32(val, bits))
#define lshift32(val, bits)  ((val)<<(bits))
#define rshift32(val, bits)  ((val)>>(bits))
#if (shift32(1, 20) | shift32(1, 3)) != (1048576 + 8)
 #warning "[WARNING] Multi-byte shifting appears to be broken; using alternate macros to try to fix it."
 #undef lshift32
 #undef rshift32
 #define lshift32(val32, bits32)  IIF((bits32) < 8, lshift8(val32, bits32), lshift24(val32, (bits32) - 8) * 256)
 #define lshift24(val24, bits24)  IIF((bits24) < 8, lshift8(val24, bits24), lshift16(val24, (bits24) - 8) * 256)
 #define lshift16(val16, bits16)  IIF((bits16) < 8, lshift8(val16, bits16), lshift8(val16, (bits16) - 8) * 256)
 #define lshift8(val8, bits8)  ((val8)<<(bits8))
 #define rshift32(val32, bits32)  IIF((bits32) < 8, rshift8(val32, bits32), rshift24(val32, (bits32) - 8) / 256)
 #define rshift24(val24, bits24)  IIF((bits24) < 8, rshift8(val24, bits24), rshift16(val24, (bits24) - 8) / 256)
 #define rshift16(val16, bits16)  IIF((bits16) < 8, rshift8(val16, bits16), rshift8(val16, (bits16) - 8) / 256)
 #define rshift8(val8, bits8)  ((val8)>>(bits8))
 #if (shift32(1, 20) | shift32(1, 3)) != (1048576 + 8)
  #error "Multi-byte shift still broken"
 #endif
//#else
// #warning "[WARNING] Multi-byte shifting okay!"
#endif


//BoostC XOR function is broken in preprocessor:
//#define XOR(a, b)  ((a) ^ (b))
//#if byteof(XOR(0xA0, 0x720), 1) != 0x7
// #warning "[WARNING] XOR in preprocessor is broken; trying work-around"
// #undef XOR
// #define XOR(a, b)  (((a) & ~(b)) | (~(a) & (b)))
// #if byteof(XOR(0xA0, 0x720), 1) != 0x7
//  #error "[ERROR] XOR work-around is also broken"
// #endif
//#endif


//;misc arithmetic helpers:
#define non0(val)  (IIF(val, (val)-1, 0) + 1) //avoid divide-by-0 errors
//#define non0(val)  IIF(val, val, 1) //avoid divide-by-0 errors
#define rdiv(num, den)  (((num)+(den)/2)/non0(den))  //;rounded divide
#define divup(num, den)  (((num)+(den)-1)/non0(den))  //;round-up divide
#define entries(thing)  (sizeof(thing)/sizeof((thing)[0]))  //#entries in an array

//these are upper case to avoid expansion within text:
//IIF is used so constants can be reduced at compile time.
#define SGN(x)  IIF((x) < 0, -1, (x) != 0)  //;-1/0/+1
#define ABS(x)  IIF((x) < 0, 0-(x), x)  //;absolute value
#define MIN(x, y)  IIF((x) < (y), x, y)
#define MAX(x, y)  IIF((x) > (y), x, y)
//#define MIN(x, y)  ((x) + IIF0((y) < (x), (y) - (x), 0)) //;uses IIF0 to try to avoid macro body length problems
//#define MAX(x, y)  ((x) + IIF0((y) > (x), (y) - (x), 0)) //;uses IIF0 to try to avoid macro body length problems


//clear memory:
#define memclr(adrs, len)  \
{ \
	fsrH = (adrs) / 0x100; fsrL = (adrs) % 0x100; \
	WREG = len; \
	for (;;) \
	{ \
		indf = 0; \
		++fsrL; \
		if (!--WREG) break; \
	} \
}


//exchange 2 regs without using a temp:
inline void xchg(uint8& reg1, uint8& reg2)
{
#if 0 //uses a temp; 6 instr:
	temp = reg1;
	reg1 = reg2;
	reg2 = temp;
#endif
#if 0 //no temp needed; still uses 6 instr:
	reg1 ^= reg2; //reg1 ^ reg2
	reg2 ^= reg1; //reg2 ^ (reg1 ^ reg2) == reg1
	reg1 ^= reg2; //(reg1 ^ reg2) ^ reg1 == reg2
#endif
#if 1 //4 instr, no temps:
	WREG = reg1 ^ reg2;
//	xchg_partial(reg1, reg2, EMPTY);
	reg1 ^= WREG; //reg1 ^ (reg1 ^ reg2) == reg2
	reg2 ^= WREG; //reg2 ^ (reg1 ^ reg2) == reg1
#endif
}


//partial exchange of 2 regs:
//5 instr, no temps
#define xchg_partial(reg1, reg2, mask)  \
{ \
	WREG = reg1 ^ reg2; \
	mask; /*adjust WREG*/ \
	reg1 ^= WREG; /*reg1 ^ (reg1 ^ reg2) upper == reg2 upper | reg1 lower*/ \
	reg2 ^= WREG; /*reg2 ^ (reg1 ^ reg2) upper == reg1 upper | reg2 lower*/ \
}

//exchange reg and WREG without using a temp:
inline void xchg_WREG(uint8& reg)
{
//uses 2 temps; 6 instr:
//	temp = WREG;
//	temp2 = reg;
//	reg = temp;
//	WREG = temp2;
//no temps needed; 3 instr:
	reg ^= WREG; //reg ^ WREG
	WREG ^= reg; //WREG = WREG ^ reg; //WREG ^ (reg ^ WREG) == reg
	reg ^= WREG; //(reg ^ WREG) ^ reg == WREG
}

//exchange 2 bits:
//NOTE: bits might be in different positions, so can't just use &/|
//use Carry as a temp
//9 instr
//#define xchg_bit(bit1, bit2)  \
//{ \
//	bitcopy(bit1, Carry);
//	bitcopy(bit2, bit1);
//	bitcopy(Carry, bit2);
//}


//multiply WREG by 3 and save to reg:
inline void x3_WREG(uint8& reg)
{
	reg = WREG;
	reg += WREG; //x2
	reg += WREG; //x3
}


//multiply reg by 8:
//for values >= 32, Carry = lsb overflow
inline void x8(uint8& reg)
{
	swap(reg); //x16
	reg >>= 1; //x8, Carry = lsb overflow
}


//compile-time swap:
#define SWAP8(val) ((((val) << 4) & 0xF0) | (((val) >> 4) & 0x0F))

//;powers of 2 (up to 16):
//Equivalent to Log2(value) + 1.
//BoostC compiler doesn't handle "?:" operator at compile time, even with constants (it should).
//This macro allows a constant expression to be generated at compile time.
#ifdef __CC5X__
 #warning "CC5XBUG: multi-line macros not handled in #if"
#endif
#define NumBits8(val)  \
( \
	IIF((val)>>8, 99, 0) /*return bad value if out of range*/ + \
	IIF(((val)>>7) == 1, 8, 0) + \
	IIF(((val)>>6) == 1, 7, 0) + \
	IIF(((val)>>5) == 1, 6, 0) + \
	IIF(((val)>>4) == 1, 5, 0) + \
	IIF(((val)>>3) == 1, 4, 0) + \
	IIF(((val)>>2) == 1, 3, 0) + \
	IIF(((val)>>1) == 1, 2, 0) + \
	IIF(((val)>>0) == 1, 1, 0) + \
0)
#define NumBits16(val)  \
( \
	IIF((val)>>16, 99, 0) /*return bad value if out of range*/ + \
	IIF(((val)>>15) == 1, 16, 0) + \
	IIF(((val)>>14) == 1, 15, 0) + \
	IIF(((val)>>13) == 1, 14, 0) + \
	IIF(((val)>>12) == 1, 13, 0) + \
	IIF(((val)>>11) == 1, 12, 0) + \
	IIF(((val)>>10) == 1, 11, 0) + \
	IIF(((val)>>9) == 1, 10, 0) + \
	IIF(((val)>>8) == 1, 9, 0) + \
	IIF(((val)>>7) == 1, 8, 0) + \
	IIF(((val)>>6) == 1, 7, 0) + \
	IIF(((val)>>5) == 1, 6, 0) + \
	IIF(((val)>>4) == 1, 5, 0) + \
	IIF(((val)>>3) == 1, 4, 0) + \
	IIF(((val)>>2) == 1, 3, 0) + \
	IIF(((val)>>1) == 1, 2, 0) + \
	IIF(((val)>>0) == 1, 1, 0) + \
0)


//max value for a given number of bits:
//This is equivalent to 1<<NumBits(value), but is a little shorter. 
//#define BitMax(val)  \
//( \
//	IIF0((val) & ~0xffff, 99, 0) /*junk if value out of range*/ + \
//	IIF0(((val) & 0x8000) == (1<<15), (1<<16)-1, 0) + \
//	IIF0(((val) & 0xc000) == (1<<14), (1<<15)-1, 0) + \
//	IIF0(((val) & 0xe000) == (1<<13), (1<<14)-1, 0) + \
//	IIF0(((val) & 0xf000) == (1<<12), (1<<13)-1, 0) + \
//	IIF0(((val) & 0xf800) == (1<<11), (1<<12)-1, 0) + \
//	IIF0(((val) & 0xfc00) == (1<<10), (1<<11)-1, 0) + \
//	IIF0(((val) & 0xfe00) == (1<<9), (1<<10)-1, 0) + \
//	IIF0(((val) & 0xff00) == (1<<8), (1<<9)-1, 0) + \
//	IIF0(((val) & 0xff80) == (1<<7), (1<<8)-1, 0) + \
//	IIF0(((val) & 0xffc0) == (1<<6), (1<<7)-1, 0) + \
//	IIF0(((val) & 0xffe0) == (1<<5), (1<<6)-1, 0) + \
//	IIF0(((val) & 0xfff0) == (1<<4), (1<<5)-1, 0) + \
//	IIF0(((val) & 0xfff8) == (1<<3), (1<<4)-1, 0) + \
//	IIF0(((val) & 0xfffc) == (1<<2), (1<<3)-1, 0) + \
//	IIF0(((val) & 0xfffe) == (1<<1), (1<<2)-1, 0) + \
//	IIF0(((val) & 0xffff) == (1<<0), (1<<1)-1, 0) + \
//0)


//exponential value:
//This macro results in a constant expression at compile time.
#define Power(base, exp)  \
( \
	(IIF((exp) >= 1, (base)-1, 0) + 1) * \
	(IIF((exp) >= 2, (base)-1, 0) + 1) * \
	(IIF((exp) >= 3, (base)-1, 0) + 1) * \
	(IIF((exp) >= 4, (base)-1, 0) + 1) * \
	(IIF((exp) >= 5, (base)-1, 0) + 1) * \
	(IIF((exp) >= 6, (base)-1, 0) + 1) * \
	(IIF((exp) >= 7, (base)-1, 0) + 1) * \
	(IIF((exp) >= 8, (base)-1, 0) + 1) * \
1)


//paranoid checks:
//This section verifies that various macros above are working correctly.
#if (IIF(4, 12, 14) != 12) || (IIF(0, 13, 16) != 16) //;paranoid check
 #error "IIF macro is broken"
#endif

#if !non0(4) || !non0(0)
 #error "NON0 macro is broken"
#endif

#if NumBits8(63) != 6
 #error "NUMBITS macro is broken"
#endif

#if Power(3, 4) != 81
 #error "POWER macro is broken"
#endif


//;==================================================================================================
//;Misc opcode helper macros.
//NOTE: BoostC compiler doesn't allow mixing of asm and C within the same multi-line macro, so inline functions are used as wrappers.
//;==================================================================================================

//BoostC compiler requires leading "_" to reference C vars from asm:
//NOTE: DON'T put more than one ASM on a line; BoostC ignores the rest of the line (multi-line macros must use asm block)
//#ifdef _BOOSTC
// #define asmref(var)  _##var
// #define cref(type)  type&
// #define ASM(stmt)  asm stmt
// #define ASM2(stmt, param)  asm stmt, param //kludge to allow comma within stmt
//#endif
//#ifdef SDCC
// #define asmref(var)  var
// #define cref(type)  type
// #define ASM(stmt)
// #define ASM2(stmt, param)
// #error "unimplemented asm + ref macros"
//#endif
//#ifndef ASM
//#ifdef _GCC_
// #define asmref(var)  var
// #define cref(type)  type
// #define ASM(stmt)
// #define ASM2(stmt, param)
// #error "unimplemented asm + ref macros"
//#endif


#define uninitialized  volatile //BoostC: use "volatile" to disable warnings about uninitialized vars

//copy a bit:
//BoostC generates inefficient instr seq using a temp, so use explicitly copy it here
#define bitcopy(frombit, tobit)  \
{ \
	tobit = FALSE; \
	if (frombit) tobit = TRUE; \
}


//repeat a stmt in-line:
//cuts down on source code verbosity.
//intended only for short stmts and low repeat counts
#define REPEAT(count, stmt)  \
{ \
	if (count > 0) stmt; \
	if (count > 1) stmt; \
	if (count > 2) stmt; \
	if (count > 3) stmt; \
	if (count > 4) stmt; \
	if (count > 5) stmt; \
	if (count > 6) stmt; \
	if (count > 7) stmt; \
	if (count > 8) stmt; \
	if (count > 9) stmt; \
	if (count > 10) stmt; \
	if (count > 11) stmt; \
	if (count > 12) stmt; \
	if (count > 13) stmt; \
	if (count > 14) stmt; \
	if (count > 15) stmt; \
}


//define a dummy reg to allow a const to be passed by reference (for inline function params):
//CAUTION: if value is a valid RAM address, memory will be allocated
//to compensate, an offset is added to avoid allocating general purpose RAM; the compiler will drop the upper part of address anyway
//NOTE: assumes < 1K RAM is present on device
//#define Const2Ref(name, value)  uint8 FixedAdrs(name, (value) + 0x400) //creates var with value as address so it can be passed by reference

//pseudo-reg:
//#define WREG  0xDeadBeef //placeholder operand for a few macros


//no-op:
//Okay to use for *very* short busy-waits.
//Cooperative multi-tasking should be used for longer delays, so other threads can run while waiting.
#undef NOOP
#define NOOP(n)  \
{ \
	if (n & 1) NOOP_.0 = TRUE; \
	if (n & 2) NOOP_.1 = TRUE; \
	if (n & 4) NOOP_.2 = TRUE; \
	if (n & 8) NOOP_.3 = TRUE; \
}
//#ifdef _CC5X_
// #define NOOP1()  nop()
// #define NOOP2()  nop2()
//#else
//inline void NOOP1(void)
//{
//#ifdef _GCC_
//	WREG = 0;
//#else
//	ASM(nop);
//#endif //def _GCC_
//}
//inline void NOOP2(void)
//{
//broken (compiler is optimizing out):
//	goto here;
//here:
//	NOOP1();
//	NOOP1();
//	++pcl; //16F takes 2 instr cycles when modifying pcl, but only a single opcode is used
//	REPEAT(2, NOOP1());
//}
//#endif
//non_inline void NOOP4(void)
//{
//call+return uses 4 instr cycles, so just return after being called
//}
//non_inline void NOOP8(void)
//{
//	asm
//	{
//		call noop4;
//	noop4:
//		return
//	}
//BoostC doesn't like "call" within asm, so just use a nested call
//same #instr cycles, but takes more prog space
//	NOOP4();
//}


//generic opcode or prog data:
//no worky
//inline void OPCODE(uint8& ophi, uint8& oplo)
//{
//	asm data ((asmref(ophi)<<8) + asmref(oplo));
//}


//increment a 24-bit value:
#if 0
inline void inc(cref(uint3x8) val)
{
	++val.bytes[0]; //little endian
	if (status.C) ++val.bytes[1];
	if (status.C) ++val.bytes[2];
}

//clear a 24-bit value:
inline void clrf(cref(uint3x8) val)
{
	val.bytes[0] = 0; //little endian
	val.bytes[1] = 0;
	val.bytes[2] = 0;
}
#endif


//increment a value but don't wrap; leave at max value:
inline void inc_nowrap(uint8& reg)
{
//	if (reg != 0xFF) ++reg; /*try inc, then skip real inc if result would be 0; only takes 2 instr instead of 3*/
	asm incfsz _reg, W; //try inc, then skip real inc if result would be 0; only takes 2 instr instead of 3
	++reg;
}
//alternate version to preserve WREG:
#define inc_nowrap_WREG(reg)  \
{ \
	if (!++reg) --reg; /*3 instr: inc, btfss status.z, dec*/ \
}


//;==================================================================================================
//;Addressing helper macros.
//;==================================================================================================

//#define USE_RP1  FALSE  //;no need to use Bank 2 + 3 for this code; this allows only 1 instr to be used for bank selects (gives more compact code)

#define NONBANKED  0x70 //start of non-banked RAM
#define BANKLEN  0x80 //bank size/end of first bank
#define SFRLEN  0x20 //SFR size at start of each bank

#define GPRLEN  (NONBANKED - SFRLEN) //#banked GPR bytes in each bank
#define GPRGAP  (BANKLEN + SFRLEN - NONBANKED) //#bytes between GPR across banks, and #bytes GPR in each bank
#define TOTAL_BANKRAM  (TOTAL_RAM - (BANKLEN - NONBANKED)) //#bytes total GPR available


//BoostC compiler is not reducing address arithmetic at compile time, so use constants to force it:
//The reason is because the final addresses are not resolved until link time, after compiler has finished.
#define adrsof(thing)  thing##_ADDR //ADRSOF_##thing
//#define _ADDR  *1 //kludge: allow expr in bankof() //no worky with BoostC

//hard return:
//#define RETURN()  return 0

//#define RETLW_fixup(stmt)  { stmt; asm movwf _WREG } 


#define BankOfs(reg)  (adrsof(reg) & (BANKLEN - 1))
#define bankof(reg)  (/*adrsof*/(reg) & ~(BANKLEN - 1))

//MPASM doesn't like linear addresses, so 0x2000 must be stripped off of address
#define Linear2Banked(adrs)  (((((adrs) & 0x1FFF) / GPRLEN) * BANKLEN) + (((adrs) & 0x1FFF) % GPRLEN) + SFRLEN)
#define Banked2Linear(adrs)  (((adrs) / BANKLEN) * GPRLEN + ((adrs) % BANKLEN) - SFRLEN)
//#ifdef PIC16X
// #define MakeBanked(adrs)  Linear2Banked(adrs)
//#else
// #define MakeBanked(adrs)  adrs
//#endif


//kludgey way to check macro param types:
//used to avoid extra run-time instr
#define IsConst(thing)  IsConst_##thing
#define IsConst_0  TRUE
#define IsConst_1  TRUE
#define IsConst_WREG  FALSE


//add to fsr and then adjust for banks (if applicable):
//NOTE: assumes max 1 bank span (ie, amt is <= 0x30 on older PICs)
//amt is intended to be a const; use add_banked_WREG for variable at run-time
#define bank_gap_entpt  //label will be redefined later
#define add_banked(amt, high2low, extra/*, wrap_stmt, nowrap_stmt*/)  \
{ \
	if (!IsConst(amt) || amt) \
	{ \
		if (!(high2low)) /*palette data stored low -> high*/ \
		{ \
			fsrL += amt; \
			if (Carry) { fsrH_adjust(fsrH, +1); /*wrap_stmt*/; } \
		} \
		if (high2low) /*node data stored high -> low*/ \
		{ \
			fsrL -= amt; \
			if (Borrow) { fsrH_adjust(fsrH, -1); /*wrap_stmt*/; } \
		} \
	} \
bank_gap_entpt; \
	bank_gaps(high2low, extra/*, wrap_stmt, nowrap_stmt*/); \
}


#ifdef PIC16X //linear addressing on extended PICs: no bank gaps; always inline
 //no gaps to account for in linear addressing:
 #define bank_gaps(high2low, ignored/*, wrap_stmt, nowrap_stmt*/)  \
 { \
	fsrH |= 0x20; /*kludge: stay within linear address space to prevent SFR overwrite; fsr will wrap to wrong address, but at least it won't overwrite SFR*/ \
	/*nowrap_stmt*/; \
 }
 #define add_banked256(amt, high2low, extra/*, wrap_stmt, nowrap_stmt*/)  { WREG = amt; add_banked(WREG, high2low, extra/*, wrap_stmt, nowrap_stmt*/); }
#else //banked addressing on older PICs; account for bank gaps; pre-expand common variations to reduce code space
 //adjust for banks (if applicable) after changing fsr:
 #define bank_gaps(high2low, extra/*, wrap_stmt, nowrap_stmt*/)  \
 { \
	WREG = fsrL & ~BANKLEN; /*use separate expr to avoid BoostC temp*/ \
	/*if ((WREG >= NONBANKED) || (WREG <= SFRLEN))*/\
	WREG += 0 -(SFRLEN + extra) /*IIF(high2low, SFRLEN, SFRLEN + 1) force the valid range to wrap*/; \
	WREG += 0 -(GPRLEN - extra); /*now check for the invalid range*/ \
	if (NotBorrow) /*bank wrap*/ \
	{ \
		if (!(high2low)) /*0x20..0x6f okay, 0x70..0x7f,0..0x1f => bank wrap*/ \
		{ \
			if (extra) fsrL += WREG; \
			fsrL += (GPRGAP + extra); \
			if (Carry) { fsrH_adjust(fsrH, +1); /*wrap_stmt*/; } \
		} \
		if (high2low) /*0x21..0x70 okay, 0..0x20,0x71..0x7f => bank wrap*/ \
		{ \
			if (extra) fsrL -= WREG; \
			fsrL -= (GPRGAP + extra); \
			if (Borrow) { fsrH_adjust(fsrH, -1); /*wrap_stmt*/; } \
		} \
	} \
	/*else nowrap_stmt*/; \
 }

 #define BankGaps_TRUE_0  97
 #define BankGaps_FALSE_0  96
 #define BankGaps_TRUE_2  99
 #define BankGaps_FALSE_2  98
 //kludge: pre-expand commonly used variations to reduce code space and cut down on macro body size:
 #undef bank_gap_entpt
 #define bank_gap_entpt  ABSLABEL(BankGaps_TRUE_0)
 non_inline void add_banked_WREG_TRUE_0(void)
 {
	ONPAGE(PROTOCOL_PAGE); //reduce page selects
	add_banked(WREG, TRUE, 0/*, wrap_stmt, nowrap_stmt*/);
 }
 #undef bank_gap_entpt
 #define bank_gap_entpt  ABSLABEL(BankGaps_TRUE_2)
 non_inline void add_banked_WREG_TRUE_2(void) //;;;;used; only here to resolve ref
 {
	ONPAGE(PROTOCOL_PAGE); //reduce page selects
	add_banked(WREG, TRUE, 2/*, wrap_stmt, nowrap_stmt*/);
 }
 #undef bank_gap_entpt
 #define bank_gap_entpt  ABSLABEL(BankGaps_FALSE_0)
 non_inline void add_banked_0_FALSE_0(void)
 {
	ONPAGE(PROTOCOL_PAGE); //reduce page selects
	add_banked(0, FALSE, 0/*, wrap_stmt, nowrap_stmt*/);
 }
 #undef bank_gap_entpt
 #define bank_gap_entpt  ABSLABEL(BankGaps_FALSE_2)
 non_inline void add_banked_0_FALSE_2(void) //not used; only here to resolve ref
 {
	ONPAGE(PROTOCOL_PAGE); //reduce page selects
	add_banked(0, FALSE, 2/*, wrap_stmt, nowrap_stmt*/);
 }
//redirect further expansions of bank_gaps to above instances:
// inline void bank_gaps_TRUE_0(void)
// {
//	ABSCALL(BankGaps_TRUE_0); //alternate entry point into tail of add_banked_TRUE_0
// }
// inline void bank_gaps_TRUE_2(void) //not used; only here to resolve ref
// {
//	ABSCALL(BankGaps_TRUE_2); //alternate entry point into tail of add_banked_TRUE_2
// }
// inline void bank_gaps_FALSE_0(void)
// {
//	ABSCALL(BankGaps_FALSE_0); //alternate entry point into tail of add_banked_FALSE_0
// }
// inline void bank_gaps_FALSE_2(void) //not used; only here to resolve ref
// {
//	ABSCALL(BankGaps_FALSE_2);
// }
//redirect further expansions of bank_gaps into above instances:
 #undef bank_gaps
 #define bank_gaps(high2low, extra/*, wrap_stmt, nowrap_stmt*/)  \
 { \
	if (high2low) \
	{ \
		ABSCALL(BankGaps_TRUE_##extra##); \
		if (NEVER) add_banked_WREG_TRUE_##extra(); /*kludge: prevent dead code removal*/ \
	} \
	else \
	{ \
		ABSCALL(BankGaps_FALSE_##extra##); \
		if (NEVER) add_banked_0_FALSE_##extra(); /*kludge: prevent dead code removal*/ \
	} \
 }
//pre-expand additional variations:
 #undef bank_gap_entpt
 #define bank_gap_entpt
 non_inline void add_banked_0_TRUE_0(void)
 {
	ONPAGE(PROTOCOL_PAGE); //reduce page selects
	add_banked(0, TRUE, 0/*, wrap_stmt, nowrap_stmt*/);
 }
 non_inline void add_banked_0_TRUE_2(void)
 {
	ONPAGE(PROTOCOL_PAGE); //reduce page selects
	add_banked(0, TRUE, 2/*, wrap_stmt, nowrap_stmt*/);
 }
 non_inline void add_banked_1_TRUE_0(void) //not used; only here to resolve ref
 {
	ONPAGE(PROTOCOL_PAGE); //reduce page selects
	add_banked(1, TRUE, 0/*, wrap_stmt, nowrap_stmt*/);
 }
 non_inline void add_banked_1_TRUE_2(void) //not used; only here to resolve ref
 {
	ONPAGE(PROTOCOL_PAGE); //reduce page selects
	add_banked(1, TRUE, 0/*, wrap_stmt, nowrap_stmt*/);
 }
// non_inline void add_banked_WREG_TRUE_2(void) //not used; only here to resolve ref
// {
//	ONPAGE(PROTOCOL_PAGE); //reduce page selects
//	add_banked(WREG, TRUE, 2/*, wrap_stmt, nowrap_stmt*/);
// }
// non_inline void add_banked_0_FALSE_2(void) //not used; only here to resolve ref
// {
//	ONPAGE(PROTOCOL_PAGE); //reduce page selects
//	add_banked(0, FALSE, 2/*, wrap_stmt, nowrap_stmt*/);
// }
 non_inline void add_banked_1_FALSE_0(void) //not used; only here to resolve ref
 {
	ONPAGE(PROTOCOL_PAGE); //reduce page selects
	add_banked(1, FALSE, 0/*, wrap_stmt, nowrap_stmt*/);
 }
 non_inline void add_banked_WREG_FALSE_0(void) //not used; only here to resolve ref
 {
	ONPAGE(PROTOCOL_PAGE); //reduce page selects
	add_banked(WREG, FALSE, 0/*, wrap_stmt, nowrap_stmt*/);
 }
 non_inline void add_banked_WREG_FALSE_2(void) //not used; only here to resolve ref
 {
	ONPAGE(PROTOCOL_PAGE); //reduce page selects
	add_banked(WREG, FALSE, 2/*, wrap_stmt, nowrap_stmt*/);
 }
//redirect further expansions of add_banked to above instances:
 #undef add_banked
 #define add_banked(amt, high2low, extra/*, wrap_stmt, nowrap_stmt*/)  \
 { \
	if (!IsConst(amt) || (amt)) \
	{ \
		WREG = amt; \
		if (high2low) add_banked_WREG_TRUE_##extra##(); \
		else add_banked_WREG_FALSE_##extra##(); \
	} \
	else if (high2low) add_banked_0_TRUE_##extra##(); \
	else add_banked_0_FALSE_##extra##(); \
 }

 //adjust fsr bank by bank for larger amts, account for gaps between banks each time; can be variable and >= 0x30
 #define add_banked256(amt, high2low, extra/*, wrap_stmt, nowrap_stmt*/)  \
 { \
	WREG = amt; \
	WREG += 0 -2 * GPRLEN; \
	if (NotBorrow) /*advance 2 banks*/ \
	{ \
		if (!(high2low)) { fsrH_adjust(fsrH, +1); /*wrap_stmt*/; } \
		if (high2low) { fsrH_adjust(fsrH, -1); /*wrap_stmt*/; } \
	} \
	/*else*/ if (Borrow) WREG += 2 * GPRLEN; /*undo prev*/ \
	WREG += 0 -GPRLEN; \
	if (NotBorrow) /*advance 1 bank, msb adrs change*/ \
	{ \
		if (fsrL & 0x80) { fsrL &= ~0x80; if (!(high2low)) fsrH_adjust(fsrH, +1); } \
		else { fsrL |= 0x80; if (high2low) fsrH_adjust(fsrH, -1); } \
	} \
	else WREG += GPRLEN; /*undo prev*/ \
	WREG += 0 - GPRGAP; \
	if (NotBorrow) /*advance partial and check for bank gap*/ \
	{ \
		add_banked(WREG, high2low, extra/*, wrap_stmt, nowrap_stmt*/); \
		WREG = GPRGAP; \
	} \
	else WREG += GPRGAP; /*undo prev*/ \
	add_banked(WREG, high2low, extra/*, wrap_stmt, nowrap_stmt*/); \
 }
 //kludge: pre-expand commonly used variations to reduce code space and macro body size:
 non_inline void add_banked256_WREG_TRUE_0(void)
 {
	ONPAGE(PROTOCOL_PAGE); //reduce page selects
	add_banked256(WREG, TRUE, 0/*, wrap_stmt, nowrap_stmt*/);
 }
 non_inline void add_banked256_WREG_FALSE_0(void) //not used; only here to resolve ref
 {
	ONPAGE(PROTOCOL_PAGE); //reduce page selects
	add_banked256(WREG, FALSE, 0/*, wrap_stmt, nowrap_stmt*/);
 }
 #undef add_banked256
 #define add_banked256(amt, high2low, extra/*, wrap_stmt, nowrap_stmt*/)  \
 { \
	if (high2low) { WREG = amt; add_banked256_WREG_TRUE_##extra##(); } \
	else { WREG = amt; add_banked256_WREG_FALSE_##extra##(); } \
 }
#endif


#if 0 //too much baggage
#define add_banked(amt, high2low)  add_banked_##amt(high2low, FALSE)
//expand a few special cases by value to avoid a potential run-time check on variable parameter:
#define add_banked_1(high2low, callable)  { WREG = 1; add_banked_WREG(high2low, callable); } //TODO: replace with ++ and --?
#define add_banked_0(high2low, ignored)  bank_gaps(high2low, 0) /*just check for bank gaps*/
//NOTE: WREG is assumed < 0x50 for non-linear addressing on older PICs
#define add_banked_WREG(high2low, callable)  \
{ \
	if (!(high2low)) /*palette data stored low -> high*/ \
	{ \
		fsrL += WREG; \
		if (Carry) { fsrH_adjust(fsrH, +1); /*onwrap*/; } \
	} \
	if (high2low) /*node data stored high -> low*/ \
	{ \
		fsrL -= WREG; \
		if (Borrow) { fsrH_adjust(fsrH, -1); /*onwrap*/; } \
	} \
	if (callable) ABSLABEL(IIF(high2low, BankGapsHigh2Low_0, BankGapsLow2High_0)); /*allow call into here; avoids code duplication*/ \
	bank_gaps(high2low, 0); \
}

#define AddBankedHigh2Low  99 //101
#define AddBankedLow2High  98 //100
#define BankGapsHigh2Low_0  97 //103
#define BankGapsLow2High_0  96 //102

#define bank_gaps(high2low, extra)  bank_gaps_else(high2low, extra, EMPTY)
#ifdef PIC16X //extended instr set supports linear addressing: no bank gaps; always inline
 #define add_banked_WREG256(high2low, callable)  add_banked_WREG(high2low, callable)
 #define bank_gaps_else(high2low, ignored, nowrap_stmt)  /*no gaps to account for in linear addressing*/ \
 { \
	fsrH |= 0x20; /*kludge: stay within linear address space to prevent SFR overwrite; fsr will wrap to wrong address, but at least it won't overwrite SFR*/ \
	nowrap_stmt(); \
 }
#else //bank by bank, account for gaps between banks; var can be >= 0x50
 #define add_banked_WREG256(high2low, callable)  \
 { \
	WREG += 0 -2 * GPRLEN; \
	if (NotBorrow) \
	{ \
		if (!(high2low)) fsrH_adjust(fsrH, +1); \
		if (high2low) fsrH_adjust(fsrH, -1); \
	} \
	else \
	{ \
		WREG += 2 * GPRLEN; \
		/*if (callable) if (EqualsZ) ABSGOTO(IIF(high2low, BankGapsHigh2Low, BankGapsLow2High))*/; /*bypass extra arithmetic*/ \
		WREG += 0 -GPRLEN; \
		if (NotBorrow) \
		{ \
			if (callable) ABSCALL(IIF(high2low, AddBankedHigh2Low, AddBankedLow2High)); /*call into reusable code below; avoids code duplication*/ \
			else add_banked_WREG(high2low, FALSE); \
			WREG = BANKLEN - GPRLEN; \
		} \
		WREG += GPRLEN; \
	} \
	if (callable) ABSLABEL(IIF(high2low, AddBankedHigh2Low, AddBankedLow2High)); /*allow call into here; avoids code duplication*/ \
	add_banked_WREG(high2low, callable); \
 }
//	WREG = fsrL - FSRLEN; 
//	WREG &= ~BANKLEN; /*separate expr to avoid BoostC temp*/ 
//skip over gaps in GPR (not needed for linear addressing):
//low-to-high:
//[0..0x1f]: += 0x30
//[0x20..0x6f]: no change
//[0x70..0x9f]: += 0x30
//[0xa0..0xef]: no change
//[0xf0..0xff]: += 0x30
//high-to-low:
//[0..0x20]: -= 0x30
//[0x21..0x70]: no change
//[0x71..0xa0]: -= 0x30
//[0xa1..0xf0]: no change
//[0xf1..0xff]: -= 0x30
 #if 1
//	           -0x22        -0x4e
//0..0x21 => 0xde..ff B => 0x90..b1 !B => 0xf0..0xf0 (70..70)
//0x22..0x6f => 0..4d !B => 0xb2..ff B
//0x70..0x7f => 4e..5d !B => 0..0f !B => 

//             -0x20       -0x50          -= 0x30
//0..0x1f => 0xe0..ff B => 0x90..af !B => 0xd0..0xef (50..6f)
//0x20..0x6f => 0..4f !B => 0xb0..ff B =>
//0x70..0x7f => 50..5f !B => 0..0f !B => 0x40..0x4f
 //7-10 instr (7 = most common case) using compares and arithmetic:
 #define /*low2high broken*/ bank_gaps_else(high2low, extra, nowrap_stmt)  \
 { \
	WREG = fsrL & ~BANKLEN; /*use separate expr to avoid BoostC temp*/ \
	/*if ((WREG >= NONBANKED) || (WREG <= SFRLEN))*/\
	WREG += 0 -(SFRLEN + extra) /*IIF(high2low, SFRLEN, SFRLEN + 1) force the valid range to wrap*/; \
	WREG += 0 -(GPRLEN - extra); /*now check for the invalid range*/ \
	if (NotBorrow) /*bank wrap*/ \
	{ \
		if (!(high2low)) /*0x20..0x6f okay, 0x70..0x7f,0..0x1f => bank wrap*/ \
		{ \
			if (extra) fsrL += WREG; \
			fsrL += (GPRGAP + extra); \
			if (Carry) fsrH_adjust(fsrH, +1); \
		} \
		if (high2low) /*0x21..0x70 okay, 0..0x20,0x71..0x7f => bank wrap*/ \
		{ \
			if (extra) fsrL -= WREG; \
			fsrL -= (GPRGAP + extra); \
			if (Borrow) fsrH_adjust(fsrH, -1); \
		} \
	} \
	else nowrap_stmt(); \
 }
 #else //7-9-13 instr (7-9 = most common) using gotoc:
 #define bank_gaps(high2low)  \
 { \
	swap_WREG(fsrL); \
	WREG &= 0x0F; \
	INGOTOC(TRUE); \
	pcl += WREG; \
	PROGPAD(0); /*jump table base address*/ \
	JMPTBL16(0) goto skip_gap; \
	JMPTBL16(1) goto skip_gap; \
	JMPTBL16(2) goto no_change; \
	JMPTBL16(3) goto no_change; \
	JMPTBL16(4) goto no_change; \
	JMPTBL16(5) goto no_change; \
	JMPTBL16(6) goto no_change; \
	JMPTBL16(7) goto skip_gap; \
	JMPTBL16(8) goto skip_gap; \
	JMPTBL16(9) goto skip_gap; \
	JMPTBL16(0xa) goto no_change; \
	JMPTBL16(0xb) goto no_change; \
	JMPTBL16(0xc) goto no_change; \
	JMPTBL16(0xf) goto no_change; \
	JMPTBL16(0xe) goto no_change; \
	/*JMPTBL16(0xf) goto skip_gap*/; \
	INGOTOC(FALSE); /*check jump table doesn't span pages*/ \
skip_gap: \
	if (!(high2low)) /*0x20..0x6f okay, 0x70..0x7f,0..0x1f => bank wrap*/ \
	{ \
		fsrL += GPRGAP; \
		if (Carry) fsrH_adjust(fsrH, +1); \
	} \
	if (high2low) /*0x21..0x70 okay, 0..0x20,0x71..0x7f => bank wrap*/ \
	{ \
		fsrL -= GPRGAP; \
		if (Borrow) fsrH_adjust(fsrH, -1); \
	} \
no_change: \
 }
 #endif
//pre-expand 1 copy of each macro into callable functions to conserve code space:
//NOTE: each function contains 3 callable entry points
 non_inline void add_banked_WREG256_TRUE(void)
 {
	ONPAGE(PROTOCOL_PAGE); //reduce page selects
	add_banked_WREG256(TRUE, TRUE); //high->low, callable
 }
 non_inline void add_banked_WREG256_FALSE(void)
 {
	ONPAGE(PROTOCOL_PAGE); //reduce page selects
	add_banked_WREG256(FALSE, TRUE); //low->high, callable
 }
//then map other calls to reuse the above 2 copies:
 #undef add_banked_WREG256
 #define add_banked_WREG256(high2low, ignored)  \
 { \
	if (high2low) add_banked_WREG256_TRUE(); \
	if (!(high2low)) add_banked_WREG256_FALSE(); \
 }
 #undef add_banked_WREG
 #define add_banked_WREG(high2low, ignored)  \
 { \
	/*ABSCALL(IIF(high2low, AddBankedHigh2Low, AddBankedLow2High)) causes temps if high2low is not a const*/; /*call into functions above; avoids code duplication*/ \
	if (high2low) ABSCALL(AddBankedHigh2Low); /*call into functions above; avoids code duplication*/ \
	if (!(high2low)) ABSCALL(AddBankedLow2High); /*call into functions above; avoids code duplication*/ \
	/*if (NEVER && (high2low)) add_banked_WREG256_TRUE()*/; /*kludge: prevent dead code removal*/ \
	/*if (NEVER && !(high2low)) add_banked_WREG256_FALSE()*/; /*kludge: prevent dead code removal*/ \
 }
 //kludge: pre-expand variations that will be needed
 inline void bank_gaps_H2L_2(void)
 {
	 bank_gaps(TRUE, 2);
 }
 inline void bank_gaps_L2H_2(void)
 {
	 bank_gaps(FALSE, 2);
 }
 inline void bank_gaps_H2L_0(void)
 {
	 bank_gaps(TRUE, 0);
 }
 inline void bank_gaps_L2H_0(void)
 {
	 bank_gaps(FALSE, 0);
 }
 #undef bank_gaps
 #define bank_gaps(high2low, extra)  \
 { \
	if (extra == 0) \
	{ \
		/*ABSCALL(IIF(high2low, BankGapsHigh2Low, BankGapsLow2High)) causes temps if high2low is not a const*/; /*call into functions above; avoids code duplication*/ \
		if (high2low) ABSCALL(BankGapsHigh2Low_0); /*call into functions above; avoids code duplication*/ \
		if (!(high2low)) ABSCALL(BankGapsLow2High_0); /*call into functions above; avoids code duplication*/ \
	/*if (NEVER && (high2low)) add_banked_WREG256_TRUE()*/; /*kludge: prevent dead code removal*/ \
	/*if (NEVER && !(high2low)) add_banked_WREG256_FALSE()*/; /*kludge: prevent dead code removal*/ \
	} \
	else \
	{ \
		if (high2low) bank_gaps_H2L_##extra(); \
		if (!high2low) bank_gaps_L2H_##extra(); \
	} \
 }

//increment fsr past non-banked RAM and SFR:
//fsrL        WREG     fsrL += WREG
//0x20..0x70  0..0x50  if (fsrL >= 0x70) { fsrL += 0x30; if (Carry) ++fsrH; if (fsrL >= 0xF0) { fsrL += 0x30; if (Carry) ++fsrH; }}
//if (WREG >= 0xA0) { ++fsrH; WREG -= 0xA0; } //2 banks
//if (WREG >= 0x50) { fsrL += 0x80; if (Carry) ++fsrH; WREG -= 0x50; } //1 bank

//0xa0..0xf0
//if += 0..0x4F &~0x80 0x50 => 0x80
#endif
#endif


//syntax varies so try to hide it within a macro:
//#ifdef _CC5X_
// #define FixedAdrs(name, adrs)  name @ adrs
//#endif
//#ifdef _BOOSTC
// #define FixedAdrs(name, adrs)  name @ adrs
//#endif
//#ifdef SDCC
// #define FixedAdrs(name, adrs)  __at (adrs) name /* @ adrs */
//#endif
//#ifndef FixedAdrs
// #warning "TODO: add syntax for fixed-address variables/registers"
// #define FixedAdrs(name, adrs)  name //dummy def just to pass syntax check
//#endif


//;==================================================================================================
//;ROM addressing helpers.
//;==================================================================================================

//verify a jump table entry is in the correct place:
#define JMPTBL16(entry)  PROGPAD((entry) & 0xF); if (ALWAYS)
#define JMPTBL64(entry)  PROGPAD((entry) & 0x3F); if (ALWAYS)

//pack 2 ASCII chars into one prog word:
#define A2(char1, char2)  (((char1) << 7) | ((char2) & 0x7F))

#define FLASH_READ  TRUE //, FALSE
#define EEPROM_READ  FALSE //, FALSE
#define EEPROM_WRITE  FALSE //, TRUE
#define WANT_READ  FALSE
#define WANT_WRITE  TRUE
#define MY_EECON1(which, wrenable)  \
(0  /*;default to all bits off, then turn on as needed*/ \
	| IIF(FALSE, 1<<CFGS, 0)  /*0 => EEPROM or flash; 1 => User ID or Device ID*/ \
	| IIF(which, 1<<EEPGD, 0)  /*1 => flash, 0 => EEPROM*/ \
	| IIF(!wrenable, 1<<RD, 0)  /*1 => initiate read cycle*/ \
	| IIF(wrenable, 1<<WREN, 0)  /*1 => enable writes*/ \
	NOPE(| IIF(wrenable, 1<<WR, 0))  /*1 => initiate write cycle*/ \
)


//;read program memory (gets 14 bits only):
//NOTE: eeadrH/eeadrL must already be set by caller
#define INC_ADRS  TRUE
#define NOINC_ADRS  FALSE
#define PROGREAD(incadrs)  { PROGREAD_(); if (incadrs) inc16_eeadr(); }
inline void PROGREAD_(void)
{
#if 0
	eecon1.EEPGD = TRUE; //select program (as opposed to EE) memory
	eecon1.RD = TRUE; //;start EE/flash read
#else
	eecon1 = MY_EECON1(FLASH_READ, WANT_READ);
// #warning "CHECK THIS" //seems to work, according to MPSIM
#endif
//	nop(); nop(); //DO NOT MOVE THIS!  required only for flash read
	asm data 0, 0; //kludge: don't use "nop" (optimizer will combine into goto $+1)
//	++eeadrL; if (EqualsZ) ++eeadrH;
}


//read EEPROM memory:
//NOTE: eeadrH/eeadrL must already be set by caller
#define EEREAD(incadrs)  { EEREAD_(); if (incadrs) ++eeadrL; }
inline void EEREAD_(void)
{
#if 0
	eecon1.EEPGD = FALSE; //select EEPROM (as opposed to flash) memory
	eecon1.RD = TRUE; //;start EE/flash read
#else
	eecon1 = MY_EECON1(EEPROM_READ, WANT_READ);
// #warning "CHECK THIS" //seems to work, according to MPSIM
#endif
//	NOPE(REPEAT(2, nop())); //DO NOT MOVE THIS!  required only for flash read
//is this needed?	while (eecon1.RD);
//	++eeadrL; if (EqualsZ) ++eeadrH;
}
//read EEPROM again:
#if MY_EECON1(EEPROM_READ, WANT_READ) == 1 //only 1 bit needs to be set
 #define EEREAD_AGAIN(incadrs)  { eecon1 |= MY_EECON1(EEPROM_READ, WANT_READ); if (incadrs) ++eeadrL; }
#else
 #define EEREAD_AGAIN(incadrs)  EEREAD(incadrs)
#endif


//write EEPROM memory:
//NOTE: eeadrH/eeadrL and eedatL must already be set by caller
//NOTE: this does not refresh overall EEPROM when write cycles have been exceeded
#define EEWRITE(incadrs)  { EEWRITE_(); if (incadrs) ++eeadrL; }
inline void EEWRITE_(void)
{
#if 0
	eecon1.EEPGD = FALSE; //select EEPROM (as opposed to flash) memory
	eecon1.WR = TRUE; //;start EE/flash read
	WREN = TRUE;
	while (!pir2.EEIF); //wait for write to complete
	pir2.EEIF = FALSE;
#else
	eecon1 = MY_EECON1(EEPROM_WRITE, WANT_WRITE);
 #warning "CHECK THIS"
//magic Microchip sequence to prevent extraneous writes:
 	eecon2 = 0x55;
 	eecon2 = 0xAA;
 	eecon1.WR = TRUE;
	while (eecon1.WR); //wait for write to complete
#endif
	eecon1.WREN = FALSE; //paranoid
#if 0 //paranoid: read and compare for successful write
	WREG = eedatL;
	eecon1.RD = TRUE;
	if (eedatl != WREG) error...
#endif
//	++eeadrL; if (EqualsZ) ++eeadrH;
}


inline void inc16_eeadr(void)
{
//	++eeadr16;
	++eeadrL; if (EqualsZ) ++eeadrH; //disjoint on PIC16F688
}

#if 0
BCF EECON1, CFGS ;Deselect Configuration space
BCF EECON1, EEPGD ;Point to DATA memory
BSF EECON1, WREN ;Enable writes
BCF INTCON, ;Disable INTs.
MOVLW 55h ;
MOVWF EECON2 ;Write 55h
MOVLW 0AAh ;
MOVWF EECON2 ;Write AAh
BSF EECON1, ;Set WR bit to begin write
BSF INTCON, ;Enable Interrupts
BCF EECON1, ;Disable writes
BTFSC EECON1, ;Wait for write to complete
GOTO $-2 ;Done
-----
BANKSEL EEDATL
MOVF
EEDATL, W
BSF
XORWF
BTFSS
GOTO
:
;
;EEDATL not changed
;from previous write
EECON1, RD ;YES, Read the
;value written
EEDATL, W ;
STATUS, Z ;Is data the same
WRITE_ERR ;No, handle error
;Yes, continue
#endif


//;==================================================================================================
//;Helpers to allow more generic port and I/O pin handling.
//;==================================================================================================

//;port/pin definitions:
//;These macros allow I/O pins to be refered to using 2 hex digits.
//;First digit = port#, second digit = pin#.  For example, 0xB3 = Port B, pin 3 (ie, RB3).
//;For example, 0xA0 = Port A pin 0, 0xC7 = Port C pin 7, etc.
#define PORTOF(portpin)  nibbleof(portpin, 1)
#define PINOF(portpin)  nibbleof(portpin, 0)


#ifdef PORTA_ADDR
 #define IFPORTA(stmt, nostmt)  stmt
#else
 #define IFPORTA(stmt, nostmt)  nostmt
#endif

#if defined(PORTB_ADDR) || defined(PORTC_ADDR)
 #define IFPORTBC(stmt, nostmt)  stmt
#else
 #define IFPORTBC(stmt, nostmt)  nostmt
#endif

//eof
