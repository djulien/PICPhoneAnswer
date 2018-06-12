////////////////////////////////////////////////////////////////////////////////
////
/// Compile time helpers, macros, misc defs (compiler-independent)
//

#ifndef _HELPERS_H
#define _HELPERS_H

#define FALSE  0
#define TRUE  1 //(-1) ;"-1" is safer, but "1" avoids loss-of-precision warnings when assigning to a bit var
#define MAYBE  2  //;not FALSE and not TRUE; used for tri-state logic
#define DONT_CARE  FALSE  //;arbitrary, but it's generally safer to turn off a don't-care feature than to leave it on


//stringize for text strings and #pragma messages:
//https://stackoverflow.com/questions/1562074/how-do-i-show-the-value-of-a-define-at-compile-time
#define TOSTR(x) TOSTR_INNER(x)
#define TOSTR_INNER(x) #x

#define CONCAT(lhs, rhs) CONCAT_INNER(lhs, rhs)
#define CONCAT_INNER(lhs, rhs) lhs##rhs

//ANSI color codes (for console output):
//https://en.wikipedia.org/wiki/ANSI_escape_code
#define ANSI_COLOR(code)  "\x1b[" code "m"
#define RED_MSG  ANSI_COLOR("1;31") //too dark: "0;31"
#define GREEN_MSG  ANSI_COLOR("1;32")
#define YELLOW_MSG  ANSI_COLOR("1;33")
#define BLUE_MSG  ANSI_COLOR("1;34")
#define PINK_MSG  ANSI_COLOR("1;35")
#define CYAN_MSG  ANSI_COLOR("1;36")
#define GRAY_MSG  ANSI_COLOR("0;37")
#define ENDCOLOR  ANSI_COLOR("0")

//#define RED_MSG(msg)  RED_LT msg ENDCOLOR
//#define GREEN_MSG(msg)  GREEN_LT msg ENDCOLOR
//#define YELLOW_MSG(msg)  YELLOW_LT msg ENDCOLOR
//#define BLUE_MSG(msg)  BLUE_LT msg ENDCOLOR
//#define PINK_MSG(msg)  PINK_LT msg ENDCOLOR
//#define CYAN_MSG(msg)  CYAN_LT msg ENDCOLOR
//#define GRAY_MSG(msg)  GRAY_LT msg ENDCOLOR


////////////////////////////////////////////////////////////////////////////////
////
/// Compile-time conditionals (shouldn't generate any run-time logic if used with constants)
//

//in-line IF:
//BoostC compiler (and maybe others) doesn't handle "?:" tenary operator at compile time, even with constants (it should).
//This macro gives equivalent functionality, but results in a constant expression that can be reduced at compile time.
//This generates *a lot* more efficent code, especially for * / % operators.
//CAUTION: when used with #defines these macros may generate long statements that could overflow preprocessors's macro body limit.
//#define IIF(tfval, tval, fval)  //((((tfval)!=0)*(tval)) + (((tfval)==0)*(fval))) //this one repeats tfval so there could be side effects ot macro body could get too long
#define IIF(tfval, tval, fval)  (((tfval)!=0) * ((tval)-(fval)) + (fval)) //this one assumes fval is short and tries to minimize macro body size and side effects by expaning tfval only once
//#define IIF0(tfval, tval, dummy)  (((tfval)!=0)*(tval))  //;shorter version if fval == 0; helps avoid macro body length errors
#define IIFNZ(expr, tval)  (((expr)!=0)*(tval))  //;shorter version if fval == 0; helps avoid macro body length errors
#if ((3 == 3) != 1) || ((3 == 4) != 0)  //paranoid check; IIF relies on this behavior
 #error RED_MSG "IIF broken: TRUE != 1 or FALSE != 0"
#endif


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

#define NOP(n)  REPEAT(n, nop())


////////////////////////////////////////////////////////////////////////////////
////
/// Arithemtic functions, data helpers, etc
//

//;misc arithmetic helpers:
#define non0(val)  IIF(val, val, 1) //(IIFNZ(val, (val)-1) + 1) //avoid divide-by-0 errors
#define rdiv(num, den)  (((num) + (den) / 2) / non0(den))  //;rounded divide
#define divup(num, den)  (((num) + (den) - 1) / non0(den))  //;round-up divide
#define SIZE(thing)  (sizeof(thing) / sizeof((thing)[0]))  //#entries in an array

//kludge: compensate for SDCC preprocessor sign extend
#define U16FIXUP(val)  ((val) & 0xFFFF)

//these are upper case to avoid expansion within text:
//IIF is used so constants can be reduced at compile time.
#define SGN(x)  IIF((x) < 0, -1, (x) != 0)  //;-1/0/+1
#define ABS(x)  IIF((x) < 0, 0-(x), x)  //;absolute value
#define MIN(x, y)  IIF((x) < (y), x, y)
#define MAX(x, y)  IIF((x) > (y), x, y)
//#define MIN(x, y)  ((x) + IIF0((y) < (x), (y) - (x), 0)) //;uses IIF0 to try to avoid macro body length problems
//#define MAX(x, y)  ((x) + IIF0((y) > (x), (y) - (x), 0)) //;uses IIF0 to try to avoid macro body length problems


//convert bit mask to bit#:
//use token-pasting to reduce macro body size after expansion (needed for MPASM usage)
//CAUTION: sensitive to value format; must match exactly
#define bit2inx(val)  bit2inx_inner(val) //kludge: need nested macro to force eval
#define bit2inx_inner(val)  BITNUM_##val
#define BITNUM_0x80  7
#define BITNUM_0x40  6
#define BITNUM_0x20  5
#define BITNUM_0x10  4
#define BITNUM_0x08  3
#define BITNUM_0x04  2
#define BITNUM_0x02  1
#define BITNUM_0x01  0


//;powers of 2 (up to 16):
//Equivalent to Log2(value) + 1 when only 1 bit of value is set
//BoostC compiler doesn't handle "?:" operator at compile time, even with constants (it should).
//This macro allows a constant expression to be generated at compile time.
//#ifdef __CC5X__
// #warning YELLOW_MSG "CC5XBUG: multi-line macros not handled in #if"
//#endif
//	IIF((val)>>8, 99, 0) /*return bad value if out of range*/ + 
#define NumBits8(val)  \
( \
	((val) >= 0x80) + \
	((val) >= 0x40) + \
	((val) >= 0x20) + \
	((val) >= 0x10) + \
	((val) >= 0x08) + \
	((val) >= 0x04) + \
	((val) >= 0x02) + \
	((val) >= 0x01) + \
0)

//	IIF((val)>>16, 99, 0) /*return bad value if out of range*/ + 
#define NumBits16(val)  \
( \
	((val) >= 0x8000) + \
	((val) >= 0x4000) + \
	((val) >= 0x2000) + \
	((val) >= 0x1000) + \
	((val) >= 0x0800) + \
	((val) >= 0x0400) + \
	((val) >= 0x0200) + \
	((val) >= 0x0100) + \
NumBits8(val))


#endif //ndef _HELPERS_H
//EOF