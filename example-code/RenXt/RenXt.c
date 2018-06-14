//stand-alone node player (mainly for testing or burn-in)
//10/8/13 DJ fixed WS2811 palette shift bug
//3/5/14  DJ  streamlined WS2811 I/O to use raw (unshifted) palette bytes and fixed > 512 RAM bug; added GECE I/O; added GetBitmap start offset (mainly for windowed GECE I/O)
//3/25/14 DJ  fixed add_banked addressing errors for >= 0x30 <= 0x50
//10/27/14 DJ clean up tris init; tighten up chplex/pwm (streamline pwm, replace common cathode/anode with active high/low); prevent NodeByte reset during first GetBitmap
//11/5/14 DJ added support for 16F1827; resolved conflict between EscPending and temp 0x10 state bits (GECE most susceptible to problems)
//11/18/14 DJ limit white in demo to #CCCCCC to reduce power + brightness; this avoids excessive specs on power supplies where full white will not be used

//#define NODE_TYPE  WS2811 //default type for demo/test
#define DIMMING_CURVE  256 //try to make LED brightness appear more linear, 256 steps; NOTE: demo/test mode only

//#define SINGLE_PORT  //A //set to A or B/C to use a single port (for series strings only, reduced I/O overhead), or leave blank to use both ports A + B/C (parallel strings)
#define SERIES_PIN  0xA2 //convenient because it tends to be at the end of many PIC12F or PIC16F chips and not used for other purposes (RX/TX, ICSP, etc); all PICs seem to have pin A2
#define MAX_BAUD  (250 KBAUD) //(500 KBAUD) //start with this baud rate and auto-detect slower speeds; with 50 msec frame rate: 500k gives max 2500 bytes, 250k gives max 1250 bytes, 115.2k baud ~= max 576 bytes, 57.6k baud ~= 288 bytes
//#warning "TODO: bump this to 500k (might not be possible due to 30 usec node times)"
#define HIGHBAUD_OK  TRUE //PIC16F688 rev A.3 or older should be FALSE; newer can be TRUE
#define AUTO_DETECT_BAUD  TRUE //auto-detect 250k vs. 115k vs. 57k baud
#define CLOCK_FREQ  18432000 //(20 MHz) //ext clock freq; comment this out to run at max int osc freq; PIC will use int osc if ext clock is absent
//TODO: compile for 12F1840 @32MHz, 16F182x @32MHz, 16F688 @18.432MHz (4.6 vs 5 MIPS only affects SPBRG), others as requested
#define ASSUME_NOZCCHANGE  TRUE //assume ZC is stable and will not vary after power-up (avoids re-sampling overhead)

//group related code according to purpose in order to reduce page selects:
#define DEMO_PAGE  1 //free-running/demo/test code; non-critical
#define IOH_PAGE  0 //I/O handlers: WS2811, GECE, chplex, etc; timing-critical
#define PROTOCOL_PAGE  1 //protocol handler and opcodes; timing-sensitive
#define LEAST_PAGE  0 //put less critical code in whichever page has the most space (initialization or rarely-used functions)

#define WANT_TEST //include demo/test pattern
//#warning "NO TEST"
#define WANT_WS2811
#define WANT_GECE
#define WANT_CHPLEX


#define DEBUG2  FALSE //avoid extraneous defs
#define DEBUG  TRUE //only used for IDE debug; interferes with timing
#define WANT_STATS2  FALSE //avoid extraneous defs
#include "RenXt_compiler.h"
#include "RenXt_helpers.h"

//#define WANT_LOGIC  FALSE //constants only
#include "..\api\RenXt.h"

#if defined(PIC16X) && defined(CLOCK_FREQ) //&& (CLOCK_FREQ < 32 MHz)
 #warning "[WARNING] Ignoring ext clock (PIC can run faster without it)"
 #undef CLOCK_FREQ //run at max freq (32 MHz with PLL)
#endif


//;==================================================================================================
//;Memory allocation
//;==================================================================================================

#if !defined(PIC16X) && (TOTAL_RAM > 256)
 #undef TOTAL_RAM
 #define TOTAL_RAM  256 //kludge: use 8-bit ptrs for older/slower PICs to reduce instr overhead for indirect addressing
 #warning "[INFO] Ignoring RAM > 256 bytes on older PIC"
#endif

#define RAM_SCALE  divup(TOTAL_RAM, 256) //RAM scalar for 8-bit counters; 1..256 == 1, 257..512 == 2, 769..1024 == 4
//#define RAM_SCALE  4 //make it consistent for all memory sizes (avoids config file changes when swapping/rearranging controllers)
#define RAM_SCALE_BITS  MIN(RAM_SCALE, 3) //1, 2, 4 => 1, 2, 3
//#if RAM_SCALE > 1
// #define IFRAM_SCALE(stmt)  stmt
//#else
// #define IFRAM_SCALE(stmt)
//#endif

//kludge: BoostC is not handling DEVICE expansion correctly, so replace with a simpler expression:
#if TOTAL_RAM == 64
 #undef TOTAL_RAM
 #define TOTAL_RAM  64
 #define TOTAL_BANKRAM_tostr  48 //64 - 16
#else
#if TOTAL_RAM == 256
 #undef TOTAL_RAM
 #define TOTAL_RAM  256
 #define TOTAL_BANKRAM_tostr  240 //256 - 16
#else
#if TOTAL_RAM == 384
 #undef TOTAL_RAM
 #define TOTAL_RAM  384
 #define TOTAL_BANKRAM_tostr  368 //384 - 16
#else
#if TOTAL_RAM == 1024
 #undef TOTAL_RAM
 #define TOTAL_RAM  1024
 #define TOTAL_BANKRAM_tostr  1008 //1024 - 16
#else
 #error "[ERROR] Add more cases here"
#endif
#endif
#endif
#endif

//kludge: #warning doesn't reduce macro values, so force it here (mainly for debug/readability):
#if RAM_SCALE == 1
 #define RAM_SCALE_tostr  (1-to-1)
#else
#if RAM_SCALE == 2
 #define RAM_SCALE_tostr  (byte pairs)
#else
#if RAM_SCALE == 4
 #define RAM_SCALE_tostr  (quad bytes)
#else
// #define RAM_SCALE_tostr  RAM_SCALE
 #error "[ERROR] Invalid RAM scale: "RAM_SCALE""
#endif
#endif
#endif


//#define PALETTE_DIRECTION  POST_INC //TRUE //increasing address order
//#define NODE_DIRECTION  POST_DEC //FALSE //decreasing address order
#define PALETTE_DIRECTION  FALSE //increasing address order
#define NODE_DIRECTION  TRUE //decreasing address order
#define IsDecreasing(direction)  direction //((direction) & 1)

//#define MOVWI_NODE(/*fsr*/ignored)  MOVWI(adrsof(fsrL), NODE_DIRECTION)
//#define MOVWI_PALETTE(/*fsr*/ignored)  MOVWI(adrsof(fsrL), PALETTE_DIRECTION)
//#define MOVIW_NODE(/*fsr*/ignored)  MOVIW(adrsof(fsrL), NODE_DIRECTION)
//#define MOVIW_PALETTE(/*fsr*/ignored)  MOVIW(adrsof(fsrL), PALETTE_DIRECTION)
//#define incdec_NODE(/*fsr*/ignored)  incdec(fsrL, NODE_DIRECTION)
//#define incdec_PALETTE(/*fsr*/ignored)  incdec(fsrL, PALETTE_DIRECTION)
//volatile uint8 indf_autonode @adrsof(INDF_POSTDEC);
//volatile uint8 indf_autopal @adrsof(INDF_POSTINC);
#define indf_autonode  INDF_POSTDEC_ //decreasing address order
#define indf_autopal  INDF_POSTINC_ //increasing address order
#define indf_autopal_reverse  INDF_PREDEC_ //decreasing address order for reverse-filling palette
//for indirect addressing of parallel palette entries on PIC16X:
//use fsr0 for node groups, fsr1 for palette ptr
//#define indf_parapal_autofirst  INDF_POSTINC_ //_first_WREG()  MOVIW(0, POST_INC)
#ifdef PIC16X //CLOCK_FREQ >= 32 MHz
 volatile uint8 fsrL_parapal @adrsof(FSR1L); //#define fsrL_parapal  fsr1L
 volatile uint8 fsrH_parapal @adrsof(FSR1H); //#define fsrH_parapal  fsr1H
 volatile uint8 indf_parapal @adrsof(INDF1); //#define indf_parapal  indf1
 #define indf_parapal_auto  INDF1_POSTINC_ //indf_autopal //INDF_POSTINC_ //increasing address order
 #define indf_parapal_unauto  INDF1_POSTDEC_ //second  INDF_POSTDEC_ //second_WREG()  MOVIW(0, POST_DEC)
 #define indf_parapal_reverse  INDF1_PREDEC_ //decreasing address order for reverse-filling palette
#else //dummy definitions to satisfy first pass of compiler; THESE WILL NOT WORK; compiler should drop them as unused later
 #define fsrL_parapal fsrL
 #define fsrH_parapal fsrH
 #define indf_parapal indf
 #define indf_parapal_auto  INDF_POSTINC_
 #define indf_parapal_unauto  INDF_PREDEC_
 #define indf_parapal_reverse  INDF_PREDEC_
#endif
//load parallel palette ptr into fsr:
//indirect parallel palette entries are 2 bytes each
#define set_parapal_ptr(palinx)  \
{ \
	rl_WREG(palinx); /*fsrL_parapal = WREG*/; /*2x palinx*/ \
	WREG += INDIR_PALETTE_ADDR(0, 4 BPP); fsrL_parapal = WREG; /*ptr to parallel palette entry: 2x inx*/ \
	fsrH_parapal = INDIR_PALETTE_ADDR(0, 4 BPP) / 0x100; \
}

//#define indf_autonode_group  INDF1_POSTDEC_ //decreasing address order
volatile uint8 fsrL_node @adrsof(FSR0L); //#define fsrL_nodegrp  fsr1L
#define fsrH_node  fsrH //CAUTION: might be 1 bit or 5 bits //volatile uint8 fsrH_parapal @adrsof(FSR1H); //#define fsrH_nodegrp  fsr1H
volatile uint8 indf_node @adrsof(INDF0); //#define indf_nodegrp  indf1
//#define indf_nodegrp_auto  INDF1_POSTDEC_ //MOVIW(1, POST_DEC)
//copy parallel palette triplet to node group:
//parallel palette ptr moves up/down during this function, but is unchanged afterward
inline void copy_parapal2nodes(void)
{
	indf_autonode = indf_parapal_auto; indf_autonode = indf_parapal_unauto; //MOVWI_NODE(fsrL); /*add_banked(1, NON_INLINED, NODE_DIRECTION);*/
}


#define BPP  //dummy symbol for readability
#define Nodes2Bytes(nodes, bpp)  divup((nodes) * (bpp), 8)
#define Bytes2Nodes(bytes, bpp)  ((bytes) * 8 / (bpp))

//use banked RAM for nodes and RGB palette:
#ifdef PIC16X //extended instr set supports linear addressing: no bank gaps
 #define PALETTE_ADDR(palinx, bpp)  (0x2000 + 3 * (palinx)) //put palette at start of GPR in first bank for faster addressing during node I/O (fsrH == 0); 3 bytes per palette entry
 #define NODE_ADDR(node, bpp)  (0x2000 + TOTAL_BANKRAM - Nodes2Bytes((node) + 1, bpp)) //node data follows palette (can use spare palette entries), but begins at end of GPR in last bank and works backward; this allows both to start at fixed addresses, but space can be traded off between them
 #define MakeBanked(adrs)  Linear2Banked((adrs) & ~0x2000) //MPASM doesn't like linear addresses
#else //older PICs have gaps between GPR in each bank
 #define PALETTE_ADDR(palinx, bpp)  Linear2Banked(3 * (palinx)) //put palette at start of GPR in first bank for faster addressing during node I/O (fsrH == 0); 3 bytes per palette entry
//#define NODEND_ADDR  Linear2Banked(TOTAL_BANKRAM) //node data ends at end of banked GPR (for easier eof checking); spare palette entries can be used to hold extra node data
 #define NODE_ADDR(node, bpp)  Linear2Banked(TOTAL_BANKRAM - Nodes2Bytes((node) + 1, bpp)) //node data follows palette (can use spare palette entries), but begins at end of GPR in last bank and works backward; this allows both to start at fixed addresses, but space can be traded off between them
//#define PALETTE_ADDR(bpp)  Linear2Banked(TOTAL_BANKRAM - (1<<(bpp)) * 3) //put palette at end of GPR in last bank for faster addressing; 3 bytes per palette entry
//#define NODES_ADDR(node, bpp)  Linear2Banked((node)/(8/(bpp))) //put node data at start of GPR; first 2 banks can be addressed with fsrH == 0
 #define MakeBanked(adrs)  adrs
#endif
//#define FXDATA_ADDR(bpp)  PALETTE_ADDR(1 << (bpp)) //stash temp fx data immediately after palette; CAUTION: this uses some node space
//parallel palette entries use same address but are 4 bytes each:
#define PARALLEL_PALETTE_ADDR(palinx, bpp)  (PALETTE_ADDR(0, bpp) + ((palinx) * 4))
//indirect parallel palette entries occupy sameaddress space and are 2 bytes each:
#define INDIR_PALETTE_ADDR(palinx, bpp)  (PALETTE_ADDR(0, bpp) + 2 + ((palinx) * 2)) //kludge: offset by 2 bytes so last entry is past first 8 parallel palette entries, but first entry can still be self-referencing (typically used for all off)
//(node groups) are 2 bytes each; offset so first para

//#if bankof(Linear2Banked((3<<4) - 1)) != bankof(Linear2Banked(0))
#if PALETTE_ADDR(0, 4 BPP)/0x100 != PALETTE_ADDR(16, 4 BPP)/0x100
// #error "[ERROR] Palette addressing assumes palette will all fit within same bank, but it doesn't: " Linear2Banked((3<<4) - 1)""
 #error "[ERROR] Palette addressing assumes palette will all fit within same bank, but it doesn't: "PALETTE_ADDR(16, 4 BPP)/0x100""
#endif

#define NUM_NODES(bpp)  Bytes2Nodes(TOTAL_BANKRAM - (3<<bpp), bpp) //max #nodes with full palette
#define MAX_NODES(bpp)  Bytes2Nodes(TOTAL_BANKRAM - (3<<1), bpp) //max #nodes with minimal palette (2 entries)


//use non-banked RAM (16 bytes) for everything else:
// SourceBoost Linker Call Tree Dump
//main:
//	get_frame:
//		delay_10msec:
//			delay_50usec:
//		send_nodes:
//			delay_10msec:
//				delay_50usec:

//non-banked RAM is used to avoid bank selects during time-sensitive logic (all banked RAM is designated for node + palette data)
//memory is statically allocated in reverse order so length and offset can be embedded within the same #define

//stack frames:
#define LEAF_PROC_ADDR  (BANKLEN - 1) //for use within innermost (non-nested) functions, or can be used as a temp
//send_nodes (inner nesting, for low-level I/O):
#define PROTOCOL_STACKFRAME3_ADDR  (LEAF_PROC_ADDR - 3) //3 bytes for misc protocol handler data
//get_frame (outer nesting, for animation and protocol functions):
//#define GET_FRAME_STACKFRAME3_ADDR  (PROTOCOL_STACKFRAME3_ADDR - 3)

//stats:
#define IOERRS_ADDR  (PROTOCOL_STACKFRAME3_ADDR - 1) //stats; 8-bit counter (doesn't wrap)
#define PROTOERRS_ADDR  (IOERRS_ADDR - 1) //stats; 8-bit counter (doesn't wrap)
#define IOCHARS_ADDR  (PROTOERRS_ADDR - 3) //stats; 24-bit counter allows minimum of 11 minutes at 250 kbaud sustained

//global state (max 16 bytes; uses shared RAM bank to reduce bank selects and avoid node + palette address spaces):
#define NODE_BYTES_ADDR  (IOCHARS_ADDR - 1) //config; node data size
#define NODE_CONFIG_ADDR  (NODE_BYTES_ADDR - 1) //config; node type and packing
 #define NODETYPE_MASK  0xF0 //node type in upper nibble
 #define PARALLEL_NODES_ADDR  (8 * NODE_CONFIG_ADDR + 4) //bottom bit of node type indicates series vs. parallel for smart nodes
 #define COMMON_CATHODE_ADDR  (8 * NODE_CONFIG_ADDR + 4) //bottom bit of node type indicates common anode vs. cathode for dumb nodes
// #define OVERLAPPED_IO_ADDR  (8 * NODE_CONFIG_ADDR + 3) //overlapped I/O (currently only used by GECE during GetBitmap)
// #define UNUSED_NODECONFIG_BIT2_ADDR  (8 * NODE_CONFIG_ADDR + 2)
 #define IO_BUSY_ADDR /*ZC_STABLE_ADDR*/  (8 * NODE_CONFIG_ADDR + 3) //use unused bit to remember async I/O is active; somewhat related to node type, so put it here (running short of memory)
// #define ZZ_ADDR  (8 * NODE_CONFIG_ADDR + 2) //zig-zag strings
// #define OVERLAPPED_IO_ADDR  (8 * NODE_CONFIG_ADDR + 2) //overlapped I/O flag (used only for GECE I/O during GetBitmap)
 #define BPP_MASK  7 //0x03 //bottom 2 bits: 0x00 == 4 bpp, 0x01 == 1 bpp, 0x02 == 2 bpp, 0x03 == reserved for either 6 bpp or variable (tbd)
 #define ISBPP4_ADDR  (8 * NODE_CONFIG_ADDR + 2)
 #define ISBPP2_ADDR  (8 * NODE_CONFIG_ADDR + 1)
 #define ISBPP1_ADDR  (8 * NODE_CONFIG_ADDR + 0)
#define MYADRS_ADDR  (NODE_CONFIG_ADDR - 1) //config; controller address
//#define ADRSMODE_ADDR  (MAXNODES_ADDR - 1) //config
#define MISCBITS_ADDR  (MYADRS_ADDR - 1) //misc data
// #define ISBPP1_ADDR  (8 * MISCBITS_ADDR + 0)
// #define ISBPP2_ADDR  (8 * MISCBITS_ADDR + 1)
// #define BPP_MASK  0x03 //bottom 2 bits: 0x00 == 4 bpp, 0x01 == 1 bpp, 0x02 == 2 bpp, 0x03 == reserved for either 6 bpp or variable (tbd)
 #define WANT_ECHO_ADDR  (8 * MISCBITS_ADDR + 0)
// #define IO_BUSY_ADDR /*ZC_STABLE_ADDR*/  (8 * MISCBITS_ADDR + 1)
 #define ESC_PENDING_ADDR  (8 * MISCBITS_ADDR + 1) //escaped byte to process next
 #define PROTOCOL_INACTIVE_ADDR  (8 * MISCBITS_ADDR + 2) //whether to rcv protocol byte
 #define SENT_NODES_ADDR  (8 * MISCBITS_ADDR + 3) //did node I/O already occur
// #define RCV_BITMAP_ADDR  (8 * MISCBITS_ADDR + 4) //CAUTION: overwritten
// #define GET_FRAME_ISEOF_ADDR  (8 * MISCBITS_ADDR + 3) //animation playback eof/rewind
// #define PROTOCOL_ECHO_ADDR  (8 * MISCBITS_ADDR + 3) //whether to echo protocol byte
// #define NODE_BYTESH_ADDR  (8 * MISCBITS_ADDR + 3)
// #define SEND_NODES_NODEPTRH_ADDR  (8 * MISCBITS_ADDR + 4)
// #define NODESTARTH_ADDR  (8 * MISCBITS_ADDR + 3)
 #define statebit_0x10_ADDR  (8 * MISCBITS_ADDR + 4) //upper bits reserved for local function usage
 #define statebit_0x20_ADDR  (8 * MISCBITS_ADDR + 5) //upper bits reserved for local function usage
 #define statebit_0x40_ADDR  (8 * MISCBITS_ADDR + 6) //upper bits reserved for local function usage
 #define statebit_0x80_ADDR  (8 * MISCBITS_ADDR + 7) //upper bits reserved for local function usage

// #define GET_FRAME_CTLBLOCK_ADDR  GET_FRAME_STACKFRAME_ADDR
//	volatile uint8 node_stofs @NOFSR_ADDR, node_count @NOFSR_ADDR + 1, delay_count @NOFSR_ADDR + 2;
//	volatile uint8 pal_stofs @NOFSR_ADDR, pal_count @NOSFR_ADDR + 1, command @NOSFR_ADDR + 2;
//	volatile uint8 scroll_ofs @NOSFR_ADDR, scroll_count @NOSFR_ADDR + 1;
//	volatile uint8 regofs @NOSFR_ADDR, regval @NOSFR_ADDR + 1;
// #define GET_FRAME_NODE_TRIPLETS_ADDR  (GET_FRAME_PAL_COUNT_ADDR - 1)
// #define GET_FRAME_DELAY_25MSEC_ADDR  (GET_FRAME_NODE_TRIPLETS_ADDR - 1)
// #define GET_FRAME_FLAGS_ADDR  (GET_FRAME_DELAY_25MSEC_ADDR - 1)
// #define SEND_NODES_NODEPTR_ADDR  SEND_NODES_STACKFRAME_ADDR
// #define SEND_NODES_SKIPNODES_ADDR  SEND_NODES_STACKFRAME_ADDR
//delay (can be nested, so don't reuse space):
//#define GET_FRAME_DELAY_COUNT_ADDR  SEND_NODES_NODE_PAIRS_ADDR
//#define DELAY_5MSEC_ADDR  SEND_NODES_SKIP_NODES_ADDR
//#define DELAY_50USEC_ADDR  SEND_NODES_PALLEND_ADDR
//#define DELAY_1USEC_ADDR  SEND_NODES_PALSAVE_ADDR

//check next available RAM:
#define DEMO_STACKFRAME3_ADDR  (MISCBITS_ADDR - 3) //UNUSED3
//uint3x8 unused3 @adrsof(UNUSED3); //only for debug
#define NEXT_AVAIL_ADDR  (DEMO_STACKFRAME3_ADDR - 1) //(MISCBITS_ADDR - 1)
volatile uint8 next_avail @adrsof(NEXT_AVAIL); //only for debug
//if this drops below start of non-banked RAM, too much memory was allocated
#if NEXT_AVAIL_ADDR + 1 < NONBANKED
 #if NEXT_AVAIL_ADDR == 0x6F
  #define NEXT_AVAIL_ADDR_tostr  0x6F
 #else
 #if NEXT_AVAIL_ADDR == 0x6E
  #define NEXT_AVAIL_ADDR_tostr  0x6E
 #else
  #define NEXT_AVAIL_ADDR_tostr  NEXT_AVAIL_ADDR
 #endif
 #endif
 #error "[ERROR] Non-banked RAM over-allocated: "NEXT_AVAIL_ADDR_tostr" vs. "NONBANKED""
#endif

//icicle animation (mutually exclusive with get_frame, so reuse that space):
//#define ICICLES_ADDR  GET_FRAME_FLAGS_ADDR


volatile uint8 StateBits @adrsof(MISCBITS); //misc state bits; managed by their respective sections of code, but initialized below
volatile bit iobusy @BITADDR(adrsof(IO_BUSY)); //zc_stable @adrsof(ZC_STABLE)/8.(adrsof(ZC_STABLE) % 8); //initial 2 sec interval has passed
volatile bit protocol_inactive @BITADDR(adrsof(PROTOCOL_INACTIVE)); //zc_stable @adrsof(ZC_STABLE)/8.(adrsof(ZC_STABLE) % 8); //initial 2 sec interval has passed
volatile uint8 ioresume @MakeBanked(PALETTE_ADDR(16, 4 BPP) + 0); //adrsof(PROTOCOL_STACKFRAME3) + 2; //#bytes to send; count is more efficient than address compare (bytes not contiguous: 0x50..0x70, 0xa0..0xf0, 0x120..0x170); NOTE: units are byte pairs for 512 RAM or byte quads for 1K RAM
//volatile bit restarted @BITADDR(adrsof(RESTARTED_INACTIVE));
volatile uint8 wait4sync @adrsof(DEMO_STACKFRAME3) + 1 ;//UNUSED3)+1; //count how long wait for sync (useful only for debug or performance tuning)

//global init:
inline void global_init(void)
{
	uint8 count @adrsof(LEAF_PROC);
//	ONPAGE(0);
	init(); //prev init first

	StateBits = 0; //initialize all state bits with 1 clrf instr (try to arrange for initial/default value = 0)
//	iobusy = TRUE; //mark I/O busy until init completed; not really necessary because node type not set until after init period, but set this just in case
#if 0 //initialize RAM (for easier debug):
	fsrL = NODE_ADDR(0, 4 BPP) + 16; fsrH = (NODE_ADDR(0, 4 BPP) + 16)/ 0x100;
	count = TOTAL_BANKRAM / RAM_SCALE; //max possible with no palette
	for (;;)
	{
		WREG = 0xDB; //incdec_NODE(fsrL); //fill with value that's easy to see during debug
		indf_autonode = WREG; //MOVWI_NODE(fsrL); //add_banked(0, INLINED, NODE_DIRECTION, if (!fsrH) break);
		if (RAM_SCALE > 1) indf_autonode = WREG; //MOVWI_NODE(fsrL); //add_banked(0, INLINED, NODE_DIRECTION, if (!fsrH) break);
		if (RAM_SCALE > 2) { indf_autonode = WREG; indf_autonode = WREG; } //MOVWI_NODE(fsrL); MOVWI_NODE(fsrL); } //add_banked(0, INLINED, NODE_DIRECTION, if (!fsrH) break);
		add_banked(0, IsDecreasing(NODE_DIRECTION), 0); //adjust fsr for bank gaps/wrap; only need to check every 16 bytes
//		count -= 16 / RAM_SCALE; //update remaining byte count
//		if (/*count <= 0*/ Borrow || EqualsZ) break; //use separate stmt for avoid BoostC temp
		if (!--count) break; //update remaining byte count
	}
#endif
}
#undef init
#define init()  global_init() //initialization wedge, for compilers that don't support static initialization


//front panel:
//uses channel pins if separate status LEDS are not present (must be refreshed in that case)
//Renard-PX1 uses A0, A1, and A5 for status LEDs, so leave them at their correct values; NOTE: LEDs are connected directly to VCC, so status bits are inverted (provide ground)
//Pex Renard 1827 has different status LED pins and port (B6 and B7)
//PX1 and Pex-Ren1827 status LEDs are connected directly to VCC, so status bits are inverted (pull to ground to turn on)
//#define WAIT_CHAR  TRUE
//#define GOT_CHAR  FALSE
//#ifndef portFP
// #define portFP  0
//#endif
#ifndef fpbit_RXIO
 #define fpbit_RXIO  0 //TODO or not present
#endif
#ifndef fpbit_NODEIO
 #define fpbit_NODEIO  0 //TODO or not present
#endif
#ifndef fpbit_COMMERR
 #define fpbit_COMMERR  0 //TODO or not present
#endif
#ifndef fpbit_OTHER
 #define fpbit_OTHER  0 //TODO or not present
#endif
#ifndef FP_on
 #define FP_on  1
#endif
//#warning "[INFO] Display front panel"
#define FP_RXIO  (1 << PINOF(fpbit_RXIO))
#define FP_NODEIO  (1 << PINOF(fpbit_NODEIO))
#define FP_COMMERR  (1 << PINOF(fpbit_COMMERR))
#define FP_OTHER  (1 << PINOF(fpbit_OTHER))
#ifdef portFP
 #define front_panel(bitval)  \
 { \
	if (bitval == FP_COMMERR) portFP.PINOF(fpbit_COMMERR) = FP_on; \
	else if (bitval == ~FP_COMMERR) portFP.PINOF(fpbit_COMMERR) = ~FP_on; \
	else if (bitval == FP_RXIO) portFP.PINOF(fpbit_RXIO) = FP_on; \
	else if (bitval == ~FP_RXIO) portFP.PINOF(fpbit_RXIO) = ~FP_on; \
	else if (bitval == FP_OTHER) portFP.PINOF(fpbit_OTHER) = FP_on; \
	else if (bitval == ~FP_OTHER) portFP.PINOF(fpbit_OTHER) = ~FP_on; \
	else if (bitval == FP_NODEIO) portFP.PINOF(fpbit_NODEIO) = FP_on; \
	else if (bitval == ~FP_NODEIO) portFP.PINOF(fpbit_NODEIO) = ~FP_on; \
 }
#else //no status LEDs
 #define front_panel(ignore)
#endif
//#define BoostC_broken_front_panel(bitval) /*common cathode (positive logic)*/ \
//{ \
//	if (NumBits8(bitval) < 2) portX |= bitval; /*turn "on"*/ \
//	else portX &= ~(bitval); /*turn "off"*/ \
//}
// #define BoostC_broken_front_panel(bitval) /*common anode (inverted)*/ \
// { \
//	if (NumBits8(bitval) < 2) portFP &= ~(bitval); /*turn "on"*/ \
//	else portFP |= ~(bitval); /*turn "off"*/ \
// }
//#define xfront_panel(rcv)  \
//{ \
//	if (rcv) \
//	{ \
//		if (!ioerrs) porta |= 0x01; /*"FE"; comm errors (could be overruns also)*/ \
//		porta |= 0x20; /*"Rx"; turn it off until char received; it was also flickering during serial node I/O*/ \
//		if (!ProtocolErrors) porta |= 0x01; /*"attn"; protocol bugs or config errors*/ \
//	} \
//	else \
//	{ \
//		if (!rcv) porta &= ~0x20; /*"Rx"; turn it on until wait for next char*/ \
//	} \
// }
//#else
// #define front_panel(ignore)
// #undef front_panel
// #warning "[INFO] STATUS LED"
// #define front_panel(rcv)  \
// { \
//	if (rcv) porta |= 4; /*turn it on until char received*/ \
//	else porta &= ~4; /*turn off until wait for next char*/ \
// }
//#endif 


#define LINKERPAD  0 //kludge: set this to non-zero for "No remaining RAM block" errors with compiler temps; allows linker to generate output files in order to find and eliminate temps; NOTE: must be 0 for production code
#if LINKERPAD > 0
 #warning "[INFO] Linker pad: "LINKERPAD". DEBUG ONLY; NOT FOR LIVE USAGE"
#endif

//allocate all banked RAM for RGB palette + node data:
//this prevents compiler from allocating vars in banked RAM
//NOTE: to track down where BoostC generates temps, leave some unallocated banked RAM so linker will generate output files
volatile uint8 bank0[MIN(GPRLEN, TOTAL_BANKRAM) - LINKERPAD] @Linear2Banked(0); //0x20
#if TOTAL_BANKRAM > GPRLEN
 volatile uint8 bank1[MIN(GPRLEN, TOTAL_BANKRAM - GPRLEN - LINKERPAD)] @Linear2Banked(GPRLEN); //0x20 + 0x80;
#endif
#if TOTAL_BANKRAM > 2 * GPRLEN //for devices with 256 RAM
 volatile uint8 bank2[MIN(GPRLEN, TOTAL_BANKRAM - 2 * GPRLEN - LINKERPAD)] @Linear2Banked(2 * GPRLEN); //0x20 + 2 * 0x80;
#endif
#if TOTAL_BANKRAM > 3 * GPRLEN //for 16F1827 with 368 RAM
 volatile uint8 bank3[MIN(GPRLEN, TOTAL_BANKRAM - 3 * GPRLEN - LINKERPAD)] @Linear2Banked(3 * GPRLEN); //0x20 + 3 * 0x80;
 volatile uint8 bank4[MIN(GPRLEN, TOTAL_BANKRAM - 4 * GPRLEN - LINKERPAD)] @Linear2Banked(4 * GPRLEN); //0x20 + 4 * 0x80;
#endif
#if TOTAL_BANKRAM > 5 * GPRLEN //for devices with 1K
 volatile uint8 bank5[MIN(GPRLEN, TOTAL_BANKRAM - 5 * GPRLEN)] @Linear2Banked(5 * GPRLEN); //0x20 + 5 * 0x80;
 volatile uint8 bank6[MIN(GPRLEN, TOTAL_BANKRAM - 6 * GPRLEN)] @Linear2Banked(6 * GPRLEN); //0x20 + 6 * 0x80;
 volatile uint8 bank7[MIN(GPRLEN, TOTAL_BANKRAM - 7 * GPRLEN)] @Linear2Banked(7 * GPRLEN); //0x20 + 7 * 0x80;
 volatile uint8 bank8[MIN(GPRLEN, TOTAL_BANKRAM - 8 * GPRLEN)] @Linear2Banked(8 * GPRLEN); //0x20 + 8 * 0x80;
 volatile uint8 bank9[MIN(GPRLEN, TOTAL_BANKRAM - 9 * GPRLEN)] @Linear2Banked(9 * GPRLEN); //0x20 + 9 * 0x80;
 volatile uint8 bank10[MIN(GPRLEN, TOTAL_BANKRAM - 10 * GPRLEN)] @Linear2Banked(10 * GPRLEN); //0x20 + 10 * 0x80;
 volatile uint8 bank11[MIN(GPRLEN, TOTAL_BANKRAM - 11 * GPRLEN)] @Linear2Banked(11 * GPRLEN); //0x20 + 11 * 0x80;
 volatile uint8 bank12[MIN(GPRLEN, TOTAL_BANKRAM - 12 * GPRLEN)] @Linear2Banked(12 * GPRLEN); //x20 + 12 * 0x80;
#endif
#if TOTAL_BANKRAM > 13 * GPRLEN
// uint8 bank13[MIN(GPRLEN, TOTAL_BANKRAM - 13 * GPRLEN)] @Linear2Banked(13 * GPRLEN); //x20 + 13 * 0x80;
 #error "[ERROR] Add more bank allocs here: "TOTAL_BANKRAM""
#endif

//uint8 next_avail @NEXT_AVAIL_ADDR; //only for debug
//#if NEXT_AVAIL_ADDR < NONBANKED
// #error "[ERROR] Out of non-banked RAM: "NEXT_AVAIL_ADDR" vs. "NONBANKED"."
//#endif


//;=========================================================================================================================================================
//;Timing-related config:
//Both timers use different presets based on selected clock frequency and desired max time interval.
//;=========================================================================================================================================================

//convenience macros (for readability):
//CAUTION: might need parentheses, depending on usage
#define usec
#define msec  *1000
#define sec  *1000000
#define MHz  *1000000
//#define K  *1024 //needed sooner


//instruction timing:
//;NOTE: these require 32-bit arithmetic.
//Calculations are exact for small numbers, but might wrap or round for larger numbers (needs 32-bit arithmetic).
//;misc timing consts:
#define ONE_MSEC  (1 msec) //1000
#define ONE_SEC  (1 sec) //1000000
//#define MHz(clock)  ((clock)/ONE_SEC)
#define INSTR_CYCLES  4 //#clock cycles per instr

//define max int osc freq:
#ifdef PIC16X //extended instr set (faster)
 #define MAXINTOSC_FREQ  (32 MHz) //uses 4x PLL
#else
 #define MAXINTOSC_FREQ  (8 MHz)
#endif
#ifdef CLOCK_FREQ //ext clock will be present
// #define UseIntOsc  (CLOCK_FREQ == MAXINTOSC_FREQ)
#else //use int osc @max freq if ext clock not present
// #define UseIntOsc  TRUE
 #define CLOCK_FREQ  MAXINTOSC_FREQ
#endif
//#define CLOCK_FREQ  (eval(DEVICE)##_FREQ)
//#define CLOCK_FREQ  CLOCK_FREQ_eval(DEVICE) //(eval(DEVICE)##_FREQ)
//#define CLOCK_FREQ_eval(device)  eval(device)##_FREQ
//#define uSec2Instr(usec, clock)  ((usec) * InstrPerUsec(clock)) //CAUTION: scaling down CLOCK_FREQ avoids arith overflow, but causes rounding errors
#define uSec2Instr(usec, clock)  ((usec) * InstrPerMsec(clock) / ONE_MSEC) //CAUTION: scaling down CLOCK_FREQ avoids arith overflow, but causes rounding errors
//#define Instr2uSec(instr, clock)  ((instr) / InstrPerUsec(clock)) //CAUTION: scaling down CLOCK_FREQ avoids arith overflow, but causes rounding errors
#define Instr2uSec(instr, clock)  ((instr) * ONE_MSEC / InstrPerMsec(clock)) //CAUTION: scaling down CLOCK_FREQ avoids arith overflow, but causes rounding errors
//#define Instr2mSec(instr, clock)  ((instr) / InstrPerMsec(clock)) //CAUTION: scaling down CLOCK_FREQ avoids arith overflow, but causes rounding errors

#define InstrPerSec(clock)  ((clock)/INSTR_CYCLES)  //;#instructions/sec at specified clock speed; clock freq is always a multiple of INSTR_CYCLES
#define InstrPerMsec(clock)  (InstrPerSec(clock)/ONE_MSEC)  //;#instructions/msec at specified clock speed; not rounded but clock speeds are high enough that this doesn't matter
#define InstrPerUsec(clock)  (InstrPerSec(clock)/ONE_SEC) //CAUTION: usually this one is fractional, so it's not accurate with integer arithmetic


//small + accurate delay functions:
//these are used to help implement strict node I/O timing

inline void nop1(void)
{
	nop();
}
//consume 2 instr cycles using 1 word of prog space:
//inline void nop2(void)
//{
//	NOOP(2); //goto $+1
//}
//consume 2 instr (splittable):
//optimizer will combine into a goto $+1
inline void nop2(void)
{
	REPEAT(2, nop1());
//placeholder; CAUTION: other instr will be placed here
//	nop1();
}
inline void nop3(void)
{
	REPEAT(3, nop1());
//placeholder; CAUTION: other instr will be placed here
//	nop2_split();
}
//#define nop3()  nop3_split()
//consume 4 instr cycles using 1 word of prog space (plus 1 shared):
//call+return overhead = 4 instr
non_inline void nop4(void)
{
	ONPAGE(IOH_PAGE); //put on same page as protocol handler to reduce page selects (requires precise timing)
}
//consume 4 instr (splittable):
//inline void nop4_split(void)
//{
//	nop1();
////placeholder; CAUTION: other instr will be placed here
//	nop3();
//}
//consume 5 instr (splittable):
//inline void nop5_split(void)
//{
//	nop1();
////placeholder; CAUTION: other instr will be placed here
//	nop4();
//}
//#define nop5()  nop5_split()


//PIC16F1827 @32 MHz (8 MIPS) == 125 nsec/instr == 8 instr/usec; max delay == 128 usec
//PIC16F688 @20 MHz (5 MIPS) == 200 nsec/instr == 5 instr/usec; max delay ~= 205 usec

#define Timer0_range  (100 usec)
#define Timer1_halfRange  (50 msec/2) //kludge: BoostC gets /0 error with 50 msec (probably 16-bit arith overflow), so use a smaller value
#define Timer0_Limit  Instr2uSec(256 * Timer0_Prescalar, CLOCK_FREQ) //max duration for Timer 0
#define Timer1_halfLimit  Instr2uSec(256 * 256 / 2 * Timer1_Prescalar, CLOCK_FREQ) //max duration for Timer 1; avoid arith overflow error in BoostC by dividing by 2

//choose the smallest prescalar that will give the desired range:
//smaller prescalars give better accuracy, but it needs to be large enough to cover the desired range of time intervals
//since ext clock freq > int osc freq, only need to check faster (ext) freq here

//hard-coded values work, except BoostC has arithmetic errors with values of 32768 so try to choose an alternate value here
//#define Timer0_Prescalar  4 //max 256; 4:1 prescalar + 8-bit timer gives 1K instr total range (128 - 205 usec); 8-bit loop gives ~32 - 52 msec total
//#define Timer1_Prescalar  4 //max 8; 4:1 prescalar + 16-bit timer gives ~262K instr total range (~ 33 - 52 msec); 8-bit loop gives ~8 - 13 sec total
#define Timer0_Prescalar  1
#if Timer0_Limit < Timer0_range
 #undef Timer0_Prescalar
 #define Timer0_Prescalar  2
 #if Timer0_Limit < Timer0_range
  #undef Timer0_Prescalar
  #define Timer0_Prescalar  4
  #if Timer0_Limit < Timer0_range
   #undef Timer0_Prescalar
   #define Timer0_Prescalar  8
   #if Timer0_Limit < Timer0_range //could go up to 256, but getting inaccurate by then so stop here
    #error "[ERROR] Can't find a Timer0 prescalar to give "Timer0_range" usec range"
   #endif
  #endif
 #endif
#endif

#define Timer1_Prescalar  1
//#warning "T1 limit @ "Timer1_Prescalar" ps = "Timer1_halfLimit""
//#if Timer1_halfLimit < 0
// #warning "[WARNING] BoostC arithmetic overflow, trying work-around"
//#endif
#if Timer1_halfLimit < Timer1_halfRange
 #undef Timer1_Prescalar
 #define Timer1_Prescalar  2
// #warning "T1 limit @ "Timer1_Prescalar" ps = "Timer1_halfLimit""
 #if Timer1_halfLimit < Timer1_halfRange
  #undef Timer1_Prescalar
  #define Timer1_Prescalar  4
//  #warning "T1 limit @ "Timer1_Prescalar" ps = "Timer1_halfLimit""
  #if Timer1_halfLimit < Timer1_halfRange
   #undef Timer1_Prescalar
   #define Timer1_Prescalar  8
//   #warning "T1 limit @ "Timer1_Prescalar" ps = "Timer1_halfLimit""
   #if Timer1_halfLimit < Timer1_halfRange //exceeded max prescalar here
    #warning "[ERROR] Can't find a Timer1 prescalar to give 2*"Timer1_halfRange" usec range; limit was 2*"Timer1_halfLimit""
   #endif
  #endif
 #endif
#endif


#if Timer0_Limit == 256*4000/8000 //8 MIPS
 #define Timer0_Limit_tostr  "128 usec"
// #undef Timer0_Limit
// #define Timer0_Limit  128 //kludge: avoid macro body problems or arithmetic errors in BoostC
#else
#if Timer0_Limit == 256*2000/4608 //4.6 MIPS
 #define Timer0_Limit_tostr  "111 usec"
// #undef Timer0_Limit
// #define Timer0_Limit  222 //kludge: avoid macro body problems or arithmetic errors in BoostC
#else
#if Timer0_Limit == 256*4000/5000 //5 MIPS
 #define Timer0_Limit_tostr  "204 usec"
// #undef Timer0_Limit
// #define Timer0_Limit  204 //kludge: avoid macro body problems or arithmetic errors in BoostC
#else
#if Timer0_Limit == 256*4000/4608 //4.6 MIPS
 #define Timer0_Limit_tostr  "222 usec"
// #undef Timer0_Limit
// #define Timer0_Limit  222 //kludge: avoid macro body problems or arithmetic errors in BoostC
#else
#if Timer0_Limit == 0
 #error "[ERROR] Timer 0 limit arithmetic is broken"
#else
 #define Timer0_Limit_tostr  Timer0_Limit usec
#endif
#endif
#endif
#endif
#endif

#if Timer1_halfLimit == 256*256/2*8000/8000 //8 MIPS
 #define Timer1_limit_tostr  "65.536 msec" //"32.768 msec"
// #undef Timer1_halfLimit
// #define Timer1_halfLimit  32768 //kludge: avoid macro body problems or arithmetic errors in BoostC
#else
#if Timer1_halfLimit == 256*256/2*4000/5000 //5 MIPS
 #define Timer1_limit_tostr  "52.428 msec"
// #undef Timer1_halfLimit
// #define Timer1_halfLimit  52428 //kludge: avoid macro body problems or arithmetic errors in BoostC
#else
#if Timer1_halfLimit == 256*256/2*4000/4608 //4.6 MIPS
 #define Timer1_limit_tostr  "56.888 msec"
// #undef Timer1_halfLimit
// #define Timer1_halfLimit  65535 //kludge: BoostC treats 65536 as 0 *sometimes*
// #define Timer1_halfLimit  56888 //kludge: avoid macro body problems or arithmetic errors in BoostC
#else
#if Timer1_halfLimit < 1
 #error "[ERROR] Timer 1 limit arithmetic is broken"
#else
 #define Timer1_limit_tostr  Timer1_halfLimit "*2 msec"
#endif
#endif
#endif
#endif

#warning "[INFO] Timer 0 limit is "Timer0_Limit_tostr" with "Timer0_Prescalar":1 prescalar, Timer 1 limit is "Timer1_limit_tostr" with "Timer1_Prescalar":1 prescalar."


volatile bit Timer0Wrap @adrsof(INTCON).T0IF; //timer 0 8-bit wrap-around
volatile bit Timer1Wrap @adrsof(PIR1).TMR1IF; //timer 1 16-bit wrap-around
volatile bit Timer1Enable @adrsof(T1CON).TMR1ON; //time 1 on/off


//asociate timer regs with names:
#define Timer0_reg  tmr0
#define Timer1_reg  tmr1L//_16
#define Timer0_ADDR  TMR0
#define Timer1_ADDR  TMR1L

#define TimerPreset(duration, overhead, which, clock)  (0 - rdiv(uSec2Instr(duration, clock) + overhead, which##_Prescalar))
//#if (Timer1_halfLimit == 32768) //&& ((256 * 256 * 4) / Timer1_halfLimit != 8) //8 MIPS
// #warning "[WARNING] BoostC arithmetic broken; applying kludgey work-around"
// #undef Timer1_halfLimit
// #define Timer1_halfLimit  16384
// #define Timer1_divkludge  2
//// #define Timer1_Preset_50000
//#else
// #define Timer1_divkludge  1
//#endif

//non_inline void protocol(void); //fwd ref
///*if (/ *!NoSerialWaiting* / ProtocolListen)*/ 
//#define Wait4Timer(which, duration, idler, uponserial)  \
//	Wait4TimerThread(which, duration, EMPTY, { idler; SerialCheck(IGNORE_FRAMERRS, { protocol(); uponserial; }}))

//values for while_waiting:
#define ASYNC_TIMER  break //break out of wait loop so timer will continue to run asynchronously
#define BLOCK_TIMER  EMPTY //block caller while waiting for timer
//values for time_base:
#define CUMULATIVE_BASE  TRUE //+= //maintain overall time; overruns will be compensated for during next time interval (hopefully)
#define ABSOLUTE_BASE  FALSE //= //try for accurate delay period; ignore previous overruns
//#define op_CUMULATIVE_BASE  +=
//#define op_ABSOLUTE_BASE  =
//#define op_TRUE  +=
//#define op_FALSE  =

#define StartTimer(which, duration, time_base)  Wait4Timer(which, duration, EMPTY, ASYNC_TIMER, time_base)
#define Wait4Timer(which, duration, before_waiting, while_waiting, time_base)  \
{ \
	if (adrsof(which) == adrsof(Timer1)) \
	{ \
 		/*if (time_base == ABSOLUTE_BASE) tmr1L = 0*/; /*Microchip work-around to avoid wrap while setting free-running timer; postpones wrap until MSB can be set*/ \
 		t1con.TMR1ON = FALSE; /*for cumulative intervals, can't use Microchip workaround and must set low byte first, but then need a temp for _WREG variant; just disable timer during update for simplicity*/ \
		WREG = TimerPreset(duration, IIF(time_base, 8, 6), which, CLOCK_FREQ) / 0x100; /*BoostC sets LSB first, which might wrap while setting MSB; explicitly set LSB first here to avoid premature wrap*/ \
		if (time_base) tmr1H /*op_##time_base*/ += WREG; else tmr1H = WREG; \
	} \
	WREG = TimerPreset(duration, IIF(adrsof(which) == adrsof(Timer1), IIF(time_base, 8, 6), 0), which, CLOCK_FREQ); \
	Wait4Timer_WREG(which, before_waiting, while_waiting, time_base); \
}
//duration pre-loaded in WREG:
//not for use with Timer1 (tmr1H conflict with WREG holding tmr1L value)
#define StartTimer_WREG(which, time_base)  Wait4Timer_WREG(which, EMPTY, ASYNC_TIMER, time_base)
#define Wait4Timer_WREG(which, before_waiting, while_waiting, time_base)  \
{ \
	if (time_base) which##_reg /*op_##time_base*/ += WREG; else which##_reg = WREG; \
	if (adrsof(which) == adrsof(Timer1)) { if (time_base && Carry) ++tmr1H; t1con.TMR1ON = TRUE; } \
	which##Wrap = FALSE; /*reset *after* setting timer so "0" will be interpreted as max delay*/ \
	/*which##Enable = FALSE*/; \
	/*BANKSELECT(which##Wrap)*/; /*reduce timing jitter on non-extended PICs*/ \
	/*clrwdt()*/; /*if timer loop never exits (due to bug?) fido will catch it*/ \
	before_waiting; /*NOTE: this counts as part of time interval*/ \
	FinishWaiting4Timer(which, while_waiting); \
}
//wait for dimming timeslot to finish:
#define FinishWaiting4Timer(which, while_waiting)  \
{ \
	for (;;) \
	{ \
		while_waiting; \
		if (which##Wrap) break; \
	} \
/*continue_waiting: / *extra label in case while_waiting wants to finish timer interval after serial char received*/ \
}

//#define old  SerialCheck(IGNORE_FRAMERRS, { if (/*!NoSerialWaiting*/ ProtocolListen) protocol(); uponserial; }); /*status.Z => interval completed; filter out frame errors (open RS485 line) when timer is used for demo (to preserve timing); */ \


//wait for specified time (usec):
//used for larger delays such as reset period before/after node I/O or animation timing where high accuracy is not needed (moderate jitter occurs during serial port activity, especially frame errors)
//only 1 timer is used: Timer 0 is used for short times (< ~ 200 usec), Timer 1 for longer times
//for multi-tasking, another thread (idler) can run while waiting for timer; this feature is used heavily - for example, to handle protocol I/O
//NOTE: serial data pre-empts current delay period
//on exit, status.Z => time interval was completed (not interrupted by serial I/O)
//32 MIPS + 4:1 prescalar => 2 M / sec
//#warning "Timer1 limit = "Timer1_halfLimit""
//#warning "20th sec = "divup(1 sec/20, Timer1_halfLimit)""
//uint8 debug1 @0x70 = ((50000)/2 + 32768/2) / (32768/2);
//uint16 debug2 @0x70 = ((50000)/2 + 32768/2) / (32768/2);
//uint16 debug3 @0x70 = 32768/2;
#define delay_loop_adjust(ignored)  EMPTY //overridable by caller; use this instead of another macro param
#define wait(duration, idler)  \
{ \
	if ((duration) < Timer0_Limit) Wait4Timer(Timer0, duration, EMPTY, idler/*, goto stop_waiting*/, ABSOLUTE_BASE); /*~ 100 - 200 usec; use Timer 0 directly*/ \
	else if ((duration)/2 < Timer1_halfLimit) Wait4Timer(Timer1, duration, EMPTY, idler/*, goto stop_waiting*/, ABSOLUTE_BASE); /*~ 33 - 52 msec; use Timer 1 directly*/ \
	else /* use 8-bit loop with Timer 0 or 1*/ \
	{ \
		volatile uint8 delay_loop @adrsof(LEAF_PROC); \
		if ((duration) < 256 * Timer0_Limit) { delay_loop = divup(duration, Timer0_Limit); tmr0 = 0; } /*~ 32 - 52 msec; use 8-bit loop with Timer 0*/ \
		else { delay_loop = divup((duration)/2, Timer1_halfLimit); tmr1_16 = 0; } /*~ 8 - 13 sec; use 8-bit loop with Timer 1*/ \
		delay_loop_adjust(delay_loop); /*give caller one last chance before starting the loop; NOTE: this counts as part of time interval*/ \
		for (;;) /*NOTE: cumulative timer will compensate for idler overrun during next timer cycle*/ \
		{ \
			if ((duration) < 256 * Timer0_Limit) Wait4Timer(Timer0, /*Timer0_Limit*/ rdiv(duration, divup(duration, Timer0_Limit)), EMPTY, idler/*, goto stop_waiting*/, CUMULATIVE_BASE); /*try to make delay more accurate based on outer loop; div up to reduce chances of overflow and cumulative will catch up*/ \
			else Wait4Timer(Timer1, /*Timer1_halfLimit*/ rdiv(duration, divup((duration)/2, Timer1_halfLimit)), EMPTY, idler/*, goto stop_waiting*/, CUMULATIVE_BASE); /*try to make delay more accurate based on outer loop; div up to reduce chances of overflow and cumulative will catch up*/ \
			/*if (!NoSerialWaiting) break*/; /*wait interval interrupted by serial data*/ \
			if (--delay_loop) continue; /*NOTE: decfsz does NOT set status.Z*/ \
			/*NoSerialWaiting = TRUE*/; /*interval completed*/ \
			break; \
		} \
	} \
/*stop_waiting:*/ \
}
//			else if (Timer1_halfLimit == 32768) Wait4Timer(Timer1, rdiv((duration)/4, divup((duration)/4, 32768/4)), EMPTY, idler); /*kludge: BoostC broken again*/
		

//Int osc configuration:
//Prescalar and preset values are defined below based on desired clock speed
#ifdef PIC16X //extended/faster PIC
 #define PLL  IIF(CLOCK_FREQ > 8 MHz, 4, 1) //need PLL
//BoostC doesn't like IIF in #pragma config
// #if CLOCK_FREQ > (8 MHz)
//  #define PLL  4 //need PLL for faster speeds
// #else
//  #define PLL  1
// #endif
 #define IntOsc_PrescalarOffset  (8-1) //kludge: 2 values for LFINTOSC; skip 1
 #define IntOsc_MaxPrescalar  15
// #define UseIntOsc  (CLOCK_FREQ <= 8 MHz * PLL) //PIX16X //int osc is fast enough (with PLL)
#else //non-extended (slower) processors
 #define PLL  1 //no PLL available
 #define IntOsc_PrescalarOffset  0
 #define IntOsc_MaxPrescalar  7
#endif
//#define PLL  IIF(CLOCK_FREQ > 8 MHz, 4, 1) //need PLL; BoostC doesn't like IIF in #pragma config
//#if CLOCK_FREQ > (8 MHz)
// #define PLL  4
//#else
// #define PLL  1
//#endif
#define UseIntOsc  (CLOCK_FREQ <= MAXINTOSC_FREQ) //8 MHz * PLL) //PIX16X //int osc is fast enough (with PLL)
//#if CLOCK_FREQ > 8 MHz * PLL
// #define UseIntOsc  FALSE //too fast for int osc
//#else
// #define UseIntOsc  TRUE
//#endif
//#define SCS_INTOSC  1 //0 //use config, which will be set to int osc (PLL requires 0 here)

#if PLL != 1
 #define IFPLL(stmt)  stmt
#else
 #define IFPLL(stmt)
#endif


#ifndef LFINTOSC_FREQ
 #define LFINTOSC_FREQ  31250 //31.25 KHz LFINTOSC
#endif

//#define IntOsc_MinPrescalarSize  0 //smallest configurable Int Osc prescalar bit size; smallest prescalar is 1:1; 1:1 is actually a special case (LFINTOSC)
//#define IntOsc_MaxPrescalarSize  7 //largest configurable Int Osc prescalar bit size; largest prescalar is 1:128
//#define Timer0_PrescalarSize(range, clock)  NumBits(uSec2Instr(range, clock))-8) //;bit size of prescalar
//#define Timer0_Prescalar(clock)  Prescalar(Timer0_Range, clock) //;gives a Timer0 range of Timer0_Range at the specified clock frequency
//#define IntOsc_Prescalar(clock)  (NumBits8(InstrPerSec(clock) / LFINTOSC_FREQ) + IntOsc_PrescalarOffset) //;Int osc prescalar
#define IntOsc_Prescalar(clock)  MIN(NumBits8(InstrPerSec(clock) / LFINTOSC_FREQ) + IntOsc_PrescalarOffset,  IntOsc_MaxPrescalar) //;Int osc prescalar

//OSCCON config:
//Sets internal oscillator frequency and source.
#define MY_OSCCON(clock)  \
(0 \
	| (IntOsc_Prescalar((clock) / PLL) << IRCF0) /*set clock speed; bump up to max*/ \
	| IIF(UseIntOsc /*&& (PLL == 1)*/, 0, 1 << SCS0) /*;use CONFIG clock source (ext clock), else internal osc; NOTE: always use int osc; if there's an ext clock then it failed*/ \
)


//OPTIONS reg configuration:
//Turns misc control bits on/off, and set prescalar as determined above.
#define MY_OPTIONS(clock)  \
(0 \
	| IIF(FALSE, 1<<NOT_WPUEN, 0) /*;enable weak pull-ups on PORTA (needed to pull ZC high when open); might mess up charlieplexing, so turn off for other pins*/ \
	| IIF(DONT_CARE, 1<<T0SE, 0) /*;Timer 0 source edge: don't care*/ \
	| IIF(DONT_CARE, 1<<INTEDG, 0) /*;Ext interrupt not used*/ \
	| IIF(FALSE, 1<<PSA, 0) /*;FALSE => pre-scalar assigned to timer 0, TRUE => WDT*/ \
	| IIF(FALSE, 1<<T0CS, 0) /*FALSE: Timer 0 clock source = (FOSC/4), TRUE: T0CKI pin*/ \
	| ((NumBits8(Timer0_Prescalar) - 2) << PS0) /*;prescalar value log2*/ \
)


//;T1CON (timer1 control) register (page 51, 53):
//;set this register once at startup only, except that Timer 1 is turned off/on briefly during preset (as recommended by Microchip)
#define MY_T1CON(clock)  \
(0 \
	| IIF(FALSE, 1<<TMR1GE, 0) /*;timer 1 gate-enable off (timer 1 always on)*/ \
	| IIF(FALSE, 1<<T1OSCEN, 0) /*;LP osc disabled (timer 1 source = ext osc)*/ \
	| IIF(DONT_CARE, 1<<NOT_T1SYNC, 0) /*;no sync with ext clock needed*/ \
	| (0<<TMR1CS0) /*;use system clock (config) always*/ \
	| (1<<TMR1ON) /*;timer 1 on*/ \
	| ((NumBits8(Timer1_Prescalar) - 1) << T1CKPS0) /*;prescalar on timer 1 (page 53); <T1CKPS1, T1CKPS0> form a 2-bit binary value*/ \
)


//WDT config:
//;WDT time base is 31.25 KHz LFINTOSC.  This does not depend on the HF int osc or ext clock, so it is independent of any instruction timing.
//;Since there will be a RTC tick every ~ 20 msec, WDT interval should be > 20 msec.
//If prescalar is assigned to WDT, it is set to 1:1 so that the Timer 0 prescalar calculations do not affect WDT calculations.
//;With no prescalar, possible WDT postscalar values are 1:32 (1 msec), 1:64, 1:128, 1:256, 1:512 (16 msec), 1:1024, 1:2048, 1:4096, 1:8192, 1:16384, 1:32768, or 1:65536 (~ 2 sec).
//#define WDT_Postscalar  1024  //;the smallest WDT internval > 20 msec
//#define WDT_Postscalar(range, clock)  (4096/IIF(MY_OPTIONS(range, clock) & (1<<PSA), 2, 1))  //;if nothing has happened within 1/10 sec, something's broken (must be > 20 msec)
//#define WDT_Postscalar(range, clock)  (4096/(IIF0(!NeedsPrescalar(range, clock), 1, 0) + 1))  //;if nothing has happened within 1/10 sec, something's broken (must be > 20 msec)
#define WDT_halfRange  MAX(Timer0_Limit/2, Timer1_halfLimit) //min interval wanted for WDT (usec)
//#define WDT_Postscalar(range, clock)  (1<<WDT_PostscalarSize(range, clock))
#define WDT_MinPostscalarSize  5 //smallest configurable WDT postscalar bit size; smallest postscalar is 1:32
#define WDT_MaxPostscalarSize  16 //largest configurable WDT postscalar bit size; largest postscalar is 1:65536
//#define WDT_Postscalar  NumBits8(WDT_Range / LFINTOSC_FREQ) //;WDT postscalar; divide by LFINTOSC_FREQ has already taken into account WDT_MinPostscalar == 5
//#define WDT_Postscalar  (NumBits8(divup(WDT_Range, 32 * ONE_MSEC)) + 5) //;WDT postscalar; divide by LFINTOSC_FREQ has already taken into account WDT_MinPostscalar == 5
//#define WDT_Postscalar  NumBits16(divup(WDT_Range, ONE_MSEC)) //;WDT postscalar; divide by 1 msec has already taken into account WDT_MinPostscalar == 5
#define WDT_Postscalar  NumBits16((WDT_halfRange >> (WDT_MinPostscalarSize-1)) * 11/10 / (ONE_SEC/LFINTOSC_FREQ)) //;WDT postscalar; set target period 10% larger than desired interval to avoid WDT triggering
//#define WDT_Postscalar  NumBits8(WDT_Range / (ONE_SEC >> WDT_MinPostscalarSize)) //;WDT postscalar
//#define IntOsc_Prescalar(clock)  NumBits8(InstrPerSec(clock) / (LFINTOSC_FREQ >> IntOsc_MinPrescalarSize)) //;Int osc prescalar
#undef WDT_Postscalar //TODO: macro body overflow
#define WDT_Postscalar  7 //128 msec

#if (WDT_Postscalar < 0) || (WDT_Postscalar > WDT_MaxPostscalarSize - WDT_MinPostscalarSize)
 #error "WDT postscalar out of range "WDT_MinPostscalarSize".."WDT_MaxPostscalarSize""
#endif


//;WDTCON (watchdog control) register (page 126):
//;set this register once at startup only
//NOTE: if Timer0/WDT prescalar is assigned to WDT, it's set to 1:1 so the calculations below are independent of Timer 0.
//If WDT thread runs 1x/sec, WDT interval should be set to 2 sec.
#define MY_WDTCON  \
(0  \
	| (WDT_Postscalar << WDTPS0) /*;<WDTPS3, WDTPS2, WDTPS1, WDTPS0> form a 4-bit binary value:; 1:64 .. 1:64K postscalar on WDT (page 126)*/ \
	| IIF0(DONT_CARE, 1<<SWDTEN, 0) /*;WDT is enabled via CONFIG; wdtcon has no effect here unless off in CONFIG*/ \
)
//IFWDT(non_volatile uint8 FixedAdrs(initialized_wdtcon, WDTCON) = MY_DTCON); //;??NOTE: this seems to turn on WDT even if it shouldn't be on
#warning "TODO: WDT, BOR"

#if UseIntOsc //don't need to check if ext clock failed
 #define ExtClockFailed  FALSE
#else
 volatile bit ExtClockFailed @adrsof(PIR1).OSFIF; //ext clock failed; PIC is running on int osc
#endif


//initialize timing-related regs:
//NOTE: power-up default speed for PIC16F688 is 1 MIPS (4 MHz)
inline void TimingSetup(void)
{
	init(); //prev init first
//??	if (!osccon.OSTS) //running from int osc
	if (UseIntOsc || ExtClockFailed) /*can only change int osc freq*/
	{
//		if (CLOCK_FREQ == MAXINTOSC_FREQ)
//#define UseIntOsc  (CLOCK_FREQ <= 8 MHz * PLL) //PIX16X //int osc is fast enough (with PLL)
#if CLOCK_FREQ > MAXINTOSC_FREQ
 #warning "[CAUTION] Desired timing is not achievable with int osc"
		if (ExtClockFailed) osccon = MY_OSCCON(MAXINTOSC_FREQ); //run as fast as we can on internal oscillator
		else
#endif
//		osccon = 0x70; //8 MHz or 32 MHz (2 MIPS or 8 MIPS); 8 MIPS ==> 125 nsec/instr, 2 MIPS ==> 500 nsec/instr
		osccon = MY_OSCCON(CLOCK_FREQ); //| IIF(!UseIntOsc, 1 << SCS, 0); /*should be 0x70 for 32MHz (8 MHz + PLL) 16F1827 or 8MHz 16F688*/;NOTE: FCM does not set SCS, so set it manually (page 31); serial port seems to be messed up without this!
#if 0 //is this really needed? (initialization code takes a while anyway); easier IDE debug without it
		while (!oscstat.HFIOFS); /*wait for new osc freq to stabilize (affects other timing)*/
		IFPLL(while (!oscstat.PLLR)); /*wait for PLL to stabilize*/
#endif
	}
//	option_reg = 0x87; //weak pullups disabled; 1:256 prescalar assigned to Timer 0
	option_reg = MY_OPTIONS(CLOCK_FREQ / PLL); /*should be 0x00 for 1:2 prescalar or 0x01 for 1:4 prescalar (0x80/0x81 if no WPU)*/
	t1con = MY_T1CON(CLOCK_FREQ / PLL); //configure + turn on Timer 1; should be 0x21 for 1:4 prescalar
//	wdtcon = MY_WDTCON; //config and turn on WDT ;??NOTE: this seems to turn on WDT even if it shouldn't be on??; should be 0x0a for 32 msec, 0x0c for 64 msec, 0x0e for 128 msec, 0x16 for 2 sec interval
}
#undef init
#define init()  TimingSetup() //initialization wedge, for compilers that don't support static initialization


//;==================================================================================================
//;Processor config section
//;==================================================================================================

#define MY_CONFIG1  \
(0xFFFF \
	& _IESO_OFF  /*;internal/external switchover not needed; turn on to use optional external clock?  disabled when EC mode is on (page 31); TODO: turn on for battery-backup or RTC*/ \
	& _BOD_OFF  /*;brown-out disabled; TODO: turn this on when battery-backup clock is implemented?*/ \
	& _CPD_OFF  /*;data memory (EEPROM) NOT protected; TODO: CPD on or off? (EEPROM cleared)*/ \
	& _CP_OFF  /*;program code memory NOT protected (maybe it should be?)*/ \
	& _MCLRE_OFF  /*;use MCLR pin as INPUT pin (required for AC line sync with Renard); no external reset needed anyway*/ \
	& _PWRTE_ON  /*;hold PIC in reset for 64 msec after power up until signals stabilize; seems like a good idea since MCLR is not used*/ \
	& _WDT_OFF/*ON*/  /*;use WDT to restart if software crashes (paranoid); WDT has 8-bit pre- (shared) and 16-bit post-scalars (page 125)*/ \
	& IIF(UseIntOsc, _INTRC_OSC_NOCLKOUT, _EC_OSC)  /*;I/O on RA4, CLKIN on RA5; external clock (18.432 MHz); if not present, int osc will be used*/ \
	& _FCMEN_ON  /*;turn on fail-safe clock monitor in case external clock is not connected or fails (page 33); RA5 will still be configured as clock input though (not available for I/O)*/ \
/*;disable fail-safe clock monitor; NOTE: this bit must explicitly be turned off since MY_CONFIG started with all bits ON*/ \
)


#ifdef PIC16X //extended
 #define MY_CONFIG2  \
 (0xFFFF \
	& _WRT_OFF  /*Write protection off*/ \
	& IIF(PLL != 1, _PLLEN_ON, _PLLEN_OFF)  /*4x PLL only needed for top speed*/ \
	& _STVREN_OFF  /*Stack Overflow or Underflow will NOT cause a Reset (circular)*/ \
	& _BORV_25  /*Brown-out Reset Voltage (VBOR) set to 2.5 V*/ \
	& _LVP_OFF  /*High-voltage on MCLR/VPP must be used for programming*/ \
 )

 #pragma DATA _CONFIG1, MY_CONFIG1
 #pragma DATA _CONFIG2, MY_CONFIG2
#else
 #pragma DATA _CONFIG, MY_CONFIG1
#endif


//kludge: #warning doesn't reduce macro values, so force it here (mainly for debug/readability):
#if CLOCK_FREQ == 20 MHz
 #if UseIntOsc
  #define CLOCK_FREQ_tostr  "20 MHz (int)"
 #else
  #define CLOCK_FREQ_tostr  "20 MHz (ext)"
 #endif
#else
#if CLOCK_FREQ == 32 MHz
 #if UseIntOsc
  #define CLOCK_FREQ_tostr  "32 MHz (int, with PLL)"
 #else
  #define CLOCK_FREQ_tostr  "32 MHz (ext, with PLL)"
 #endif
#else
#if CLOCK_FREQ == 18432000
 #if UseIntOsc
  #define CLOCK_FREQ_tostr  "18.432 MHz (int)"
 #else
  #define CLOCK_FREQ_tostr  "18.432 MHz (ext)"
 #endif
#else
 #define CLOCK_FREQ_tostr  CLOCK_FREQ UseIntOsc
#endif
#endif
#endif

#ifdef PIC16X //extended instr set
 #warning "[INFO] Compiled for "DEVICE" running at "CLOCK_FREQ_tostr" with extended instruction set."
// #define GPRAM_LINEAR_START  0x2000
// #warning "device "DEVICE" has ext instr set, "GPRAM_LINEAR_SIZE" ram"
// #define RAM(device)  device##_RAM
/////// #define _GPRAM_LINEAR_SIZE(device)  (concat(device, _RAM) - 16)
//#define GPRAM_LINEAR_SIZE  ((4 * 0x50) + 0x30) //384 excludes 16 bytes non-banked GP RAM
//#define GPRAM_LINEAR_SIZE  ((12 * 0x50) + 0x30) //1024 excludes 16 bytes non-banked GP RAM
//#define GPRAM_LINEAR_SIZE  ((1 * 0x50) + 0x20) //128 excludes 16 bytes non-banked GP RAM
//#define GPRAM_LINEAR_SIZE  ((3 * 0x50)) //256 excludes 16 bytes non-banked GP RAM
#else
 #warning "[INFO] Compiled for "DEVICE" running at "CLOCK_FREQ_tostr" with NON-extended instruction set."
#endif


//;=========================================================================================================================================================
//;Serial port handling and config:
//;=========================================================================================================================================================

#define KBAUD  *1000


#define MY_BAUDCTL  \
(0  /*;default to all bits off, then turn on as needed*/ \
	| IIF(HIGHBAUD_OK, 1<<BRG16, 0)  /*;16-bit baud rate generator; SEE ERRATA PAGE 2 REGARDING BRGH && BRG16; NOT needed since we are NOT using low baud rates*/ \
	| IIF(FALSE, 1<<SCKP, 0)  /*;synchronous clock polarity select: 1 => xmit inverted; 0 => xmit non-inverted*/ \
	| IIF(FALSE, 1<<WUE, 0)  /*;no wakeup-enable*/ \
	| IIF(FALSE, 1<<ABDEN, 0)  /*;no auto-baud rate detect (not reliable - consumes first char, which must be a Break)*/ \
)


//;Transmit Status and Control (TXSTA) register (page 87, 100):
//;This register will be set at startup, but individual bits may be turned off/on again later to reset the UART.
#define MY_TXSTA  \
(0  /*;default to all bits off, then turn on as needed*/ \
	| IIF(TRUE, 1<<BRGH, 0)  /*;high baud rates*/ \
	| IIF(FALSE, 1<<SYNC, 0)  /*;async mode*/ \
	| IIF(FALSE, 1<<TX9, 0)  /*;don't want 9 bit data for parity*/ \
	| IIF(TRUE, 1<<TXEN, 0)  /*;transmit enable (sets TXIF interrupt bit)*/ \
	| IIF(DONT_CARE, 1<<CSRC, 0)  /*;clock source select; internal for sync mode (master); don't care in async mode; Errata for other devices says to set this even though it's a don't care*/ \
	| IIF(FALSE, 1<<SENDB, 0)  /*;don't send break on next char (async mode only)*/ \
	| IIF(FALSE, 1<<TX9D, 0)  /*;clear 9th bit*/ \
)


//;Receive Status and Control (RCSTA) register (page 90, 100):
//;This register will be set at startup, but individual bits may be turned off/on again later to reset the UART
#define MY_RCSTA  \
(0  /*;default to all bits off, then turn on as needed*/ \
	| IIF(TRUE, 1<<SPEN, 0)  /*;serial port enabled*/ \
	| IIF(FALSE, 1<<RX9, 0)  /*;9 bit receive disabled (no parity)*/ \
	| IIF(DONT_CARE, 1<<SREN, 0)  /*;single receive enable; don't care in async mode*/ \
	| IIF(TRUE, 1<<CREN, 0)  /*;continuous receive enabled*/ \
	| IIF(DONT_CARE, 1<<ADDEN, 0)  /*;address detect disabled (not used for async mode)*/ \
	| IIF(FALSE, 1<<RX9D, 0)  /*;clear 9th bit*/ \
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
#define BRSCALER  (IIF(MY_BAUDCTL & (1<<BRG16), 1, 4) * IIF(MY_TXSTA & (1<<BRGH), 1, 4))  //;denominator portion that depends on BRGH + BRG16
#define SPBRG_Preset(clock, baud)  (rdiv(InstrPerSec(clock)/BRSCALER, baud) - 1)  //;s/w programmable baud rate generator value
#define BAUD_ERRPCT(clock_ignored, baud)  (ABS((100 * BRG_MULT) / ((baud)/100) - 10000 * (BRG_MULT / (baud))) / (BRG_MULT / (baud)))
//kludge: avoid macro body length errors:
#if InstrPerSec(CLOCK_FREQ/BRSCALER) == 8 MHz
 #define BRG_MULT  8000000
#else
#if InstrPerSec(CLOCK_FREQ/BRSCALER) == 5 MHz
 #define BRG_MULT  5000000
#else
#if InstrPerSec(CLOCK_FREQ/BRSCALER) == 4608000 //4.6 MIPS (18.432 MHz clock)
 #define BRG_MULT  4608000
#else
 #define BRG_MULT  InstrPerSec(CLOCK_FREQ/BRSCALER)
#endif
#endif
#endif

#if (BRSCALER == 1) && (DEVICE == PIC16F688) //high baud rates
// #ifdef PIC16F688
 #if HIGHBAUD_OK  //;only set this if you are using 16F688 rev A4 or later
  #warning "[CAUTION] 16F688.A3 errata says NOT to use BRG16 and BRGH together: only safe on A4 and later silicon"  //;paranoid self-check
 #else //HIGHBAUD_OK
  #error "[ERROR] 16F688.A3 errata says NOT to use BRG16 and BRGH together: only safe on A4 and later silicon"  //;paranoid self-check
 #endif //HIGHBAUD_OK
// #endif //_PIC16F688
#endif //BRSCALER == 1


#if MAX_BAUD == 500 KBAUD
 #define MAX_BAUD_tostr  500 kbaud
#else
#if MAX_BAUD == 250 KBAUD
 #define MAX_BAUD_tostr  250 kbaud
#else
#if MAX_BAUD == 115.2 KBAUD
 #define MAX_BAUD_tostr  115 kbaud
#else
#if MAX_BAUD == 57.6 KBAUD
 #define MAX_BAUD_tostr  57 kbaud
#else
 #define MAX_BAUD_tostr  MAX_BAUD
#endif
#endif
#endif
#endif
 
#if BRSCALER == 1
 #define BRSCALER_tostr  1
#else
#if BRSCALER == 4
 #define BRSCALER_tostr  4
#else
#if BRSCALER == 16
 #define BRSCALER_tostr  16
#endif
#endif
#endif

//57k = 138 @32 MHz (.08% err), 86 @20 MHz (.22% err), 79 @18.432 MHz (0% err); send pad char 1x/400 bytes @20 MHz
//115k = 68 @32 MHz (.64% err), 42 @20 MHz (.94% err), 39 @18.432 MHz (0% err); send pad char 1x/100 bytes @20 MHz
//250k = 31 @32 MHz (0% err), 19 @20 MHz (0% err), 17 @18.432 MHz (2.34% err); send pad char 1x/40 bytes @18.432MHz

//#if MaxBaudRate(CLOCK_FREQ) < BAUD_RATE * 95/100 //;desired baud rate is not attainable (accurately), down-grade it
#if BAUD_ERRPCT(CLOCK_FREQ, MAX_BAUD) < 1 //250k baud @32 or 20 MHz, or 57k/115k @18.432 MHz
 #define BAUD_ERRPCT_tostr  < .01 //exact
#else
#if BAUD_ERRPCT(CLOCK_FREQ, MAX_BAUD) == 8 //57k baud @32 MHz
 #define BAUD_ERRPCT_tostr  .08
#else
#if BAUD_ERRPCT(CLOCK_FREQ, MAX_BAUD) == 22 //57k baud @20 MHz
 #define BAUD_ERRPCT_tostr  .22
#else
#if BAUD_ERRPCT(CLOCK_FREQ, MAX_BAUD) == 64 //115k baud @32 MHz
 #define BAUD_ERRPCT_tostr  .64
#else
#if BAUD_ERRPCT(CLOCK_FREQ, MAX_BAUD) == 94 //115k baud @20 MHz
 #define BAUD_ERRPCT_tostr  .94
#else
#if BAUD_ERRPCT(CLOCK_FREQ, MAX_BAUD) == 240 //234 //250k baud @18.432 MHz; kludge: arithmetic errors
 #define BAUD_ERRPCT_tostr  2.34
#else
 #define BAUD_ERRPCT_tostr  BAUD_ERRPCT(CLOCK_FREQ, MAX_BAUD)
#endif
#endif
#endif
#endif
#endif
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
volatile bit IsCharAvailable @adrsof(pir1).RCIF;
//volatile bit IsRoomAvailable @PIR1_ADDR.TXIF;
//volatile bit HasInbuf @PIR1_ADDR.RCIF; //char received
volatile bit HasFramingError @adrsof(rcsta).FERR; //serial input frame error; input char is junk
volatile bit HasOverrunError @adrsof(rcsta).OERR; //serial input buffer overflow; input char is preserved
#define HasAnyError  (rcsta & ((1<<FERR) | (1<<OERR)) //CAUTION: must check Frame error before read rcreg, Overrun error after
//volatile bit HasOutbuf @PIR1_ADDR.TXIF; //space available in outbuf


//global state:
volatile uint8 ioerrs @adrsof(IOERRS); //keep track of frame/overrun errors; assume infrequent and use a small count to conserve memory
volatile uint3x8 iochars @adrsof(IOCHARS); //keep track of serial port activity (mainly for debug); 24-bit count @250k baud sustained ~= 16M/25K ~= 11 minutes
volatile bit WantEcho @BITADDR(adrsof(WANT_ECHO)); ///8.(adrsof(PROTOCOL_LISTEN) % 8); //initial 2 sec interval has passed
//volatile bit SerialWaiting @BITADRS(adrsof(statebit_0x20)); //serial input waiting; filters out framing errors to avoid sending junk to downstream controllers
//volatile bit NoSerialWaiting @adrsof(STATUS).Z; //status.Z => no serial data waiting
//#define IsSerialWaiting(value)  (NoSerialWaiting == !(value))

#define stats_init()  //root of stats init function chain


//initialize I/O stats:
inline void iostats_init(void)
{
//	ONPAGE(0); //1); //page 1 full, so put it in page 0
	stats_init(); //prev init first

	ioerrs = 0;
	zero24(iochars.as24);
}
#undef stats_init
#define stats_init()  iostats_init() //initialization wedge for compilers that don't support static init


//turn serial port off & on:
//This is used after a BRG change or OERR.
//inline void Toggle_serio()
//	setbit(rcsta, SPEN, FALSE);  //;turn off receive to reset FERR (page 92)
//	setbit(rcsta, SPEN, TRUE);  //;turn on receive again
//	setbit(rcsta, CREN, FALSE); //turn off to reset OERR (page 92)
//	setbit(rcsta, CREN, TRUE); //re-enable receive
#define Toggle_serio(ctlbit)  \
{ \
	rcsta.ctlbit = FALSE; \
	rcsta.ctlbit = TRUE; \
}


//flush serial in port:
//;http://www.pic101.com/mcgahee/picuart.zip says to do this 3x; 2 are probably for the input buffer and 1 for an in-progress char
inline void Flush_serin(void)
{
	WREG = rcreg;
	WREG = rcreg;
	WREG = rcreg;
}


//try to clear frame/overrun errors:
//overrun errors would be caused by bugs (not polling rcif frequently enough), so assume those won't happen 
//frame errors could be due to a variety of environmental or config issues, so check those more carefully
non_inline void SerialErrorReset(void)
{
	ONPAGE(PROTOCOL_PAGE); //put on same page as protocol handler to reduce page selects

//	WREG = rcreg;
//	SerialWaiting = FALSE;
//	if (!HasFramingError && !HasOverrunError) return; //no errors
	inc_nowrap(ioerrs);
	if (!HasOverrunError) //try to clear individual frame error; only way to clear overrun error is to reset serial port
	{
		WREG = rcreg; if (!HasFramingError) return; //clear FERR on first char in FIFO
		WREG = rcreg; if (!HasFramingError) return; //clear next FERR; frame error was successfully cleared
	}
	Toggle_serio(SPEN); //if all chars in FIFO have FERRs, this is the only way to clear it (page 92); NOTE: CREN will not clear FERR; SPEN or CREN will clear OERR
//	GetChar_WREG(); //this will clear FERR but not OERR (page 91)
	Flush_serin(); //FERR will clear as chars are read; this will also clear pir1.RCIF; is this needed?
//	NoSerialWaiting = TRUE; //clear char-available flag for SerialCheck
}


//check if serial data is waiting:
//serial data is infrequent (40 usec @250 kbaud, 87 usec @115 kbaud, 174 usec @57 kbaud), so this is optimized for the case of no serial data waiting
//frame errors occur if RS485 receiver line is open (EUSART sees low and starts async rcv process), so frame errors can be filtered out to avoid false interrupts to timers and demo sequence playback
//otherwise, treat frame/overrun errors as input so caller can take the appropriate action
//if there's no char available, then there shouldn't be frame or overrun errors to check either, so first check char available before other checks
#define RECEIVE_FRAMERRS  FALSE //TRUE //protocol handler (auto baud detect) wants to know about frame errors
#define FIXUP_FRAMERRS  TRUE //FALSE //protocol handler does not want frame errors, reset serial port and return to caller
#define SerialCheck(ignore_frerrs, uponserial)  \
{ \
	/*WREG = pir1 & (1<<RCIF)*/; /*status.Z => no char available; 2 instr + banksel; NOTE: trashes WREG*/ \
	if (IsCharAvailable /*!NoSerialWaiting*/) \
	{ \
		if (ignore_frerrs && HasFramingError) SerialErrorReset(); /*NoSerialWaiting = TRUE*/ /*ignore frame errors so open RS485 line doesn't interrupt demo sequence*/ \
		else uponserial; \
	} \
}
non_inline void protocol(void); //fwd ref
//handle frame errors in protocol() to reduce caller code space
#define BusyPassthru(want_frerrs, ignore)  SerialCheck(/*FIXUP_FRAMERRS*/ /*RECEIVE_FRAMERRS*/ want_frerrs, protocol()) //nextpkt(WaitForSync)) //{ if (IsCharAvailable) protocol(where); } //EchoChar(); } //don't process any special chars; 2 instr if false, 6+5 instr if true (optimized for false)

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
inline void GetChar_WREG(void)
{
//	ONPAGE(0);

//	while (!SerialWaiting)
	inc24(iochars.as24); //update I/O stats; count input rather than output
	WREG = rcreg; //return char to caller in WREG
}

//busy wait until char available:
//char is returned in WREG
#define WaitChar(buf)  { WaitChar_WREG(); buf = WREG; }
inline void WaitChar_WREG(void)
{
	Wait4Char(); //WANT_FRERR);
//		SerialErrorReset();
	GetChar_WREG();
}


//send byte down stream:
//assumes tx rate <= rx rate (no overruns)
#define PutChar(ch)  { WREG = ch; PutChar_WREG(); }
inline void PutChar_WREG(void)
{
//	if (!SerialWaiting) return;
	txreg = WREG; //rcreg;
//update I/O stats:
//	inc24(iochars);
//	SerialWaiting = FALSE;
}


//pass thru serial data to downstream controllers:
//NOTE: caller should only call this when a char is known to be available
//garbled chars (frame errors) are echoed
//non_inline void protocol(void); //fwd ref
//#define protocol(where)  { WREG = ~(where); protocol_WREG(); }
#if 0
non_inline void xEchoChar(void)
{
	ONPAGE(0);

//	Carry = TRUE;
//	if (HasFramingError) Carry = FALSE; //remember whether to pass along char; must check before reading char from FIFO
//	if (protocol_inactive) { protocol(); return; } //
#if 0
	bitcopy(!HasFramingError, Carry); //remember whether to pass along char; must check before reading char from FIFO
	GetChar_WREG(); //must do this to clear frame error; always do it
	if (Carry) txreg = WREG; //PutChar_WREG(); //trying to get BoostC to inline the btf here
#else
	inc24(iochars.as24); //update I/O stats; count input rather than output (some chars might not be echoed)
	if (!HasFramingError) //most common case (hopefully)
	{
//		GetChar_WREG(); //must do this to clear frame error; always do it
		WREG = rcreg; //return char to caller in WREG
		if (WantEcho) txreg = WREG; //PutChar_WREG(); //trying to get BoostC to inline the btf here
		return;
	}
	SerialErrorReset();
//	GetChar_WREG();
//	WREG = rcreg; //return char to caller in WREG; NO - it's junk
#endif
}
#endif


//update baud rate:
//used to recover during baud rate auto-detect
#define AdjustBaud(brate)  { WREG = SPBRG_Preset(CLOCK_FREQ, brate); AdjustBaud_WREG(); }
non_inline void AdjustBaud_WREG(void)
{
	ONPAGE(PROTOCOL_PAGE); //put on same page as protocol handler to reduce page selects

//	MOVWF(prevbaud); //remember currently selected baud rate in case we need to reinitialize serial port due to comm errors
//	prevbaud = spbrgl; //remember currently selected baud rate in case we need to reinitialize serial port later due to comm errors
	spbrgL = WREG; //assume upper byte won't change (target values are all small values)
//;baud rate change with int osc seems to need to reset the serial port?? (ext clock doesn't seem to need this)
	Toggle_serio(SPEN);
	Flush_serin(); //anything already received is junk, so flush it
}


//;initialize serial port:
//;BRG is set to highest available for the given current clock freq.
//Protocol handler will reduce the baud rate as needed.
//Called from 2 places (main init and get_sync), so don't inline it.
inline void serial_init(void)
{
//	ONPAGE(0); //1); //page 1 full, so put it in page 0
	init(); //prev init first

//	if (bitof(rcsta, SPEN)) return; //already initialized
	baudcon = MY_BAUDCTL; //should be 0x08
//;changing baud rate after FSCM kicks in doesn't seem to work, so just set it once here at the start:
//;We already know by now whether or not the ext clock is working anyway (can't get here on ext clock if it's not working).
	rcsta.SPEN = FALSE; //;disable serial port; some notes say this is required when changing the baud rate
//;NOTE: need to set SPBRG a while before enabling interrupts so SPBRG can stabilize
//;http://www.pic101.com/mcgahee/picuart.zip also recommends a startup delay so MAX232 charge pump stabilizes,
//; but it has probably already stabilized by the time we get to this code.
//#if IsDualClock 
//check if ext clock is working before setting baud rate (affects BRG timing):
//	if (serinits) spbrgl = prevbaud; //use previously selected baud rate; don't need to auto-detect again; NOTE: only lower byte can change
//#if (PLL == 1) //&& IsDualClock
//	if (UseIntOsc) osccon.SCS = TRUE; //;NOTE: FCM does not set this, so set it manually (page 31); serial port seems to be messed up without this!
//#endif
//#if MaxBaudRate(CLOCK_FREQ) < BAUD_RATE * 95/100 //;desired baud rate is not attainable (accurately), down-grade it
#if BAUD_ERRPCT(CLOCK_FREQ, MAX_BAUD) < 250 //2.5%; units are .01%
 #warning "[INFO] Baud rate "MAX_BAUD_tostr" probably reliable with "CLOCK_FREQ_tostr" MHz ext clock, BR scalar "BRSCALER_tostr" (" BAUD_ERRPCT_tostr "% error)."
//  #define INITIAL_BAUD_RATE  BAUD_RATE
#else
 #warning "[INFO] Baud rate "MAX_BAUD_tostr" not reliable with "CLOCK_FREQ_tostr" MHz ext clock, BR scalar "BRSCALER_tostr" (" BAUD_ERRPCT_tostr "% error)."
//  #define INITIAL_BAUD_RATE  BAUD_RATE_STEPDOWN(BAUD_RATE) /*;desired baud rate is not attainable (accurately), down-grade it*/
#endif
//57k = 138 @32 MHz (.08% err), 86 @20 MHz (.22% err), 79 @18.432 MHz (0% err); send pad char 1x/400 bytes @20 MHz?
//115k = 68 @32 MHz (.64% err), 42 @20 MHz (.94% err), 39 @18.432 MHz (0% err); send pad char 1x/100 bytes @20 MHz?
//230k = 34? @32 MHz (?% err), ? @20 MHz (?% err), 19 @18.432 MHz (0% err); send pad char 1x/100 bytes @20 MHz?
//460k = 17? @32 MHz (?% err), ? @20 MHz (?% err), 9 @18.432 MHz (0% err); send pad char 1x/100 bytes @20 MHz?
//-------
//250k = 31 @32 MHz (0% err), 19 @20 MHz (0% err), 17 @18.432 MHz (2.4% err); send pad char 1x/40 bytes @18.432MHz?
//500k = 15 @32 MHz (0% err), 9 @20 MHz (0% err), 8 @18.432 MHz (2.4% err); send pad char 1x/40 bytes @18.432MHz?
//NOTE: 5 MIPS + 8 MIPS PICs have slightly different baud rates (230400 vs 250 kbaud), which seems to cause comm errors for large data
//if the comm port can use non-standard baud rates (ie, FTDI allows this), then this mismatch can be significantly reduced by using an intermediate baud rate of 242500
//decrease SPBRG for 5 MIPS and increase it for 8 MIPS to give: 18.432MHz /4/242500 - 1 == 18, 32 MHz /4/242500 - 1 == 32, 0.4% error rate in both cases
//vs. 8MHz /4/250000 - 1 = 7 if !ext clock on 5 MIPS PIC
//10/23/14: this gives 242526 baud @ 18.432MHz, 242424 baud @ 32 MHz => .04% error => maybe need 1 pad char / frame @ 20 fps (5 msec): 1212 bytes
#if (CLOCK_FREQ == 32 MHz) || (CLOCK_FREQ == 18432000)
 #define BAUD_SKEW  242500
 #warning "[INFO] Using skewed baud rate for more reliable comm between PIC families: "BAUD_SKEW""
#else
 #define BAUD_SKEW  MAX_BAUD
#endif
	spbrgL = SPBRG_Preset(CLOCK_FREQ, BAUD_SKEW); //MAX_BAUD); //will go do some other stuff after this, so don't need to wait for brg to stabilize
	spbrgH = SPBRG_Preset(CLOCK_FREQ, BAUD_SKEW) / 0x100;
//#if byteof(SPBRG_Preset(EXT_CLK_FREQ * PLL, INITIAL_BAUD_RATE), 1)
// #error code below assumes spbrgh == 0
//#endif
	if (ExtClockFailed) spbrgL = SPBRG_Preset(MAXINTOSC_FREQ, BAUD_SKEW); //use alternate timing value; 5 MIPS PIC @8MHz internal will use 250k baud with higher error rate
//#undef INITIAL_BAUD_RATE //not useful after this point
//	prevbaud = spbrgl; //remember currently selected baud rate in case we need to reinitialize serial port due to comm errors (lower byte only)
//	inc_nowrap(serinits); //keep track of re-inits
//;turn serial port off/on after baud rate change:
	txsta = MY_TXSTA; //should be 0x24 ;set TXEN (enable xmiter) and reset SYNC (set async mode) after setting SPBRG
	rcsta = MY_RCSTA; //should be 0x90 ;set SPEN (enable UART and set TX pin for output + RX pin for input) and set CREN (continuous rcv) to enable rcv
//;	setbit TXSTA, TXEN, TRUE  ;enable TX after RX is set
	Flush_serin(); //is this needed?
}
#undef init
#define init()  serial_init() //initialization wedge for compilers that don't support static init


//;==================================================================================================
//;RGB definitions and helpers
//;==================================================================================================

//#define RGB(r, g, b) ((0x10000 * (r) + 0x100 * (g) + (b))
#define RGB(r, g, b)  ((((r) & 0xFF) * 0x10000) | (((g) & 0xFF) * 0x100) | ((b) & 0xFF))
#define R(rgb)  (((rgb) / 0x10000) & 0xFF)
//#define Rhi(rgb)  (((rgb) / 0x100000) & 0x0F)
//#define Rlo(rgb)  (((rgb) / 0x10000) & 0x0F)
#define G(rgb)  (((rgb) / 0x100) & 0xFF) //(((rgb) & 0xFF00) / 0x100)
//#define Ghi(rgb)  (((rgb) / 0x1000) & 0x0F)
//#define Glo(rgb)  (((rgb) / 0x100) & 0x0F)
#define B(rgb)  ((rgb) & 0xFF)
//#define Bhi(rgb)  (((rgb) / 0x10) & 0x0F)
//#define Blo(rgb)  ((rgb) & 0x0F)
//useful for fade/ramp/gradient between 2 colors:
#define RGB_BLEND(start, end, blend)  RGB(blend * R(start) / 255, + (255 - blend) * R(end) / 255, blend * G(start) / 255, + (255 - blend) * G(end) / 255, blend * B(start) / 255, + (255 - blend) * B(end) / 255)

//pack an RGB value into prog space:
//GGGG RRRR RRRR, GGGG BBBB BBBB
//#define RGB2PROG(rgb)  (nibbleof(G(rgb), 0) << 8) | R(rgb), (nibbleof(G(rgb), 1) << 8) | B(rgb)

//palette entry:
//#if NODE_TYPE == WS2811 //kludge: R and G swapped WRT datasheet
// #warning "[CAUTION] Some WS2811 (strips) have red and green swapped"
//#endif
typedef struct
{
//	uint8 G8, R8, B8;
//#else
	uint8 R8, G8, B8;
//#endif
} rgb;
typedef struct
{
	uint8 I8; //intensity
	uint8 B4G4, R4Z4; //blue, green, red nibblea
} ibgrz;

//allow static addressing of palette (mainly for hard-coded debug or demo mode, or lower overhead during node I/O):
volatile rgb palette[1<<4 BPP] @MakeBanked(PALETTE_ADDR(0, 4 BPP));
volatile ibgrz gece_palette[1<<4 BPP] @MakeBanked(PALETTE_ADDR(0, 4 BPP));
volatile uint8 palette_bytes[3<<4 BPP] @MakeBanked(PALETTE_ADDR(0, 4 BPP));
volatile uint2x8 indir_palette[1<<4 BPP] @MakeBanked(INDIR_PALETTE_ADDR(0, 4 BPP));
//statically addressed first node pair (for demo only):
//NOTE: nodes are in descreasing address order
//uint8 nodes[GPRLEN] @NODE_ADDR(0, 4 BPP) /*+ 16*3*/; //node data at start
//uint8 first_node @Linear2Banked(TOTAL_BANKRAM - Nodes2Bytes(0 + 1, 4 BPP));
volatile uint8 first_node @MakeBanked(NODE_ADDR(0, 4 BPP)); //Linear2Banked(TOTAL_BANKRAM - Nodes2Bytes(0 + 1, 4 BPP));
volatile uint8 third_node_grp @MakeBanked(NODE_ADDR(16, 4 BPP)); //Linear2Banked(TOTAL_BANKRAM - Nodes2Bytes(0 + 1, 4 BPP));


#define DimmingCurve(brightness)  \
{ \
	WREG = brightness; \
	/*if ((brightness) && ((brightness) != 255)) might as well use these 4 - 6 instr to just go ahead and call dimming curve*/ ApplyDimmingCurve_WREG(); /*on + off stay as-is*/ \
}

#ifdef DIMMING_CURVE //NOTE: used only for hard-coded demo/test patterns; for live shows, this should be done by plug-in so it can vary by node type
//we see expontential brightness increase:
//0 = off, 1 = very dim, 2 = +100%, 3 = +50%, 4 = +33%, 5 = +25%, 6 = +20%, 7 = +17%, 8 = +14%, 9 = +13%, 10 = +11%, 11 = +10%, ... 101 = +1%  !noticeable after that?
//dimming curve doesn't actually help with lower values (limited by pixel color depth), and dithering is not feasible when all pixels are in one series string (long refresh time) and difficult on an 5 or 8 MIPS PIC
//to compensate, wait times should be adjusted for smoother ramp/fade; however, chase and other patterns are simpler with fixed wait times
//so, back to using a dimming curve to compensate by smoothing out the preceived dimming
//1% change in brightness is not noticeable; try 5%, so use (working backwards): , 190, 208, 219, 230, 242, 255
//for ideal dimming curve, want [0] to be 0 (off), [1] to be minimum detectable brightness, [2] .. [n] to look like equal steps with [n] being max brightness
 non_inline void ApplyDimmingCurve_WREG(void)
 {
	ONPAGE(DEMO_PAGE); //put on same page as demo code to reduce page selects
	
#if FALSE //TOTAL_ROM > 4K //enough space for a large table
	INGOTOC(TRUE); //#warning "CAUTION: pclath<2:0> must be correct here"
//TODO: change to pcl = WREG and put on prev page
	pcl += WREG;
	PROGPAD(0); //jump table base address
//256 steps with [0] == 0, [1] == 1, [255] == 255:
	RETLW(0); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); //0..15
	RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); //16..31
	RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); RETLW(1); //32..47
	RETLW(2); RETLW(2); RETLW(2); RETLW(2); RETLW(2); RETLW(2); RETLW(2); RETLW(2); RETLW(2); RETLW(2); RETLW(2); RETLW(2); RETLW(2); RETLW(2); RETLW(2); RETLW(2); //48..63
	RETLW(2); RETLW(2); RETLW(2); RETLW(2); RETLW(2); RETLW(3); RETLW(3); RETLW(3); RETLW(3); RETLW(3); RETLW(3); RETLW(3); RETLW(3); RETLW(3); RETLW(3); RETLW(3); //64..79
	RETLW(3); RETLW(3); RETLW(3); RETLW(4); RETLW(4); RETLW(4); RETLW(4); RETLW(4); RETLW(4); RETLW(4); RETLW(4); RETLW(4); RETLW(4); RETLW(5); RETLW(5); RETLW(5); //80..95
	RETLW(5); RETLW(5); RETLW(5); RETLW(5); RETLW(5); RETLW(6); RETLW(6); RETLW(6); RETLW(6); RETLW(6); RETLW(6); RETLW(6); RETLW(7); RETLW(7); RETLW(7); RETLW(7); //96..111
	RETLW(7); RETLW(7); RETLW(8); RETLW(8); RETLW(8); RETLW(8); RETLW(8); RETLW(9); RETLW(9); RETLW(9); RETLW(9); RETLW(10); RETLW(10); RETLW(10); RETLW(10); RETLW(11); //112..127
	RETLW(11); RETLW(11); RETLW(11); RETLW(12); RETLW(12); RETLW(12); RETLW(13); RETLW(13); RETLW(13); RETLW(14); RETLW(14); RETLW(14); RETLW(15); RETLW(15); RETLW(15); RETLW(16); //128..143
	RETLW(16); RETLW(17); RETLW(17); RETLW(17); RETLW(18); RETLW(18); RETLW(19); RETLW(19); RETLW(20); RETLW(20); RETLW(21); RETLW(21); RETLW(22); RETLW(22); RETLW(23); RETLW(23); //144..159
	RETLW(24); RETLW(25); RETLW(25); RETLW(26); RETLW(26); RETLW(27); RETLW(28); RETLW(29); RETLW(29); RETLW(30); RETLW(31); RETLW(32); RETLW(32); RETLW(33); RETLW(34); RETLW(35); //160..175
	RETLW(36); RETLW(37); RETLW(38); RETLW(38); RETLW(39); RETLW(40); RETLW(41); RETLW(42); RETLW(44); RETLW(45); RETLW(46); RETLW(47); RETLW(48); RETLW(49); RETLW(51); RETLW(52); //176..191
	RETLW(53); RETLW(54); RETLW(56); RETLW(57); RETLW(59); RETLW(60); RETLW(62); RETLW(63); RETLW(65); RETLW(66); RETLW(68); RETLW(70); RETLW(72); RETLW(73); RETLW(75); RETLW(77); //192..207
	RETLW(79); RETLW(81); RETLW(83); RETLW(85); RETLW(87); RETLW(90); RETLW(92); RETLW(94); RETLW(97); RETLW(99); RETLW(102); RETLW(104); RETLW(107); RETLW(109); RETLW(112); RETLW(115); //208..223
	RETLW(118); RETLW(121); RETLW(124); RETLW(127); RETLW(130); RETLW(133); RETLW(137); RETLW(140); RETLW(144); RETLW(147); RETLW(151); RETLW(155); RETLW(159); RETLW(163); RETLW(167); RETLW(171); //224..239
	RETLW(176); RETLW(180); RETLW(185); RETLW(189); RETLW(194); RETLW(199); RETLW(204); RETLW(209); RETLW(214); RETLW(220); RETLW(225); RETLW(231); RETLW(237); RETLW(243); RETLW(249); RETLW(255); //240..255
	PROGPAD(256); //verify correct number of entries
	INGOTOC(FALSE); //check jump table doesn't span pages
#else //approximation of above; takes less code space but more instr cycles
//keep 0 + 255 as-is, and then for each group of 32 values use graduated ranges as follows:
// 0x00..0x0f: steps of 1/32 => 32/32 => 0..1
// 0x20..0x3f: steps of 1/16 => 32/16 => 2..3
// 0x40..0x5f: steps of 1/8 => 32/8 => 4..7
// 0x60..0x7f: steps of 1/4 => 32/4 => 8..15 = 0x08..0x0f
// 0x80..0x9f: steps of 1/2 => 32/2 => 16..31 = 0x10..0x1f
// 0xa0..0xbf: steps of 1 => 32*1 => 32..63 = 0x20..0x3f
// 0xc0..0xdf: steps of 2 => 32*2 => 64..127 = 0x40..0x7f
// 0xe0..0xff: steps of 4 => 32*4 => 128..255 = 0x80..0xff
	uint8 dimval @adrsof(LEAF_PROC);

	dimval = WREG; swap(dimval); WREG &= 0x1F; WREG |= 0x20; //save group index then generate offset within group of 32 values
	xchg_WREG(dimval); WREG &= 0xE; //top 3 bits of dim value * 2
	Carry = FALSE; //clear it in case movf substituted for addwf on pcl below
	INGOTOC(TRUE); //#warning "CAUTION: pclath<2:0> must be correct here"
	pcl += WREG; //NOTE: Carry == FALSE not necessarily here
	PROGPAD(0); //jump table base address
	JMPTBL16(0x00>>4) { WREG = dimval; goto is_non_zero; } //if (!EqualsZ) WREG = 1; return; ///32, round up
small_swap:
	JMPTBL16(0x20>>4) { swap_WREG(dimval); goto low_nibble; } //WREG &= 0x1F>>4; WREG += 2; return; ///16
	JMPTBL16(0x40>>4) { rl_nc(dimval); goto small_swap; } //doesn't fit in 2 instr; chain with prev jump table entry; ;;;;swap(dimval); r1_nc_WREG(dimval); WREG &= 0x1F>>3; WREG += 4; return; ///8 == *2/16; CAUTION: Carry off from above
	JMPTBL16(0x60>>4) rr_nc(dimval); Carry = FALSE; //kludge: doesn't fit in 2 instr; chained with next jump table entry /*rr_nc_WREG(dimval); WREG &= 0x1F>>2; WREG += 8; return; ///4
	JMPTBL16(0x80>>4) { rr_nc_WREG(dimval); /*WREG &= 0x1F>>1; WREG += 0x10*/; return; } ///2; NOTE: Carry was off from above
	JMPTBL16(0xa0>>4) { WREG = dimval; /*WREG &= 0x1F; WREG += 0x20*/; return; } //*1
	JMPTBL16(0xc0>>4) { rl_nc_WREG(dimval); /*WREG &= 0x1F<<1; WREG += 0x40*/; return; } //*2; NOTE: Carry was off from above
	JMPTBL16(0xe0>>4) { rl_nc(dimval); rl_nc_WREG(dimval); return; } //NOTE: last table entry can be longer ;;;;WREG &= 0x1F<<2; WREG += 0x80; return; //*4
	INGOTOC(FALSE); //check jump table doesn't span pages
//more_0x40:
//	swap_WREG(dimval);
is_non_zero:
	WREG &= ~0x20;
	if (!EqualsZ) RETLW(1);
	//fall thru
low_nibble:
	WREG &= 0x0F; return;
#endif //TOTAL_ROM > 4K
 }
#else //no dimming curve
 inline void ApplyDimmingCurve_WREG(void) {}
#endif


#define AddPalette(color)  \
{ \
	DimmingCurve(R(color)); /*WREG2indf(fsrL)*/ indf_autopal = WREG; \
	if (G(color) != R(color)) DimmingCurve(G(color)); /*WREG2indf(fsrL)*/ indf_autopal = WREG; \
	if (B(color) != G(color)) DimmingCurve(B(color)); /*WREG2indf(fsrL)*/ indf_autopal = WREG; \
}
#define IsGray(val) ((val) && ((val) != 255))
#define SetPalette(palinx, color)  \
{ \
	if (!IsGray(R(color)) && !IsGray(G(color)) && !IsGray(B(color))) \
	{ \
		palette[palinx].R8 = 0; \
		if (R(color) != 0) --palette[palinx].R8; /*CAUTION: BoostC broken; need explicit compare to 0 here*/ \
		palette[palinx].G8 = 0; \
		if (G(color) != 0) --palette[palinx].G8; \
		palette[palinx].B8 = 0; \
		if (B(color) != 0) --palette[palinx].B8; \
	} \
	else \
	{ \
		DimmingCurve(R(color)); palette[palinx].R8 = WREG; \
		if (G(color) != R(color)) DimmingCurve(G(color)); palette[palinx].G8 = WREG; \
		if (B(color) != G(color)) DimmingCurve(B(color)); palette[palinx].B8 = WREG; \
	} \
}
//#define SetParallelPalette(palinx, color1, color2, color3, color4)  \
//{ \
//}

//set up a ptr to a palette entry:
//lower 4 bits of WREG = palette entry#
//5 instr
//inline void palptr(uint8& reg)
//{
//	WREG &= 0x0F; reg = WREG; reg += WREG; WREG += PALETTE_ADDR(0, 4 BPP); reg += WREG;
//}

//convert palette entry# to palette offset:
//assumes reg and WREG already have palette entry# ("reg = WREG" would typically preceed this function)
inline void palinx2ofs_WREG(uint8& reg) //4 instr
{
//	WREG = reg;
	reg += WREG; WREG += PALETTE_ADDR(0, 4 BPP); reg += WREG; //start offset + 3 * entry#
}


//set fsr to specified palette entry:
#define GetPalent(palinx)  { WREG = palinx; GetPalent_WREG(); }
non_inline void GetPalent_WREG(/*uint8& WREG*/ void)
{
	ONPAGE(DEMO_PAGE); //put on same page as demo code to reduce page selects

	WREG &= 0xF; //assume 4 bpp
	fsrL = WREG;
//	fsrL += WREG; if (PALETTE_ADDR(0, 4 BPP)) WREG += PALETTE_ADDR(0, 4 BPP); fsrL += WREG; //3 * palette entry# + start offset
	palinx2ofs_WREG(fsrL); fsrH = PALETTE_ADDR(0, 4 BPP) / 0x100;
}


//	  Linear2Banked(TOTAL_BANKRAM - Nodes2Bytes((node) + 1, bpp)) //node data follows palette (can use spare palette entries), but begins at end of GPR in last bank and works backward; this allows both to start at fixed addresses, but space can be traded off between them
//set a node# known at compile time (allows compiler to use direct addressing):
//NOTE: assumes palette entry is already set
#define SetNode(nodeofs, palinx, bpp)  \
{ \
	volatile uint8 pair @MakeBanked(NODE_ADDR(nodeofs, bpp)); /*static address of target node pair; NOTE: compiler will generate bank selects*/ \
	if ((nodeofs) & 1) { pair &= 0xF0; pair |= (palinx) & 0x0F; } /*lower nibble*/ \
	else { pair &= 0xF; pair |= (palinx) << 4; } /*upper nibble*/ \
}


//primary colors:
//non-BLACK values used only for demo/test pattern
#define BLACK  0x000000 //RGB(0, 0, 0)
#define BLUE  0x0000FF //RGB(0, 0, 255)
#warning "[CAUTION] BoostC compiler arithmetic broken, using hard-coded RGB values"
//#if NODE_TYPE == WS2811 //kludge: R and G swapped on datasheet
// #define GREEN  0xFF0000 //RGB(0, 255, 0) //0x00FF00 //NOTE: BoostC arithmetic broken on 16- and 32-bit consts
// #define CYAN  0xFF00FF //RGB(0, 255, 255)
// #define RED  0x00FF00 //RGB(255, 0, 0)
// #define MAGENTA  0x00FFFF //RGB(255, 0, 255)
//#else //normal order
 #define GREEN  0x00FF00 //RGB(0, 255, 0) //0x00FF00 //NOTE: BoostC arithmetic broken on 16- and 32-bit consts
 #define CYAN  0x00FFFF //RGB(0, 255, 255)
 #define RED  0xFF0000 //RGB(255, 0, 0)
 #define MAGENTA  0xFF00FF //RGB(255, 0, 255)
//#endif
#define YELLOW  0xFFFF00 //RGB(255, 255, 0)
#define WHITE  0xCCCCCC //0xFFFFFF //RGB(255, 255, 255) 80% brightness
//gray scale (use ~ 33% to match single-color brightness):
#define GRAY(factor)  RGB(255 * factor, 255 * factor, 255 * factor) //NOTE: subexpr not cumutative (factor will be converted to int 0 if placed first)

//#define RGB2IBGR(rgb)  MAX(RGB2R(rgb), RGB2G(rgb), RGB2B(rgb))
#define RGB2IBGR(color)  GECE_##color //kludge: BoostC compiler problem
//pre-define GECE_converted colors to compensate for broken compiler:
//TODO: check if safe to run non-white colors at full brightness
#define GECE_RED  0xCC00F0
#define GECE_GREEN  0xCC0F00
#define GECE_BLUE  0xCCF000
#define GECE_YELLOW  0xCC0FF0
#define GECE_CYAN  0xCCFF00
#define GECE_MAGENTA  0xCCF0F0
#define GECE_WHITE  0xCCFFF0

//bit pivot:
//turns 8 separate bits/bytes into one parallel byte; used for compile-time packing of parallel nodes
#if 0
#define PIVOT8(byte7, byte6, byte5, byte4, byte3, byte2, byte1, byte0, bit)  \
( \
	IIF((bype7) & (bit), 1<<7, 0) + \
	IIF((bype6) & (bit), 1<<6, 0) + \
	IIF((bype5) & (bit), 1<<5, 0) + \
	IIF((bype4) & (bit), 1<<4, 0) + \
	IIF((bype3) & (bit), 1<<3, 0) + \
	IIF((bype2) & (bit), 1<<2, 0) + \
	IIF((bype1) & (bit), 1<<1, 0) + \
	IIF((bype1) & (bit), 1<<0, 0) + \
0)
#endif


//pivot bits and arrange into 4-byte parallel palette entry:
//intended for use with constants so compiler can reduce expressions
#define SetParallelPalette(palinx, color0, color1, color2, color3)  \
{ \
	palette_bytes[4*(palinx)+0] = (((color0) & 0x80) >> (0-0)) | (((color1) & 0x80) >> (1-0)) | (((color2) & 0x80) >> (2-0)) | (((color3) & 0x80) >> (3-0)) | (((color0) & 0x40) >> (4-1)) | (((color1) & 0x40) >> (5-1)) | (((color2) & 0x40) >> (6-1)) | (((color3) & 0x40) >> (7-1)); \
	palette_bytes[4*(palinx)+1] = (((color0) & 0x20) << (0-(0-2))) | (((color1) & 0x20) << (0-(1-2))) | (((color2) & 0x20) >> (2-2)) | (((color3) & 0x20) >> (3-2)) | (((color0) & 0x10) >> (4-3)) | (((color1) & 0x10) >> (5-3)) | (((color2) & 0x10) >> (6-3)) | (((color3) & 0x10) >> (7-3)); \
	palette_bytes[4*(palinx)+2] = (((color0) & 0x08) << (0-(0-4))) | (((color1) & 0x08) << (0-(1-4))) | (((color2) & 0x08) << (0-(2-4))) | (((color3) & 0x08) << (0-(3-4))) | (((color0) & 0x04) << (0-(4-5))) | (((color1) & 0x04) >> (5-5)) | (((color2) & 0x04) >> (6-5)) | (((color3) & 0x04) >> (7-5)); \
	palette_bytes[4*(palinx)+3] = (((color0) & 0x02) << (0-(0-6))) | (((color1) & 0x02) << (0-(1-6))) | (((color2) & 0x02) << (0-(2-6))) | (((color3) & 0x02) << (0-(3-6))) | (((color0) & 0x01) << (0-(4-7))) | (((color1) & 0x01) << (0-(5-7))) | (((color2) & 0x01) << (0-(6-7))) | (((color3) & 0x01) >> (7-7)); \
}

#define CopyParallelPalette(from, to)  \
{ \
	palette_bytes[4*(to)+0] = palette_bytes[4*(from)+0]; \
	palette_bytes[4*(to)+1] = palette_bytes[4*(from)+1]; \
	palette_bytes[4*(to)+2] = palette_bytes[4*(from)+2]; \
	palette_bytes[4*(to)+3] = palette_bytes[4*(from)+3]; \
}

//pivot 3-byte RGB entry to 3 4-byte parallel palette entries:
//mainly used for test/demo
#define rgb2parallel(palinx)  { WREG = palinx; rgb2parallel_WREG(); } //convert series palette entry (3 bytes) to 3 parallel sub-palette entries (4 bytes each) + 1 indirect entry to palette triplet
non_inline void rgb2parallel_WREG(void)
{
	volatile uint8 palbyte @adrsof(DEMO_STACKFRAME3) + 1 ;//cached palette byte
	ONPAGE(DEMO_PAGE);

	WREG += 1; GetPalent(WREG); //point past end of palette entry
//	WREG &= 0xF; fsrL_noode = WREG;
//	palinx2ofs_WREG(fsrL_parapal); fsrH_parapal = PARALLEL_PALETTE_ADDR(0, 4 BPP) / 0x100;
	fsrL_parapal = PARALLEL_PALETTE_ADDR(4, 4 BPP); fsrH_parapal = PARALLEL_PALETTE_ADDR(4, 4 BPP) / 0x100;
	for (;;)
	{
//		if (fsrL == (PARALLEL_PALETTE_ADDR(4, 4 BPP) & 0xFF)) palbyte = palette[0].B8;
//		else if (fsrL == (PARALLEL_PALETTE_ADDR(3, 4 BPP) & 0xFF)) palbyte = palette[0].G8;
//		else if (fsrL == (PARALLEL_PALETTE_ADDR(2, 4 BPP) & 0xFF)) palbyte = palette[0].R8;
		if (fsrL_parapal == (PARALLEL_PALETTE_ADDR(1, 4 BPP) & 0xFF)) break;
		WREG = fsrL_parapal & 3; if (EqualsZ) palbyte = indf_autopal_reverse;
		WREG = 0; rr_nc(palbyte); if (Carry) WREG |= 0x0F; rr_nc(palbyte); if (Carry) WREG |= 0xF0; indf_parapal_reverse = WREG; //fill in reverse order so original series RGB value will not be overwritten while still needed
	}
	indir_palette[0].bytes[0] = ((1 << 10) | (2<<5) | 3) / 0x100; //set up palette entry#0 as indirection to sub-palette entries (1, 2, 3)
	indir_palette[0].bytes[1] = (1 << 10) | (2<<5) | 3; //set up palette entry#0 as indirection to sub-palette entries (1, 2, 3)
}


//;==================================================================================================
//;Low-level node I/O helpers
//;==================================================================================================

//supported node types:
//NOTE: only certain types of nodes can be supported based on uController speed
//timing is very tight, and processor utilization may be as high as 60% for an 8 MIPS PIC
//#define WS2811  1
//#define TM1809  2
//#define WS2801  3
//#define LPD6803  4
//#define GECE  5


//convert bit# to pin mask:
//takes 7 instr always, compared to a variable shift which would take 2 * #shift positions == 2 - 14 instr (not counting conditional branching)
non_inline void pin2mask_WREG(void)
 {
//	volatile uint8 entnum @adrsof(LEAF_PROC);
	ONPAGE(PROTOCOL_PAGE); //put on same page as protocol handler to reduce page selects

//return bit mask in WREG:
	INGOTOC(TRUE); //#warning "CAUTION: pclath<2:0> must be correct here"
	pcl += WREG & 7;
	PROGPAD(0); //jump table base address
	JMPTBL16(0) RETLW(0x80); //pin A4
	JMPTBL16(1) RETLW(0x40); //A2
	JMPTBL16(2) RETLW(0x20); //A1
	JMPTBL16(3) RETLW(0x10); //A0
	JMPTBL16(4) RETLW(0x08); //C3
	JMPTBL16(5) RETLW(0x04); //C2
	JMPTBL16(6) RETLW(0x02); //C1
	JMPTBL16(7) RETLW(0x01); //C0
	INGOTOC(FALSE); //check jump table doesn't span pages
#if 0 //in-line minimum would still take 7 instr:
	WREG = 0x01;
	if (reg & 2) WREG = 0x04;
	if (reg & 1) WREG += WREG;
	if (reg & 4) swap(WREG);
#endif
#ifdef PIC16X
	TRAMPOLINE(1); //kludge: compensate for address tracking bug
#endif
}


//#ifndef NODE_TYPE
// #define NODE_TYPE  0  //avoid undef symbol error
//#endif

#ifndef SERIES_PIN
 #define SERIES_PIN  0xA2 //default; convenient for 12F1840 (the only unused pin)
#endif

//#warning "1 "SERIES_PIN""
//#warning "2 "ToHex(SERIES_PIN)""
//#warning "3 "if ((ToHex(SERIES_PIN) & 0xF0) == 0xA0) && defined(PORTA)""

//try to make code independent of port#:
//#if ((/*ToHex*/(SERIES_PIN) & 0xF0) == 0xA0) && defined(PORTA)
#if (PORTOF((SERIES_PIN)) == 0xA) && defined(PORTA)
 #define portX  porta //lata
 #define portX_ADDR  PORTA_ADDR //lata_ADDR
#else
#if (PORTOF(SERIES_PIN)) == 0xB) && defined(PORTB)
 #define portX  portb //latb
 #define portX_ADDR  PORTB_ADDR //latb_ADDR
#else
#if (PORTOF(SERIES_PIN)) == 0xC) && defined(PORTC)
 #define portX  portc //portc
 #define portX_ADDR  PORTC_ADDR //latc_ADDR
#else
 #error "[ERROR] Unrecognized port: "SERIES_PIN""
#endif //port C
#endif //port B
#endif //port A


//show node capacity for this device:
#if NUM_NODES(4 BPP) == (64-16-(3<<4))*2
 #define NUM_NODES_4BPP_tostr  64
#else
#if NUM_NODES(4 BPP) == (256-16-(3<<4))*2
 #define NUM_NODES_4BPP_tostr  384
#else
#if NUM_NODES(4 BPP) == (384-16-(3<<4))*2
 #define NUM_NODES_4BPP_tostr  640
#else
#if NUM_NODES(4 BPP) == (1024-16-(3<<4))*2
 #define NUM_NODES_4BPP_tostr  1920
#else
 #define NUM_NODES_4BPP_tostr  NUM_NODES(4 BPP)
#endif
#endif
#endif
#endif

#if NUM_NODES(1 BPP) == (64-16-(3*2))*8
 #define NUM_NODES_1BPP_tostr  336
#else
#if NUM_NODES(1 BPP) == (256-16-(3*2))*8
 #define NUM_NODES_1BPP_tostr  1872
#else
#if NUM_NODES(1 BPP) == (384-16-(3*2))*8
 #define NUM_NODES_1BPP_tostr  2896
#else
#if NUM_NODES(1 BPP) == (1024-16-(3*2))*8
 #define NUM_NODES_1BPP_tostr  8016
#else
 #define NUM_NODES_1BPP_tostr  NUM_NODES(1 BPP)
#endif
#endif
#endif
#endif

//theoretical max #nodes with minimum palette:
//minimum palette size == 2 (otherwise smart nodes are not needed)
#if MAX_NODES(4 BPP) == (64-16-(3*2))*2
 #define MAX_NODES_4BPP_tostr  84
#else
#if MAX_NODES(4 BPP) == (256-16-(3*2))*2
 #define MAX_NODES_4BPP_tostr  468
#else
#if MAX_NODES(4 BPP) == (384-16-(3*2))*2
 #define MAX_NODES_4BPP_tostr  724
#else
#if MAX_NODES(4 BPP) == (1024-16-(3*2))*2
 #define MAX_NODES_4BPP_tostr  2004
#else
 #define MAX_NODES_4BPP_tostr  MAX_NODES(4 BPP)
#endif
#endif
#endif
#endif

#warning "[INFO] Total banked RAM "TOTAL_BANKRAM_tostr" bytes "RAM_SCALE_tostr" allows "NUM_NODES_4BPP_tostr" RGB nodes @4 bpp ("MAX_NODES_4BPP_tostr" max) up to "NUM_NODES_1BPP_tostr" RGB nodes @1 bpp."

//special parameter values for SEND_BIT:
//val param should be a const so compiler can expand the appropriate code at compile time (not enough bandwidth to decide at run time)
//#define INCOMPLETE  //dummy symbol for readability
//#define UNUSED8  //dummy symbol for readability
//#define UNUSED7  //dummy symbol for readability
//#define UNUSED4  //dummy symbol for readability
//#define UNUSED3  //dummy symbol for readability
#define FROMRAM  0x8000 //values vary at run time (come from RAM); can be direct or indirect but must only take 1 instr to load
#define GETBIT /*NEXTBIT*/  0x4000 //use next bit from previously loaded values
//#define VARIES  0xbeef //dummy value to force correct send_bit loop structure


//low-level send series bit:
//nodes are only connected to one I/O pin; this can be more convenient for strings/strips that are spread out or cover a larger area
//series connection results in longer refresh times, but this is okay because another thread handles serial port pass-thru during node I/O
//max string length is determined by sender's refresh rate after accounting for serial port transmission time
//NOTE: 8 bits can't just be loaded into port A and then shifted (some bits would be lost due to unimplemented I/O pins)
//NOTE: there are no LATx regs on older/slower PICs, so PORTx is used instead; CAUTION: this can lead to read-modify-write issues?
//#define SERPIN  (/*ToHex*/(SERIES_PIN) & 0xF)
#define SERPIN  PINOF(SERIES_PIN)


//WS2811 with 5 MIPS PIC:
//WS2812: "0" = usec high (0.2 - 0.5) + usec low (0.65 - 0.95), "1" = usec high (0.55 - 0.85) + usec low (0.45 - 0.75)
//high speed WS2811: "0" = 0.25 usec high (0.18 - 0.32) + 1.0 usec low (0.93 - 1.07), "1" = 0.6 usec high (0.53 - 0.67) + 0.65 usec low (0.58 - 0.72), >= 25 usec reset
//16F688 @20 MHz (5 MIPS): 200 nsec/instr, "0" = 1 instr high + 5 instr low, "1" = 3 instr high + 3 instr low => 1/2/3 instr bit cycle; TIMING WITHIN SPEC
//16F688 @18.432 MHz (4.6 MIPS): 217 nsec/instr, "0" = 1 instr high + 5 instr low (1.085 vs. 1.07 usec), "1" = 3 instr high + 3 instr low; MARGINAL TIMING (1.4% off, probably okay)
//16F688 @16 MHz (4 MIPS): 250 nsec/instr, "0" = 1 instr high + 4 instr low, "1" = 2.2 - 2.6 instr high + 2.4 - 2.8 instr low; BAD TIMING
//total bit cycle time @20 MHz: 6 instr * 200 nsec = 1.2 usec => 28.8 usec/node (24 bits)
//total bit cycle time @18.432 MHz: 6 instr * 217 nsec ~= 1.3 usec => 31.25 usec/node (24 bits)
#define SEND_SERIES_BIT_WS2811_5MIPS(val, varbit, spare3or4)  \
{ \
/*	1!	2?	3	4!	5	6	1!	etc.*/ \
	portX.SERPIN = 1; /*bit start*/ \
		/*2 spare instr MIGHT be moved to here from below; for variable bit, FIRST instr must alter portX*/ \
				if (val) SWOPCODE(0, 2); /*"1" bit transition occurs later*/ \
				portX.SERPIN = 0; /*"0" bit transition occurs earlier*/ \
		/*if ((val) && ((val) != ~0))*/ varbit; /*NOTE: for variable bits, first instr must alter portX*/ \
			spare3or4; /*3 spare instr (4 for const bit, counting varbit above)*/ \
}


//WS2811 with 8 MIPS PIC:
//high speed WS2811: "0" = 0.25 usec high (0.18 - 0.32) + 1.0 usec low (0.93 - 1.07), "1" = 0.6 usec high (0.53 - 0.67) + 0.65 usec low (0.58 - 0.72), >= 25 usec reset
//16F182X @32 MHz (8 MIPS): 125 nsec/instr, "0" = 2 instr high + 8 instr low, "1" = 5 instr high + 5 instr low => 2/3/5 instr bit cycle; TIMING WITHIN SPEC
//total bit cycle time: 10 instr * 125 nsec = 1.25 usec => 30 usec/node (24 bits)
#define SEND_SERIES_BIT_WS2811_8MIPS(val, varbit, spare7or8)  \
{ \
/*	1!	2	3?	4	5	6!	7	8	9	10	1!	etc.*/ \
	portX.SERPIN = 1; /*bit start*/ \
		/*4 spare instr MIGHT be moved to here from below; for variable bit, SECOND instr must alter portX*/ \
						SWOPCODE(0, IIF(val, 4, 1)); /*"1" bit transition occurs later*/ \
						portX.SERPIN = 0; /*"0" bit transition occurs earlier*/ \
			if ((val) && ((val) != ~0)) SWOPCODE(0, 1); /*variable bit transition occurs later*/ \
			/*if ((val) && ((val) != ~0))*/ varbit; /*NOTE: for variable bits, first instr must alter portX*/ \
				spare7or8; /*7 spare instr (8 for const bit, counting varbit above); CAUTION: portX.SERPIN = 0 might be inserted mid way*/ \
}


//clock-dependent implementation:
#if (CLOCK_FREQ == 20 MHz) || (CLOCK_FREQ == 18432000) //5 or 4.6 MIPS; NOTE: 4.6 MIPS is 1.4% out of spec with WS2811 timing, but seems to work
 #define SEND_SERIES_BIT_WS2811(comment, val, varbit, spare3or4_5mips, want_extra)  \
 { \
	PROGPAD(0); \
	SEND_SERIES_BIT_WS2811_5MIPS(val, varbit, spare3or4_5mips); \
	PROGPAD(6 want_extra); \
 }
// #warning "5 MIPS WS2811"
#endif
#if (CLOCK_FREQ == 32 MHz) //8 MIPS
 //extra: < 0 for instr that take > 1 instr cycle, > 0 to reduce nops and allow extra instr
 #define SEND_SERIES_BIT_WS2811(comment, val, varbit, spare3or4_5mips, want_extra)  \
 { \
	PROGPAD(0); \
	SEND_SERIES_BIT_WS2811_8MIPS(val, varbit, { REPEAT(4 - MAX(0 want_extra, 0), nop1()); spare3or4_5mips; }); /*throttle back to 5 MIPS so caller doesn't need to change; NOTE: spare3 might contain break, so put nop2() ahead of it*/ \
	/*if (0 want_extra > 1)*/ BANKSELECT(portX); \
	PROGPAD(MIN(10 want_extra, 10)); \
 }
//	SEND_SERIES_BIT_WS2811_8MIPS(val, { varbit; nop2_split(); }, { if (0 want_extra < 2) nop2_split(); spare3or4; }); /*throttle back to 5 MIPS so caller doesn't need to change; NOTE: spare3 might contain break, so put nop2() ahead of it*/
// #warning "8 MIPS WS2811"
#endif


//TM1809 with 5 MIPS PIC:
//high speed TM1809: "0" = 0.3 usec high (0.25 - 0.39) + 0.6 usec low (0.53 - 0.67), "1" = 0.6 usec high (0.53 - 0.67) + 0.3 usec low (0.25 - 0.39), >= 50 usec reset
//16F688 @20 MHz (5 MIPS): 200 nsec/instr, "0" = 2 instr high (.4 vs .39 usec) + 3 instr low, "1" = 3 instr high + 2 instr low => 2/1/2 instr bit cycle; MARGINAL TIMING
//16F688 @18.432 MHz (4.6 MIPS): 217 nsec/instr, "0" = 1 - 2 instr high (.22 vs. .25 or .43 vs. .39) + 3 instr low (1.085 vs. 1.07 usec), "1" = 3 instr high + 1 - 2 instr low => 1/2/1; MARGINAL TIMING
//total bit cycle time @20 MHz: 6 instr * 200 nsec = 1.2 usec => 28.8 usec/node (24 bits)
//total bit cycle time @18.432 MHz: 6 instr * 217 nsec ~= 1.3 usec => 31.25 usec/node (24 bits)
//#if 0 //not enough instr
#define SEND_SERIES_BIT_TM1809_5MIPS(val, varbit, spare5)  \
{ \
/*	1!	2	3	4?	5	6!	7	8	1!	etc.*/ \
	portX.SERPIN = 1; /*bit start*/ \
		/*4 spare instr MIGHT be moved to here from below; for variable bit, THIRD instr must alter portX*/ \
						if (val) SWOPCODE(0, 4); /*"1" bit transition occurs later*/ \
						portX.SERPIN = 0; /*"0" bit transition occurs earlier*/ \
				/*if ((val) && ((val) != ~0))*/ SWOPCODE(0, 2); /*variable bit transition occurs later*/ \
				/*if ((val) && ((val) != ~0))*/ varbit; /*NOTE: for variable bits, this instr must alter portX*/ \
							spare5; /*6 spare instr (5 for variable bit); CAUTION: portX.SERPIN = 0 might be inserted mid way*/ \
}
//#endif


//TM1809 with 8 MIPS PIC:
//high speed TM1809: "0" = 0.3 usec high (0.25 - 0.39) + 0.6 usec low (0.53 - 0.67), "1" = 0.6 usec high (0.53 - 0.67) + 0.3 usec low (0.25 - 0.39), >= 50 usec reset
//16F182X @32 MHz (8 MIPS): 125 nsec/instr, "0" = 2 - 3 instr high + 5 instr low, "1" = 5 instr high + 2-3 instr low => 3/2/3 or 2/3/2 cycle; TIMING WITHIN SPEC
//total 3/2/3 bit cycle time: 8 instr * 125 nsec = 1 usec => 24 usec/node (24 bits)
//total 2/3/2 bit cycle time: 7 instr * 125 nsec = .875 usec => 21 usec/node (24 bits)
#define SEND_SERIES_BIT_TM1809_8MIPS(val, varbit, spare5or6)  \
{ \
/*	1!	2	3	4?	5	6!	7	8	1!	etc.*/ \
	portX.SERPIN = 1; /*bit start*/ \
		/*4 spare instr MIGHT be moved to here from below; for variable bit, THIRD instr must alter portX*/ \
						SWOPCODE(0, IIF(val, 4, 1)); /*"1" bit transition occurs later*/ \
						portX.SERPIN = 0; /*"0" bit transition occurs earlier*/ \
				if ((val) && ((val) != ~0)) SWOPCODE(0, 2); /*variable bit transition occurs later*/ \
				/*if ((val) && ((val) != ~0))*/ varbit; /*NOTE: for variable bits, first instr must alter portX*/ \
							spare5or6; /*5 spare instr (6 for const bit, counting varbit above); CAUTION: portX.SERPIN = 0 might be inserted mid way*/ \
}


//clock-dependent implementation:
#if (CLOCK_FREQ == 20 MHz) || (CLOCK_FREQ == 18432000) //5 or 4.6 MIPS; NOTE: //4.6 MIPS is 1.4% out of spec with WS2811 timing, but seems to work
 #define SEND_SERIES_BIT_TM1809(val, varbit, spare4or3_5mips)  SEND_SERIES_BIT_TM1809_8MIPS(val, varbit, { spare4or3_5mips; nop2(); }) //throttle back to 5 MIPS so caller doesn't need to change
#endif
#if (CLOCK_FREQ == 32 MHz) //8 MIPS
 #define SEND_SERIES_BIT_TM1809(comment, val, varbit, spare3or4_5mips, want_extra)  \
 { \
	PROGPAD(0); \
	SEND_SERIES_BIT_TM1809_8MIPS(val, varbit, { if (0 want_extra < 2) nop2(); spare3or4_5mips; }) /*throttle back to 5 MIPS so caller doesn't need to change; NOTE: spare3 might contain break, so put nop2() ahead of it*/ \
	if (0 want_extra >= 1) BANKSELECT(portX); \
	PROGPAD(MIN(7 want_extra, 8)); \
 }
// #warning "8 MIPS TM1809"
#endif


//low-level send parallel bits:
//nodes can be connected to up to 8 I/O pins (spread across 2 ports); this can be more convenient for strings/strips in close proximity such as a grid or tree
//parallel strings have shorter refresh times, but a parallel color palette takes more RAM and has more overhead so serial port pass-thru must be suspended during node I/O
//max string length is determined by sender's refresh rate after accounting for serial port transmission time, then multiply up to 8

//basic instr cycle for sending one bit (8 strings/strips in parallel):


//there are 6 spare instr if all bits are "0" or "1", only 4 spare instr if bits can vary at run time (only 3 if WREG needs to be loaded each time)
//bit end has at least 3 spare instr, which can be used to return to caller, load WREG, or other flow control
//for variable bits from RAM or ROM, val param should be ~0 and nop3 should load next bit values into WREG
//nop3 and loop3 params can be empty for "0" bits to reserve 6 instr at bit end for return/WREG/call overhead
// #if PortSelect(SINGLE_PORT) == 0 //both ports; processor utilization is 40% for all "0"s or "1"s, or 60% for mixed
//  #warning "[INFO] Compiled for WS2811 on 2 ports A and B/C"
//NOTE: second port lags behind first port by one instr due to instr interleave
//some A and B/C pins are duplicated, but there are 8 distinct pins between them (A5 is input-only, B1 and B2 are serial I/O)
//2 (const) or 3 (var) of 10 instr needed for one port; 4 (const) or 6 (var) of 10 instr needed for 2 ports
#define SEND_PARALLEL_BITS_WS2811_8MIPS(val, varbits2, spare2or6)  \
{ \
/*	1!	2	3?	4	5	6!	7	8	9	10w	1!	2	3?	4	5	6!	7	8	9	10	1!	... Port A*/ \
/*	...	6!	7	8	9	10	1!	2w	3?	4	5	6!	7	8	9	10	1!	... Port B/C*/ \
	compl(porta); /*--porta*/ /*bit start for port A (low-to-high transition)*/ \
		IFPORTBC(portbc = 0, nop1()); /*bit end for port B/C: high-to-low fall-back transition (variable bit transition for "0" occurs earlier)*/ \
			porta = WREG; /*variable bit transitions on port A*/ \
				spare2or6; /*2 spare instr here (6 instr for const hard-coded bits)*/ \
						porta = 0; /*bit end for port A*/ \
							IFPORTBC(compl(portbc) /*--portbc*/, nop1()); /*bit start for port B/C*/ \
								SWOPCODE(0, 2); /*move load A bits to after flush B/C bits*/ \
								varbits2 /*swap_WREG(indf_palette)*/; /*load port B/C bits into WREG*/ \
									IFPORTBC(portbc = WREG, nop1()); /*variable bit transitions for port B/C*/ \
										/*WREG = indf_autopalette*/; /*load port A bits into WREG, inc fsr*/ \
											NOPE(WREG += 8); /*NOT ENOUGH INSTR: shift A3 to A4*/ \
}
//			if ((val) && ((val) != ~0)) SWOPCODE(0, 1); /*variable bit transition occurs later*/
#if 0 //old
#define SEND_PARALLEL_BITS_WS2811_8MIPS(val, spare4or6)  \
{ \
/*	1!	2	3?	4	5	6!	7	8	9	10	1!	etc. Port A*/ \
/*		1!	2	3?	4	5	6!	7	8	9	10	1!	etc. Port B/C*/ \
	IFPORTBC(/*--portbc*/ compl(portbc), nop1()); /*bit start*/ \
		/*--porta*/ compl(porta); /*bit start*/ \
			/*2 spare instr MIGHT be moved to here from below; for variable bit, 2 instr here must alter lata and latbc*/ \
						if (val) { SWOPCODE(1, 4); SWOPCODE(0, 3); } /*"1" bit transition occurs later*/ \
						IFPORTBC(portbc = 0, nop1()); /*bit fall-back transition; "0" bit transition occurs earlier*/ \
							porta = 0; /*fall-back transition; "0" bit transition occurs earlier*/ \
			if (((val) == FROMRAM) || ((val) == (FROMRAM - 1))) { if ((val) == FROMRAM) IFPORTBC(portbc = WREG, nop1()); porta = WREG; } /*NOTE: for variable bits, 2 instr here must set lata and latbc*/ \
								spare4or6; /*4 spare instr (6 for const bit, counting varbit above); CAUTION: lata = 0, latbc = 0 might be inserted mid way*/ \
}					
#endif


//on a 5 MIPS PIC, there is enough instr bandwidth for hard-coded patterns on 2 ports but not variable patterns, so limit 5 MIPS PICs to one port:
//NOTE: for 2 ports, use 2 passes (one for each port); total string length (refresh time) would determine how closely they are in sync; data must not be altered for a longer period of time this way
#define SEND_PARALLEL_BITS_WS2811_5MIPS_ONEPORT(val, varbits, spare2or4)  \
{ \
/*	1!	2?	3	4!	5	6	1!	etc.*/ \
	compl(portX); /*--portX*/ /*bit start (low-to-high transition)*/ \
		/*2 spare instr MIGHT be moved to here from below; for variable bit, FIRST instr must alter portX*/ \
				if (val) SWOPCODE(0, 2); /*"1" bit transition occurs later*/ \
				portX = 0; /*bit end: high-to-low fall-back transition (variable bit transition for "0" occurs earlier)*/ \
		if ((val) && ((val) != ~0) /*== FROMRAM*/) varbits /*portX = WREG*/; /*NOTE: for variable bit transitions, 1 instr here must set portX*/ \
			spare2or4; /*3 spare instr (4 for const bit)*/ \
}

//there's not quite enough instr bandwidth to handle 8 data lines in parallel *and* serial pass-thru, so just do one port at a time:
#define SEND_PARALLEL_BITS_WS2811_8MIPS_ONEPORT(val, varbits2, spare6or8)  \
{ \
/*	1!	2	3?	4	5	6!	7	8	9	10	1!	etc.*/ \
	compl(portX); /*portX = ~portX*/; /*--portX*/ /*bit start (low-to-high transition)*/ \
		/*4 spare instr MIGHT be moved to here from below; for variable bit, SECOND instr must alter portX*/ \
						SWOPCODE(0, IIF(val, 4, 2)); /*"1" bit transition occurs later*/ \
						portX = 0; /*bit end: high-to-low fall-back transition (variable bit transition for "0" occurs earlier)*/ \
			if ((val) && ((val) != ~0)) { /*SWOPCODE(0, 1)*/; varbits2 /*portX = WREG*/; } /*NOTE: for variable bits, first instr here must alter portX*/ \
				spare6or8; /*7 spare instr (8 for const bit); CAUTION: portX = 0 might be inserted mid way*/ \
}


#if (CLOCK_FREQ == 20 MHz) || (CLOCK_FREQ == 18432000) //5 or 4.6 MIPS; NOTE: 4.6 MIPS is 1.4% out of spec with WS2811 timing, but seems to work
 #define SEND_PARALLEL_BITS_WS2811(comment, val, varbits, spare3or4_5mips, want_extra)  \
 { \
	PROGPAD(0); \
	SEND_PARALLEL_BITS_WS2811_5MIPS_ONEPORT(val, varbits, spare3or4_5mips); \
	PROGPAD(6 want_extra); \
 }
// #warning "5 MIPS WS2811"
#endif
#if (CLOCK_FREQ == 32 MHz) //8 MIPS
// #define SEND_PARALLEL_BITS_WS2811(comment, val, spare3or4_8mips, want_extra)  \
// { \
//	PROGPAD(0); \
//	SEND_PARALLEL_BITS_WS2811_8MIPS_ONEPORT(val, { if (0 want_extra < 4) nop4(); spare3or4_8mips; }); /*throttle back to 5 MIPS so caller doesn't need to change; NOTE: spare3 might contain break, so put nop2() ahead of it*/ \
//	/*if (0 want_extra >= 1) BANKSELECT(portX)*/; \
//	PROGPAD(MIN(10 want_extra, 10)); \
// }
 //extra: < 0 for instr that take > 1 instr cycle, > 0 to add nops in place of missing instr
 #define SEND_PARALLEL_BITS_WS2811(comment, val, varbits, spare6or8_8mips, want_extra)  \
 { \
	PROGPAD(0); \
	SEND_PARALLEL_BITS_WS2811_8MIPS_ONEPORT(val, varbits, { REPEAT(0 want_extra, nop1()); spare6or8_8mips; }); /*;;;;throttle back to 5 MIPS so caller doesn't need to change; NOTE: spare3 might contain break, so put nop2() ahead of it*/ \
	/*if (0 want_extra >= 1)*/ BANKSELECT(portX); \
	PROGPAD(MIN(10 want_extra, 10)); \
 }
#endif

//#if NODE_TYPE == WS2811
 #define WS2811_NODE_RESET_TIME  (50 usec)
//#else
//#if NODE_TYPE == TM1809
 #define TM1809_NODE_RESET_TIME  (25 usec)
//#else
// #error "[ERROR] Add node reset time here."
//#endif
//#endif


#if byteof(SERIN_PIN, 1) == 0xA1
 #define SERIN_PIN_defdesc  A1
#else
#if byteof(SERIN_PIN, 1) == 0xC5
 #define SERIN_PIN_defdesc  C5
#else
#if byteof(SERIN_PIN, 1) == 0xB1
 #define SERIN_PIN_defdesc  B1
#else
#if byteof(SERIN_PIN, 1) == 0xB2
 #define SERIN_PIN_defdesc  B2
#else
 #define SERIN_PIN_defdesc  byteof(SERIN_PIN, 1)
#endif
#endif
#endif
#endif

#if byteof(SEROUT_PIN, 1) == 0xA0
 #define SEROUT_PIN_defdesc  A0
#else
#if byteof(SEROUT_PIN, 1) == 0xC4
 #define SEROUT_PIN_defdesc  C4
#else
#if byteof(SEROUT_PIN, 1) == 0xB2
 #define SEROUT_PIN_defdesc  B2
#else
#if byteof(SEROUT_PIN, 1) == 0xB5
 #define SEROUT_PIN_defdesc  B5
#else
 #define SEROUT_PIN_defdesc  byteof(SEROUT_PIN, 1)
#endif
#endif
#endif
#endif

#if byteof(SERIN_PIN, 0) == 0xA5
 #define SERIN_PIN_altdesc  A5
#else
#if byteof(SERIN_PIN, 0) == 0xC5
 #define SERIN_PIN_altdesc  C5
#else
#if byteof(SERIN_PIN, 0) == 0xB2
 #define SERIN_PIN_altdesc  B2
#else
 #define SERIN_PIN_altdesc  byteof(SERIN_PIN, 0)
#endif
#endif
#endif

#if byteof(SEROUT_PIN, 0) == 0xA4
 #define SEROUT_PIN_altdesc  A4
#else
#if byteof(SEROUT_PIN, 0) == 0xC4
 #define SEROUT_PIN_altdesc  C4
#else
#if byteof(SEROUT_PIN, 0) == 0xB5
 #define SEROUT_PIN_altdesc  B5
#else
 #define SEROUT_PIN_altdesc  byteof(SEROUT_PIN, 0)
#endif
#endif
#endif


//initial tris settings for smart nodes or chplex:
#define TRISA_INIT(iotype)  TRISA_INIT_##iotype
#define TRISBC_INIT(iotype)  TRISBC_INIT_##iotype
//all Output on port A (reset is hard-wired as Input, RX must be input on extended PICs, doesn't matter on non-extended PICs); NOTE: lower 3 bits are used as shift register and *must* be set to Output
//all Output on port B or C (serial port must be set to Output, doesn't matter on non-extended PICs)
#define TRISA_INIT_NODES  IIF(PORTOF(SERIN_PIN) == 0xA, 1<<PINOF(SERIN_PIN), 0) | IIF(PORTOF(ZC_PIN) == 0xA, 1<<PINOF(ZC_PIN), 0) /*0x28*/
#define TRISA_INIT_PWM  TRISA_INIT_NODES
#define TRISA_INIT_CHPLEX  ~IIF(PORTOF(SEROUT_PIN) == 0xA, 1<<PINOF(SEROUT_PIN), 0) /*0x28*/
#define TRISBC_INIT_NODES  IIF(PORTOF(SERIN_PIN) != 0xA, 1<<PINOF(SERIN_PIN), 0) | IIF(PORTOF(ZC_PIN) != 0xA, 1<<PINOF(ZC_PIN), 0) /*0x3B*/
#define TRISBC_INIT_PWM  TRISBC_INIT_NODES
#define TRISBC_INIT_CHPLEX  ~IIF(PORTOF(SEROUT_PIN) != 0xA, 1<<PINOF(SEROUT_PIN), 0) /*0x3B*/
//only enable weak pullups on non-chipiplexing input pins (needed for ZC detect on Renard-PX1)
#define WPUA_INIT  IIF(PORTOF(ZC_PIN) == 0xA, 1<<PINOF(ZC_PIN), 0) | IIF(!UseIntOsc && (PORTOF(CLKIN_PIN) == 0xA), 1<<PINOF(CLKIN_PIN), 0) /*0x08*/ //pull up in case ext clock fails or is absent
#define WPUBC_INIT  IIF(PORTOF(ZC_PIN) != 0xA, 1<<PINOF(ZC_PIN), 0) /*0x3B*/

#ifdef WANT_CHPLEX
 #define IFCHPLEX(stmt)  stmt
#else
 #define IFCHPLEX(stmt)
#endif

volatile uint8 NodeConfig @adrsof(NODE_CONFIG); //upper nibble = node type, lower nibble = #parallel strings (#I/O pins used)

//re-initialize node I/O pin directions only (tris):
inline void nodepin_init(void)
{
//	is_chplex();
	IFCHPLEX(swap_WREG(NodeConfig); WREG ^= RENXt_CHPLEX(DONT_CARE); WREG &= 0x0F & ~1); //WREG = NodeConfig >> 4; //split up expr to avoid BoostC generating a temp
	WREG = TRISA_INIT(NODES); //all Output on port A (reset is hard-wired as Input, RX must be input on extended PICs, doesn't matter on non-extended PICs); NOTE: lower 3 bits are used as shift register and *must* be set to Output
	IFCHPLEX(if (EqualsZ) WREG = TRISA_INIT(CHPLEX));
	IFPORTA(trisa = WREG, EMPTY); //NODES), EMPTY); //all Output on port A (reset is hard-wired as Input, RX must be input on extended PICs, doesn't matter on non-extended PICs); NOTE: lower 3 bits are used as shift register and *must* be set to Output
	WREG = TRISBC_INIT(NODES);
	IFCHPLEX(if (EqualsZ) WREG = TRISBC_INIT(CHPLEX));
	IFPORTBC(trisbc = WREG, EMPTY); //NODES), EMPTY); //all Output on port B or C (serial port must be set to Output, doesn't matter on non-extended PICs)
}


//initialize I/O port pins:
inline void port_init(void)
{
	init(); //prev init first

#if defined(ANSELA)
	IFPORTA(ansela = 0, EMPTY); //must turn off analog functions for digital I/O
#endif
#if defined(ANSELB) || defined(ANSELC)
	IFPORTBC(anselbc = 0, EMPTY);
#endif
#ifdef NEEDS_CMCON0 //PIC16F688
	cmcon0 = NEEDS_CMCON0; //;configure comparator inputs as digital I/O (no comparators); overrides TRISC (page 63, 44, 122); must be OFF for digital I/O to work!
#endif
#ifdef APFCON
 #if (byteof(SERIN_PIN, 1) != byteof(SERIN_PIN, 0)) || (byteof(SEROUT_PIN, 1) != byteof(SEROUT_PIN, 0))
  #warning "[INFO] Moving serial pins from "SERIN_PIN_defdesc", "SEROUT_PIN_defdesc" to "SERIN_PIN_altdesc", "SEROUT_PIN_altdesc""
 #else
  #warning "[INFO] NO Move serial pins from "SERIN_PIN_defdesc", "SEROUT_PIN_defdesc" to alternate pins" //"SERIN_PIN_altdesc", "SEROUT_PIN_altdesc""
 #endif
//	apfcon = MY_APFCON; //move RX/TX to RA4/RA5
	if (byteof(SERIN_PIN, 1) && (byteof(SERIN_PIN, 1) != byteof(SERIN_PIN, 0))) apfcon.RXDTSEL = TRUE;  /*;RX on RA5 instead of RA1*/
	if (byteof(SEROUT_PIN, 1) && (byteof(SEROUT_PIN, 1) != byteof(SEROUT_PIN, 0))) apfcon.TXCKSEL = TRUE;  /*;TX on RA4 instead of RA0*/
#endif
	IFPORTA(porta = 0, EMPTY); //set all (output) pins low; do this before setting tris to avoid stray pulses in case any pins were high
	IFPORTBC(portbc = 0, EMPTY);
//paranoid: leave TRIS at Hi-Z initially in case I/O pins are Chplex, then set to pwm/nodes after we know for sure
	IFPORTA(trisa = TRISA_INIT(CHPLEX), EMPTY); //NODES), EMPTY); //all Output on port A (reset is hard-wired as Input, RX must be input on extended PICs, doesn't matter on non-extended PICs); NOTE: lower 3 bits are used as shift register and *must* be set to Output
	IFPORTBC(trisbc = TRISBC_INIT(CHPLEX), EMPTY); //NODES), EMPTY); //all Output on port B or C (serial port must be set to Output, doesn't matter on non-extended PICs)
	IFPORTA(wpua = WPUA_INIT, EMPTY); //only enable weak pullups on non-chipiplexing input pins (needed for ZC detect on Renard-PX1)
#ifdef WPUBC_ADDR
	IFPORTBC(wpubc = WPUBC_INIT, EMPTY); //no pullups on B/C needed
#endif
}
#undef init
#define init()  port_init() //initialization wedge, for compilers that don't support static initialization


//;==================================================================================================
//;High-level node I/O functions
//;==================================================================================================

//#define TRACE(where)  { /*ABSLABEL(70 + where)*/; /*portX*/ WREG = where; }
//#define xTRACE(where)
////void fatal(void); //fwd ref
//#define fatal()  nop1()


//#define PaletteAdd(rgb)  \
//{ \
//	WREG = G(rgb); WREG2indf(fsrL); \
//	if (R(rgb) != G(rgb)) WREG = R(rgb); WREG2indf(fsrL); \
//	if (B(rgb) != R(rgb)) WREG = B(rgb); WREG2indf(fsrL); \
//}


//#undef WREG2indf //avoid conflict
//#define WREG2indf(fsr)  WREG2indf_##fsr()
//inline void WREG2indf_fsrL(void) //post-inc
//{
//#ifdef PIC16X //extended instr set
//	MOVWI(IIF(adrsof(fsr) == adrsof(fsr0L), 0, 1), 2); //post inc
//#else
//	indf = WREG;
////	add_banked(1);
//	++fsrL;
//#endif
//}


//wait for nodes to update:
//inline so bank sel will carry over to caller
///*non_*/inline void send_reset(void)
//{
////	ONPAGE(0);
////	nop4(); //padding in case last bit was incomplete; not needed because of overhead to get here
//	IFPORTA(porta = 0, EMPTY);
//	IFPORTBC(portbc = 0, EMPTY);
//	wait(NODE_RESET_TIME, EMPTY);
////	BANKSELECT(portX); //force bank sel here so caller doesn't need it (BoostC forgets which bank is selected after function calls)
////	WREG = 0xFF; //setup for first bit transition
//}


//config data and global/static state:
//volatile uint8 StateBits @MISCBITS_ADDR; //misc state bits; managed by their respective sections of code, but initialized below
//volatile uint8 AdrsMode @ADRSMODE_ADDR; //node address mode: #bpp, series vs. parallel
volatile uint8 NodeBytes @adrsof(NODE_BYTES); //scaled node pairs; upper 8 bits of node data size; NOTE: for devices with > 256 RAM this will be byte pairs or quads
//volatile bit NodeBytesH @BITADRS(NODE_BYTESH_ADDR); //top bit of node byte count; 9-bit count gives max 1K nodes @4 bpp, 2K @2 bpp, or 4K @1 bpp
//volatile uint8 NodeConfig @adrsof(NODE_CONFIG); //upper nibble = node type, lower nibble = #parallel strings (#I/O pins used)
volatile bit ParallelNodes @BITADDR(adrsof(PARALLEL_NODES)); //adrsof(NODE_CONFIG).(4+0); //bottom bit used to indicate parallel wiring
volatile bit ActiveHigh @BITADDR(adrsof(COMMON_CATHODE)); //adrsof(NODE_CONFIG).(4+0); //bottom bit used to indicate common anode
volatile uint8 ProtocolErrors @adrsof(PROTOERRS); //keep track of invalid opcodes; these might cause additional data to be discarded; upper nibble = count (doesn't wrap), lower nibble = reason code
//volatile uint8 LastProtocolError @adrsof(DEMO_STACKFRAME3) + 2; //UNUSED3)+2;
//volatile bit is_1bpp @adrsof(ISBPP2)/8.1;
//volatile bit is_2bpp @adrsof(ISBPP2)/8.2;
//volatile bit IsBPP2 @BITADRS(ISBPP2);
//volatile bit IsBPP1 @BITADRS(ISBPP1);
//volatile bit SerialWaiting @BITADRS(adrsof(statebit_0x20)); //serial input waiting; filters out framing errors to avoid sending junk to downstream controllers

#define PROTOERR_NONE  0 //dummy value; won't ever be used so if you see this value then something is not right or an error did not occur
#define PROTOERR_SPEED  1 //not running fast enough for this type of node I/O
#define PROTOERR_EXCLUDED  2 //option was excluded by flags at compile time
#define PROTOERR_TIMESLOT  3 //pwm/ch'plex time slot overrun; processor is not correctly provisioned (considered to be a firmware bug)
#define PROTOERR_UNIMPLOP  4 //unknown opcode
#define PROTOERR_NODECFG  5 //attempt to change node config (type or count) after first I/O has been completed; that' not how hardware is supposed to work
#define PROTOERR_FIRMBUG  6 //whoops, some important values in RAM where overwritten somewhere; serious firmware bug somewhere
#define PROTOERR_UNGRANTED  7 //opcode was recognized but disallowed due to other conditions (typically an incorrect parameter)
#define PROTOERR_MULTIROW  8 //attempt to turn on more than one ch'plex row simultaneously (client software bug)
#define PROTOERR_TRACE  15 //reserved for debug trace info

//store location (reason) of last protocol error:
//mainly for debug, but also useful for diagnosing comm problems
//count is in upper nibble (doesn't wrap, assumes small numbers), last reason code in lower nibble
#define PROTOCOL_ERROR(errnum)  \
{ \
	PutLower(ProtocolErrors, errnum); \
	WREG = ProtocolErrors + 0x10; \
	if (!Carry) ProtocolErrors = WREG; /*inc count, prevent wrap (assumes small count only)*/ \
}


//#define SetBPP(bpp)  { bpp_0x03 &= 0x03; bpp_0x03 |= ((bpp) & 3); } //4 instr
//#define x_SetNodeCount(count, bpp)  \
//{ \
//	NodeBytes = MIN(Nodes2Bytes(count, bpp), TOTAL_BANKRAM - (3<<1)) / RAM_SCALE; /*minimum 2 palette entries, otherwise it's just a dumb string*/ \
//	SetBPP(bpp); /*determines palette size and node packing; DURING RCV ONLY*/ \
//}
//bpp must be a const or else BoostC will generate temps
//#define SetBPP(bpp) /*2 instr*/ \
//{ \
//	bpp_0x03.1 = (bpp) & 2; \
//	bpp_0x03.0 = (bpp) & 1; \
//}
//inline void SetBPP_WREG(void)
//{
//	SetBPP(0); //reset bits first; NOTE: doesn't alter WREG
//	WREG &= BPP_MASK; //only bottom 2 bits are used ("0" ==> 4)
//	bpp_0x03 |= WREG; //merge in new bits
//}


//abs labels:
//these allow flow paths outside of the normal C scoping rules (ie, jump into a function, jump from one function to another without using the stack, etc).
#define MoreNodes  1 //more node data to send
#define NoSerialPassthru  2 //do not pass thru serial data during node I/O
//#define FirstSerialReceived  3
#define SerialCancel  4 //cancel demo node I/O and start handling protocol
#define SerialPassthru  5 //pass thru serial data during node I/O
#define NoMoreNodes  6 //no more node data to send
//#define NodeTrailer  7 //idle period after sending nodes

#define MoreTrailer  8 //send more trailer bits
//#define EndTrailer  9 //no more trailer bits

//#define GetCharNoEcho  7 //reset serial input echo flag
#define BadOpcode  10 //invalid opcode in packet; ignore remainder of packet (can't parse)
#define CountWaitForSync  11 //pass thru remaining bytes in packet; init byte count
#define WaitForSync  12 //pass thru remaining bytes in packet; no byte counter
#define EndOfPacket  13 //start processing next packet (after Sync received)
#define IllegalOpcode  14 //illegal opcode in packet; opcode was recognized but ignored; discard remainder of packet rather than trying to parse current opcode and recovering next
//start processing next packet:
#define nextpkt(pktstate)  ABSGOTO(pktstate) //NOTE: can only be used with inline code

#define SendLoop  15 //send loop for parallel WS2811
#define ResumeNodeIO  16 //continue sending nodes

#define SetAll_GECEAlternating  17 //demo kludge: set alternating gece nodes, to make demo behavior consistent

//#define NoSetFSR  11 //FSR already set
//I/O handlers:
//#define ResumeGECE  12 //StartGECEIO  12 //continue gece I/O after processing incoming protocol char
//#define StartChplexIO  13 //continue chplex/pwm I/O after processing incoming protocol char
//#define StartWS2811Series  14 //start WS2811 series I/O
//#define StartWS2811Parallel  15 //start WS2811 parallel I/O
//#define ResumeChplex  13 //caller returns here after processing char

//#define resume_io()  { WREG = NodeConfig >> 4; } //root of inline function chain for resuming persistent node I/O types

#define NextAbsLabel  16 //next available in this range


//these labels make it easier to find places in the compiled code (especially inlined code):
//this is just for debug; also helps to ensure that inline code was only expanded once
#define OpcodeDispatch  100
#define GetBitmapOpcode  101
#define SetNodeListOpcode  102
#define SetPaletteOpcode  103
#define SetNodeTypeOpcode  104
#define ReadRegOpcode  105
#define WriteRegOpcode  106
#define SetNodesOpcode  107
#define CtlrFuncOpcode  108
#define PktStatusOpcode  109


//#define BANKWSAP(fromreg, toreg)  { if (bankof(adrsof(fromreg)) != bankof(adrsof(toreg))) BANKSELECT(toreg); }
//broken #define BankOverhead(reg)  +IIF(bankof(adrsof(reg)) == bankof(adrsof(portX)), 0, 2) //BoostC not token pasting
#define BankOverhead(reg)  +2*(bankof(adrsof(reg)) != /*bankof(adrsof(portX))*/ portX_ADDR/BANKLEN) //BoostC not token pasting
#define notBankOverhead(reg)  +2*(bankof(adrsof(reg)) == /*bankof(adrsof(portX))*/ portX_ADDR/BANKLEN) //BoostC not token pasting
#ifdef PIC16X //linear addressing; only supported for faster PICs
//not needed? #define LEADING_NOP1  -1 //kludge: leading nop will be combined into nop2() for 8 MIPS
 #define SET_FSRH  +1 //takes 2 instr: movlw + movf
#else
// #define LEADING_NOP1  +0
 #define SET_FSRH  +0 //takes 1 instr: bcf/bsf
#endif
#define LEADING_NOP2  -1
#define ALTERS_PCL  -1
#define HAS_NOP4  -3

//kludge: rotate palette entries by 1 bit to compensate for 5 mips palette shift-out logic below (palette entry bit send order is 0b07654321)
//this processing is done during node reset time anyway (1 out of 2 times), so there's no significant timing delay
//5 instr setup + 48 * 6 instr loop ~= 300 instr == 60 usec @ 5 MIPS or < 40 usec @ 8 MIPS
//since this is > serial data rcv time (44 usec @250k baud), serial port check is added into loop
#if 0
#define SHIFT_BEFORE  TRUE
#define UNSHIFT_AFTER  FALSE
#define palette_shift(before)  \
{ \
	volatile uint8 count @adrsof(LEAF_PROC); \
	fsrL = PALETTE_ADDR(0, 4 BPP); fsrH = PALETTE_ADDR(0, 4 BPP) / 0x100; /*set palette start address*/ \
	count = 3<<4; /*#palette bytes to fix; entire palette is 16 entries (3 bytes each)*/ \
	WREG = Nodes2Bytes(NUM_NODES(4 BPP), 4 BPP) / RAM_SCALE - NodeBytes; /*compare to max #nodes with full palette*/ \
	if (NotBorrow) WREG = 0; /*no overlap*/ \
	count += WREG; /*reduce max palette size by amount of node overlap (WREG <= 0 here); minimum palette is 6 bytes*/ \
	if (RAM_SCALE > 1) count += WREG; \
	if (RAM_SCALE > 2) { count += WREG; count += WREG; } \
	for (;;) \
	{ \
		BusyPassthru(RECEIVE_FRAMERRS, 3); /*need to do this >= every 44 usec for sustained 250k baud @8N2*/ \
		if (before) rr(indf); /*0b76543210 => 0b07654321*/ \
		else rl(indf); /*0b07654321 => 0b76543210*/ \
		++fsrL; \
		if (!--count) break; \
	} \
}
#endif

//banks 0+1 == node data (2 * 80 bytes * 4 bpp == 320 nodes); nodes stored in 0x20..0x6F,0xA0..0xEF; IRP = 0
//TODO: could go as high as (3 * 80 - 5 * 3) * 4 bpp == 450 nodes by using 13 palette entries for node data; ~= 13.5 msec
//TODO: could go as high as (80 + 95) * 2 bpp == 800 nodes by using first 15 bytes of non-banked RAM; 700 * 24 * 1.2 usec ~= 23 msec
//TODO: could go as high as (80 + 95) * 1 bpp == 1600 nodes by using first 15 bytes of non-banked RAM; 1400 * 24 * 1.2 usec ~= 46 msec
//256 bytes RAM is the max needed on 5 MIPS (non-extended) PICs => max (240 - 48) * 2 == 384 nodes, requires 8-bit ptr + parity
//1K nodes (512 bytes) is probably the max needed for 8 MIPS (extended) PICs => 9-bit linear adrs + parity
//bank 2 == palette; only uses 48 of 80 bytes; need to skip over SFR; skip an additional 32 bytes for more efficient addressing; IRP = 1
//send 320 of 384 nodes * 24 bits * 1.2 usec ~= 9.2 msec
//palbytes are in 07654321 bit order because data is shifted out over A2 instead of A0 (to reduce potential ICSP conflicts, and because A2 is in the corner for some PICs)
#ifdef WANT_WS2811
non_inline void send_ws2811_series_4bpp(void) @0x04 //put near beginning of code page 0 so pclath<2:0> doesn't change; NOTE: ASM fixup script ignores this, but BoostC will reorder the ASM code based on this address so it does improve the outcome somewhat
{
#define SEND_SERIES_BIT  SEND_SERIES_BIT_WS2811
	ONPAGE(IOH_PAGE); //put on same page as protocol handler to reduce page selects
//	ATADRS(4); //put near beginning of code page 0 so pclath<2:0> doesn't change; NOTE: ASM fixup script ignores this, but BoostC will reorder the ASM code based on this address so it does improve the outcome somewhat

//nodeptrH + nodeptrL + parity ~= 10-bit node ptr; this can address 1K nodes (linear) or 640 nodes (banked) @4 bpp
//#if TOTAL_BANKRAM > 512 //use fsr1 for 16f1825 with 1K RAM (not enough instr with 5 MIPS)
#ifdef PIC16X //linear addressing
//	volatile uint8 nodeptrL @adrsof(FSR1L);
	volatile uint8 nodeptrH @adrsof(FSR1H); //need >= 2 bits for 1K RAM
//	volatile uint8 indf_node @adrsof(INDF1);
#else
	volatile bit nodeptrH @BITADDR(adrsof(statebit_0x10)); //@adrsof(NODE_BYTES).7; //;;;;kludge: use top bit of node size since it will always be 0 (not enough memory); //BITADDR(adrsof(statebit_0x10)); //upper bit of node ptr; 9-bit adrs allows 1<<9 == 512 bytes linear or 4 * 80 == 320 bytes banked
#endif
	volatile uint8 nodeptrL @adrsof(PROTOCOL_STACKFRAME3) + 0; //lower byte of current node pair address; must be non-banked to prevent interference with bit loop timing
//	volatile uint8 indf_node @adrsof(INDF);
//	volatile uint8 SerialJumpOfs @adrsof(SEND_NODES_STACKFRAME) + 1; //jump ofs for serial data handler
//	volatile skip_nodes @SEND_NODES_SKIP_NODES_ADDR, pall_end @SEND_NODES_PALLEND_ADDR; //must be non-banked to prevent interference with bit loop timing
//	volatile uint8 skip_nodes @adrsof(SEND_NODES_STACKFRAME) + 2; //#nodes to skip; CAUTION: reuses send_count
	volatile uint8 send_count @adrsof(PROTOCOL_STACKFRAME3) + 1; //#bytes to send; count is more efficient than address compare (bytes not contiguous: 0x50..0x70, 0xa0..0xf0, 0x120..0x170); NOTE: units are byte pairs for 512 RAM or byte quads for 1K RAM
	volatile uint8 pal_cache @adrsof(PROTOCOL_STACKFRAME3) + 2; //cached palette byte (frees up fsr earlier)
	volatile uint8 octnode @adrsof(statebit_0x20)/8; //3-bit node octet counter; together with send_count this gives an 11-bit node counter, which is enough for 1K bytes @4 bpp; NOTE: must be upper bits so Carry will not affect other bits; kludge: BoostC doesn't support bit sizes
	volatile bit parity @adrsof(statebit_0x20)/8.(8-RAM_SCALE_BITS); //@BITADRS(statebit_0x80); //parity (bottom bit) of current node#; even nodes are in upper nibble, odd nodes are in lower nibble (4 bpp)
//	volatile bit SerialWaiting @BITADRS(adrsof(statebit_0x10)); //serial input waiting; filters out framing errors to avoid sending junk to downstream controllers
	volatile uint8 jumpofs @adrsof(PROTOCOL_STACKFRAME3) + 2; //CAUTION: shared location; adrsof(LEAF_PROC); //jump ofs for serial data handler
//#ifdef PIC16X //linear addressing; no bank gaps; 9-bit counter allows up to 511 bytes (address space is 1-to-1)
//	volatile bit fsrH10 @adrsof(fsrH).1; //2nd to bottom bit of upper byte; must be settable with 1 instr, and only need 512 bytes anyway
//	volatile bit fsrH9 @adrsof(fsrH).0; //bottom bit of upper byte; must be settable with 1 instr, and only need 512 bytes anyway
// #define BankSpanAdjust(ptr)  { WREG = ptr; WREG = 0xFE - WREG; } //just set Borrow if ptr wrapped (ptr == 0xFF); ptr was already updated
//#else //compensate for bank gaps; 9-bit counter allows up to 4 * 80 = 320 bytes (0x50 banked RAM occupies 0x80 of address space)
//	volatile bit fsrH9 @adrsof(STATUS).IRP; //bottom bit of upper byte; must be settable with 1 instr, and only need 512 bytes anyway
// #define BankSpanAdjust(ptr)  { WREG ^= GPRGAP; ptr -= WREG; } //adjust node ptr to span banks
//#endif

//ABSLABEL(StartWS2811Series); /*send_node jumps here*/
	if (ALWAYS) goto ioprep; //kludge: move initialization down past I/O loop to allow 8-bit jump offsets within loop
	BANKSELECT(portX); //pre-select bank to prevent compiler from inserting bank selects below (interferes with bit I/O timing)
#if SERPIN != 2
 #error "[ERROR] 5 MIPS logic is hard-coded for A2, but "SERPIN" is selected."
#endif
#if defined(FSR0H_ADDR) && (((PALETTE_ADDR(0, 4 BPP) /0x100) & ~0x20) != 0)
 #error "[ERROR] Upper Palette address must be 0 or settable with single instr (clrf or bcf/bsf)"
#endif
//#if RAM_SCALE > 2 //byte quads
// #define OCTINC  0x20
// #define NeedSendCountUpdate  Carry //update byte counter after each 4 nodes
//#else
//#if RAM_SCALE > 1 //byte pairs (4 bpp)
// #define OCTINC  0x40
// #define NeedSendCountUpdate  Carry //!parity //update byte counter after each 2 nodes
//#else //individual bytes (4 bpp)
// #define OCTINC  
// #define NeedSendCountUpdate  !parity //!pclath.3 //update byte counter after each node (condition always true)
//#endif
//#endif

//SEND_SERIES_BIT(comment, val, varbit, spare3or4_5mips, want_extra)
	for (;;) //NOTE: this block of code *MUST* fit within one 256 word code page (8-bit jump offsets are used)
	{
		INGOTOC(TRUE); //following block of code uses 8-bit jump offsets; NOTE: 24 bits * 10 instr == 240, so WS2811 at 8 MIPS should fit okay (except that conditional bits also take some space)
//finish sending blue:
//also set palette ptr for next node
ABSLABEL(MoreNodes);
#ifdef PIC16X //linear addressing; upper byte of node ptr might be > 1 bit (16F1825 has 1K RAM)
		SEND_SERIES_BIT(B4 @1000, FROMRAM, rl_nc(portX), { fsrH = nodeptrH; rr_nc_WREG(pal_cache); }, +0); //send blue.4; restore upper node ptr; pre-load WREG+Carry with blue.?7654321+0
#else //banked addresses; NOTE: assumes <= 512 bytes RAM (fsrH is only 1 bit)
		SEND_SERIES_BIT(B4 @1000, FROMRAM, rl_nc(portX), { rr_nc_WREG(pal_cache /*pal B low*/); if (nodeptrH) fsrH = TRUE; }, +0); //send blue.4; restore upper node ptr (was 0 for palette); pre-load WREG+Carry with blue.?7654321+0; NOTE: assumes 
#endif
		SEND_SERIES_BIT(B3 @1001, FROMRAM, portX = WREG, { fsrL = nodeptrL; swap(indf); }, +0); //send blue.3; point to next node pair, change node parity; CAUTION: must preserve Carry (holds blue.4)
		SEND_SERIES_BIT(B2 @1002, FROMRAM, rl_nc(portX), { fsrL = indf & 0x0F; }, +0); //send blue.2; get palette entry# for first (even) node; adjust for even/odd node parity (so a separate even/odd branch is not needed); get next palette entry#
		SEND_SERIES_BIT(B1 @1003, FROMRAM, rl_nc(portX), { palinx2ofs_WREG(fsrL); /*fsrL += WREG; WREG += PALETTE_ADDR(0, 4 BPP); fsrL += WREG*/; }, +0); //send blue.1; convert palette index to ptr; offset is 3 * palette entry#
		SEND_SERIES_BIT(B0 @1004, FROMRAM, rl_nc(portX), { fsrH = PALETTE_ADDR(0, 4 BPP) / 0x100; swap(indf /*pal R high*/); rr_nc_WREG(indf); }, +0 SET_FSRH); //send blue.0; pre-load WREG+Carry with red.?3210765+4
next_node:
//send red (for WS2811 LED strip, this is actually green because red + green are swapped):
//also setup for next node and reload jumpofs (memory shared with pal_cache)
//WREG+Carry here = ?3210765+4 red bits
		SEND_SERIES_BIT(G7 @1005, FROMRAM, portX = WREG, { WREG = 1<<(8-RAM_SCALE_BITS); if (parity) --nodeptrL; }, +0); //send green.7; advance to next node pair; CAUTION: must preserve Carry (holds green.4)
		SEND_SERIES_BIT(G6 @1006, FROMRAM, rl_nc(portX), { octnode += WREG; if (Carry) --send_count; }, +0); //send green.6; update node parity/quadrature and remaining byte count
		SEND_SERIES_BIT(G5 @1007, FROMRAM, rl_nc(portX), { ABSDIFF_WREG(SerialPassthru, NoSerialPassthru); if (protocol_inactive) ABSDIFF_WREG(SerialCancel, NoSerialPassthru) /*nop3()*/; }, +0 /*LEADING_NOP1*/); //send green.5; restore jumpofs
		SEND_SERIES_BIT(G4 @1008, FROMRAM, rl_nc(portX), { jumpofs = WREG; swap(indf); rr_nc_WREG(indf /*pal G low*/); }, +0 /*LEADING_NOP1*/); //send green.4; pre-load WREG+Carry with green.?7654321+0
#ifdef PIC16X //linear addressing; don't need to adjust for bank gaps, but still need to adjust upper byte of node ptr (16F1825 has 1K RAM)
		SEND_SERIES_BIT(G3 @1009, FROMRAM, portX = WREG, { nop1(); if (!parity) ++nodeptrL; }, +0 /*LEADING_NOP1*/); //send green.3; kludge: undo parity dec of nodeptrL above, so it can be redone here
		SEND_SERIES_BIT(G2 @1010, FROMRAM, rl_nc(portX), { WREG = 0; if (!parity) WREG = 1; }, +0); //send green.2; set up node ptr decrement
#else //banked addressing
		SEND_SERIES_BIT(G3 @1009, FROMRAM, portX = WREG, { WREG = nodeptrL; WREG ^= (SFRLEN - 1); WREG &= ~BANKLEN; }, +0); //send green.3; check for bank wrap; CAUTION: must preserve Carry (holds green.0) and Z (results of bank wrap check) here
		SEND_SERIES_BIT(G2 @1010, FROMRAM, rl_nc(portX), { WREG = 0; if (EqualsZ) WREG = GPRGAP; }, +0); //send green.2; set bank adjust if it wrapped
#endif
		SEND_SERIES_BIT(G1 @1011, FROMRAM, rl_nc(portX), { nodeptrL -= WREG; if (Borrow) fsrH_adjust(nodeptrH, -1); }, +0); //send green.1; adjust node ptr to span banks
		SEND_SERIES_BIT(G0 @1012, FROMRAM, rl_nc(portX), { ++fsrL; swap(indf /*pal B high*/); rr_nc_WREG(indf); }, +0); //send green.0; pre-load WREG+Carry with blue.?3210765+4
//send green (for WS2811 LED strip, this is actually red because red + green are swapped):
//also serial pass-thru; checking 1x/node (~ 30 usec) allows > 250k baud sustained rate
//WREG+Carry here = ?3210765+4 bits for green
		SEND_SERIES_BIT(R7 @1013, FROMRAM, portX = WREG, { WREG = jumpofs; if (!IsCharAvailable /*pir1.RCIF*/) WREG = 0; }, BankOverhead(pir1)); //send red.7; check serial port; CAUTION: Carry must be preserved (holds red.4)
		SEND_SERIES_BIT(R6 @1014, FROMRAM, rl_nc(portX), { nop1(); if (HasFramingError /*rcsta.FERR*/) WREG = 0; }, BankOverhead(rcsta)); //send red.6; don't pass thru frame errors; hold them and let overrun error trigger comm port reset
		SEND_SERIES_BIT(R5 @1015, FROMRAM, rl_nc(portX), { swap(indf); pcl += WREG; }, ALTERS_PCL); //send red.5; jump to serial pass-thru handler; CAUTION: check dest adrs
ABSLABEL(NoSerialPassthru); //no serial data waiting; base adrs for jump offsets
		SEND_SERIES_BIT(R4 @1016, FROMRAM, rl_nc(portX), { rr_nc_WREG(indf /*pal R low*/); nop2(); }, +0); //send red.4; load WREG+Carry with red.?7654321+0
		SEND_SERIES_BIT(R3 @1017, FROMRAM, portX = WREG, { nop3(); }, +0 /*LEADING_NOP1*/); //send red.3
		SEND_SERIES_BIT(R2 @1018, FROMRAM, rl_nc(portX), { nop3(); }, +0 /*LEADING_NOP1*/); //send red.2
		SEND_SERIES_BIT(R1 @1019, FROMRAM, rl_nc(portX), { nop1(); if (ALWAYS) goto serial_done; }, ALTERS_PCL /*LEADING_NOP1*/); //send red.1; kludge: avoid dead code removal
ABSLABEL(SerialPassthru); //serial pass-thru; assume packet is not mine since node I/O is already in progress
		SEND_SERIES_BIT(R4 @1020, FROMRAM, rl_nc(portX), { rr_nc_WREG(indf /*pal R low*/); nop2(); }, +0); //send red.4; pre-load WREG+Carry with red.?7654321+0
#if 0 BankOverhead(rcreg) //def PIC16X //extra bank selects needed, but can't be done before setting port bits
		SEND_SERIES_BIT(R3 @1021, FROMRAM, portX = WREG, { nop2(); WREG = rcreg; }, BankOverhead(rcreg) /*BankOverhead(rcreg)*/); //send red.3; pass thru serial char
		SEND_SERIES_BIT(R2 @1022, FROMRAM, rl_nc(portX), { nop1(); BANKSELECT(txreg) /*pre-select for shorter btf branch*/; if (WantEcho) txreg = WREG; }, BankOverhead(txreg) /*BankOverhead(txreg)*/); //send red.2; echo serial char
		SEND_SERIES_BIT(R1 @1023, FROMRAM, rl_nc(portX), { ++iochars.bytes[0]; if (EqualsZ) ++iochars.bytes[1]; if (EqualsZ) ++iochars.bytes[2]; }, +2 /*LEADING_NOP1*/); //send red.1; update serial stats
#else //no bank select needed
		SEND_SERIES_BIT(R3 @1021, FROMRAM, portX = WREG, { WREG = rcreg; if (WantEcho) txreg = WREG; }, BankOverhead(rcreg)); //send red.3; pass thru serial char
		SEND_SERIES_BIT(R2 @1022, FROMRAM, rl_nc(portX), { ++iochars.bytes[0]; if (EqualsZ) ++iochars.bytes[1]; }, +0); //send red.2; update serial stats
		SEND_SERIES_BIT(R1 @1023, FROMRAM, rl_nc(portX), { nop1(); if (EqualsZ) ++iochars.bytes[2]; }, +0 /*LEADING_NOP1*/); //send red.1; finish updating serial stats
#endif
//#warning "TODO: finish reading bitmap during node I/O"
#if 0
		if (rcv_bitmap) //TODO: allow more node data to arrive during node I/O
		{
			indf_autonode = rcreg;
			if (WREG == RENARD_ESCAPE)
			if (WREG == RENARD_SYNC)
		}
#endif
serial_done:
		SEND_SERIES_BIT(R0 @1024, FROMRAM, rl_nc(portX), { ++fsrL; swap(indf); rr_nc_WREG(indf /*pal G high*/); }, +0); //send red.0; pre-load WREG+Carry with green.?3210765+4
//start sending blue:
//also check eof (send_count == 0)
//WREG+Carry here = ?3210765+4 bits for blue
		SEND_SERIES_BIT(B7 @1025, FROMRAM, portX = WREG, { swap(indf /*pal B low*/); pal_cache = indf; }, +0 /*LEADING_NOP1*/); //send blue.7; save palette byte in cache; CAUTION: must preserve Carry here (holds blue.4)
		SEND_SERIES_BIT(B6 @1026, FROMRAM, rl_nc(portX), { WREG = send_count; if (!EqualsZ) ABSDIFF_WREG(MoreNodes, NoMoreNodes); }, +0); //send blue.6; check for eof
		SEND_SERIES_BIT(B5 @1027, FROMRAM, rl_nc(portX), { nop1(); pcl += WREG; }, ALTERS_PCL); //send blue.5; conditional loop control (if not eof); CAUTION: check dest address
ABSLABEL(NoMoreNodes); //NOTE: this must occur < address 0xFF in order for 8-bit jump offsets to work
		INGOTOC(GOTOC_WARNONLY); //end of 8-bit jump offsets
		if (ALWAYS) break; //kludge: avoid dead code removal
	}
//finish off last node (no setup for next node):
	SEND_SERIES_BIT(B4 @1028, FROMRAM, rl_nc(portX), { send_count = 3; rr_nc_WREG(pal_cache); }, +0); //send blue.4 (eof case); pre-load WREG+Carry with blue.?7654321+0
	SEND_SERIES_BIT(B3 @1029, FROMRAM, portX = WREG, { nop1(); if (ALWAYS) goto trailer; }, ALTERS_PCL /*LEADING_NOP1*/); //send blue.3
#if CLOCK_FREQ <= 20 MHz //not enough instr for loop control with variable bit @5 MIPS
trailer:
#define trailer  trailer2 //kludge: avoid name conflict with other label
//ABSLABEL(NodeTrailer);
//	for (;;) { SEND_SERIES_BIT(B2..B0 @1030, FROMRAM, rl_nc(portX), { if (--send_count) continue; nop1(); }, +0); break; } //send blue.2..0
	SEND_SERIES_BIT(B2 @1030, FROMRAM, rl_nc(portX), { nop1(); nop2(); }, +0); //send blue.2
	SEND_SERIES_BIT(B1 @1031, FROMRAM, rl_nc(portX), { nop1(); nop2(); }, +0); //send blue.1
	SEND_SERIES_BIT(B0 @1032, FROMRAM, rl_nc(portX), { nop1(); nop2(); }, +0); //send blue.0
//	IFPORTA(porta = 0, EMPTY);
//	IFPORTBC(portbc = 0, EMPTY);
	portX = 0;
	front_panel(~FP_NODEIO);
	if (ALWAYS) return;
#endif

//	BANKSELECT(bank1[0]); //kludge: force bank select below
//ABSLABEL(NodeTrailer);
//	nodeptrH = 0; //kludge: restore reused bit to original value; only needed for !PIC16X
//palette_shift takes ~= same time as send_reset, so don't need to do both:
//~ 30 usec @8 MIPS; serial char >= 40 usec, so by next serial char enough wait time will accumulate
//#if 1
//	IFPORTA(porta = 0, EMPTY);
//	IFPORTBC(portbc = 0, EMPTY);
//	palette_shift(UNSHIFT_AFTER); //restore palette so caller can manipulate unaltered palette; also used as reset time
//	if (IsNonZero24(iochars)) NoSerialWaiting = FALSE; //tell caller there was serial I/O
//	WREG = iochars.bytes[0]; WREG |= iochars.bytes[1]; WREG |= iochars.bytes[2]; //status.Z => no serial I/O
//	TRACE(0x24); if (!NoSerialWaiting) fatal();
//	if (ALWAYS) return; //RETURN();

//not sure if WS2811 likes being interrupted in the middle of a node, so this will flush out last partial node:
//serial char might have arrived immediately after last check, so need to handle it now rather than waiting for end of node I/O
//NOTE: this will eat first char, so if it was a Sync then sender must send 2 Syncs to force into protocol mode
//	BANKSELECT(bank0[0]); //kludge: inhibit bank select below
ABSLABEL(SerialCancel); //cancel demo node I/O so protocol handler can be started
	SEND_SERIES_BIT(R3 @1030 or 1033, 0, nop1(), { WREG = rcreg; if (WantEcho) txreg = WREG; }, BankOverhead(rcreg)); //send red.4; pass thru serial char
//if WREG == RENARD_SYNC
	SEND_SERIES_BIT(R2 @1031 or 1034, 0, nop1(), { ++iochars.bytes[0]; if (EqualsZ) ++iochars.bytes[1]; }, +0); //send red.3; update serial stats
	SEND_SERIES_BIT(R1 @1032 or 1035, 0, EMPTY, { send_count = 18; if (EqualsZ) ++iochars.bytes[2]; }, +0); //send red.2; remember to interrupt animation later
//	ABSDIFF_WREG(SerialPassthru, NoSerialPassthru);
trailer:
//#if CLOCK_FREQ <= 20 MHz //not enough instr for loop control with variable bit @5 MIPS, so use 0 hard-coded bit
//CAUTION: BoostC broken again; need "!= 0" here
	for (;;) { SEND_SERIES_BIT(R0 or B2..B0 @1033 or 1036, IIF(CLOCK_FREQ <= 20 MHz, 0, FROMRAM) != 0, EMPTY, { if (CLOCK_FREQ <= 20 MHz) nop1(); else rl_nc(portX); if (--send_count) continue; nop1(); }, +0); break; } //send remaining bits
//	ABSGOTO(NodeTrailer);
//	IFPORTA(porta = 0, EMPTY);
//	IFPORTBC(portbc = 0, EMPTY);
	portX = 0;
	front_panel(~FP_NODEIO);
	if (ALWAYS) return;

ioprep:
//	if (!iobusy) return; //don't need to send; nodes are persistent
//	iobusy = FALSE;
	if (ExtClockFailed && (MAXINTOSC_FREQ < 18 MHz /*CLOCK_FREQ < 32 MHz*/)) { PROTOCOL_ERROR(PROTOERR_SPEED); return; } //not running fast enough for this type of node I/O
//pre-load first node:
//do this before entering time-critical null node section, so there's more time available
//NOTE: palette arithmetic assumes that fsrL will never wrap
//	palette_shift(SHIFT_BEFORE); //compensate for palette shift-out logic; do this each time so caller can manipulate unaltered palette
	send_count = NodeBytes; //#bytes to send / RAM_SCALE; each byte is 2 nodes @4bpp
//	ABSDIFF_WREG(SerialPassthru, NoSerialPassthru);
//	if (protocol_inactive) ABSDIFF_WREG(SerialCancel, NoSerialPassthru); //if running demo, cancel I/O immediately and start handling protocol
//	jumpofs = WREG; //pre-load jump offset for serial port pass thru or demo I/O cancel (used in 3-way jump); avoids conditional branching overhead later
	nodeptrL = NODE_ADDR(0, 4 BPP); nodeptrH = NODE_ADDR(0, 4 BPP)/ 0x100; octnode &= ~0xe0; //initialize first node ptr
//6 instr to set fsrL to palette entry
	fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP)/ 0x100; swap(indf); fsrL = indf & 0x0F; //get palette entry# for first (even) node; 4 bpp
	palinx2ofs_WREG(fsrL); /*fsrL += WREG; WREG += PALETTE_ADDR(0, 4 BPP); fsrL += WREG*/; fsrH/*9*/ = PALETTE_ADDR(0, 4 BPP) / 0x100; //set palette entry address; offset is 3 * palette entry#
	swap(indf); rr_nc_WREG(indf /*pal R high*/); //pre-load WREG+Carry with first red.?3210765+4 bits; CAUTION: palette entry is now swapped, needs to be unswapped again
//start node I/O:
//#warning "fix this:"
	pclath = 0; //kludge; compensate for earlier gotocs
	BANKSELECT(portX); //pre-select bank to prevent compiler from inserting bank selects below (interferes with bit I/O timing)
	front_panel(FP_NODEIO);
	if (ALWAYS) goto next_node; //kludge: prevent "never returns" compiler warning and dead code removal
#undef SEND_SERIES_BIT
}
#else //def WANT_WS2811
 #define send_ws2811_series_4bpp()  PROTOCOL_ERROR(PROTOERR_EXCLUDED)
#endif //def WANT_WS2811


//parallel nodes:
//want to allow > 16 max combinations; 3 nibbles/node group is marginal, and is likely too restrictive for larger PICs (1K RAM)
//1 byte/node group is simplest, but wastes space for smaller PICs (not that many palette entries are possible)
//6 bits/node group allows 64 palette entries (up to 64*12 = 768 bytes, good for larger PICs), and 16 palents = 16*12 = 192 bytes, leaving 48 bytes = 64*4 = 256 nodes
// 11111122 22223333 33444444 vs. 44111111 44222222 44333333 vs. 11111144 22222244 33333344
//5 bits/node group allows 32 palette entries (up to 32*12 = 384 bytes), and 16 palents with 48 bytes = 24*3*4 = 288 nodes or 15 palents = 180 bytes + 60/2*3*4 = 360 nodes
// x1111122 22233333 vs. x3311111 33322222 vs. x1111133 32222233

//NodeList: assumes 4 bpp; sender needs to pre-send new combination, or firmware needs to build new parallel palette entry if requested combo not exist
//parallel (multiple I/O pins): main purpose is to shorten refresh time < 50 msec, while preserving overall node capacity
//with 1 nibble/4 parallel nodes: max palette = 16 * 12 = 192 bytes, 384 nodes = 48 bytes, leaves 192 bytes palette = 192/12 = 16 palette entries
//with 1 byte/4 parallel nodes: max palette = 256 * 12 = 3K bytes, 384 nodes = 96 bytes, leaves 144 bytes palette = 144/12 = 12 palette entries

//**with 3 nibbles/4 parallel nodes: max palette = 16 * 4 = 64 bytes, leaves 176/1.5 = 117 parallel node groups = 117 * 4 = 468 total nodes
//with 2 bytes/4 parallel nodes: max palette = 32-64 * 4 = 128-256 bytes, leaves 112/2 = 56 parallel node groups = 56 * 4 = 224 total nodes
//                               12 palette entries = 12 * 4 = 48 bytes, leaves 192/2 = 96 parallel node groups = 96 * 4 = 384 total nodes
//with 3 bytes/4 parallel nodes: max palette = 256 * 4 = 1K bytes, 384 nodes = 96 * 3 bytes = 288 bytes
//6:1 => 4 parallel nodes takes 2 bytes
//palette map: 1 nibble => 2 bytes: 1 pal bank sel + 3 * 5 bit palette indexes => 4 parallel * 24 bits = 12 bytes

//256 bytes RAM: 384 nodes across 4 I/O pins = 96 nodes/pin; 96 * 3 * 1/2 byte = 144 bytes, leaves 96 = 24 * 4 for palette

//1 byte per opcode (extended palette): 4bppp implemented; 4x50 ct => 3*50 bytes data + 90 available for palette (8 entries)
//1 nibble per opcode (limited palette): 4bppp TODO; 8x50 ct => 
//since <= 400 nodes can be comfortably handled in series (<= 12 msec refresh), this is only useful for larger numbers of nodes with bigger PICs
//4x256 nodes => 3*256 bytes data + 256 available for palette (21 entries)
#ifdef WANT_WS2811
non_inline void send_ws2811_parallel4_4bpp(void)
{
//#define SEND_PARALLEL_BITS  SEND_PARALLEL_BITS_WS2811 //NOTE: one port only (3 - 8 bits)
//#define SEND_PARALLEL_BITS  SEND_PARALLEL_BITS_WS2811_8MIPS_ONEPORT //(val, varbits, spare7or8)
#define SEND_PARALLEL_BITS  SEND_PARALLEL_BITS_WS2811//(comment, val, varbits, spare6or8_8mips, want_extra)
	volatile uint8 send_count @adrsof(PROTOCOL_STACKFRAME3) + 0; //#bytes to send; count is more efficient than address compare (bytes not contiguous: 0x50..0x70, 0xa0..0xf0, 0x120..0x170); NOTE: units are byte pairs for 512 RAM or byte quads for 1K RAM
	volatile uint8 bit_loop @adrsof(PROTOCOL_STACKFRAME3) + 1; //#bytes to send; count is more efficient than address compare (bytes not contiguous: 0x50..0x70, 0xa0..0xf0, 0x120..0x170); NOTE: units are byte pairs for 512 RAM or byte quads for 1K RAM
	volatile uint8 svnode @adrsof(PROTOCOL_STACKFRAME3) + 2; //#bytes to send; count is more efficient than address compare (bytes not contiguous: 0x50..0x70, 0xa0..0xf0, 0x120..0x170); NOTE: units are byte pairs for 512 RAM or byte quads for 1K RAM
	volatile uint8 octnode @adrsof(statebit_0x20)/8; //3-bit node octet counter; together with send_count this gives an 11-bit node counter, which is enough for 1K bytes @4 bpp; NOTE: must be upper bits so Carry will not affect other bits; kludge: BoostC doesn't support bit sizes
	volatile bit serial_waiting @BITADDR(adrsof(statebit_0x10));
	volatile uint8 inbuf @adrsof(PROTOCOL_STACKFRAME3) + 1; //adrsof(LEAF_PROC); //for serial passthru; CAUTION: mutually exclusive with bit_loop
//	volatile bit palbank2 @BITADDR(adrsof(statebit_0x10)); //CAUTION: reused
	ONPAGE(IOH_PAGE); //put on same page as protocol handler to reduce page selects

//	if (!iobusy) return; //don't need to send; nodes are persistent
//	iobusy = FALSE;
#ifdef PORTC
 #undef portX
 #define portX  portc
#endif

#if CLOCK_FREQ >= 32 MHz
//	fsrL_palette = PALETTE_ADDR(0, 4 BPP); fsrH_palette = PALETTE_ADDR(0, 4 BPP) / 0x100;
//	indf_autopal = 0; indf_autopal = 0; indf_autopal = 0; indf_autopal = 0; //indf_autopal = 0; indf_autopal = 0; indf_autopal = 0; indf_autopal = 0;
//	indf_autopal = ~0; indf_autopal = ~0; indf_autopal = ~0; indf_autopal = ~0; //indf_autopal = ~0; indf_autopal = ~0; indf_autopal = ~0; indf_autopal = ~0;
//	send_count = NodeBytes >> 1; //#nodes/4; node groups are 4 wide here
//	fsrL_nodes = NODE_ADDR(0, 4 BPP); fsrH_nodes = NODE_ADDR(0, 4 BPP) / 0x100;
//	for (;;)
//	{
//		indf_autonode = 1; indf_autonode = 0; indf_autonode = 0; //green (R+G are swapped)
//		if (!--send_count) break;
//	}
//NodeBytes  ram sc  #node grps (4 parallel)  #node grp-pairs
//   #n/2       1       NB/2 == #n/4            NB/4 == #n/8
//   #n/4       2       NB == #n/4              NB/2 == #n/8
//   #n/8       4       NB*2 == #n/4            NB == #n/8
	send_count = NodeBytes; //#bytes to send / RAM_SCALE = #nodes/2/RAM_SCALE; each byte is 2 nodes @4bpp
	if (RAM_SCALE < 2) { send_count >>= 1; if (Carry) ++send_count; } //convert to node pairs
//	if (RAM_SCALE < 2) { send_count >>= 1; if (Carry) ++send_count; }
//	if (RAM_SCALE < 4) { send_count >>= 1; if (Carry) ++send_count; }
	fsrL_node = NODE_ADDR(0, 4 BPP); fsrH_node = NODE_ADDR(0, 4 BPP) / 0x100; octnode &= ~0xe0; //initialize first node ptr
//NOTE: palette arithmetic assumes that fsrL will never wrap
//p111 1122, 2223 3333 => p111 1100, p222 2200, p333 3300
	WREG = indf_node & (0x3F << 2);/* if (PALETTE_ADDR(0, 4 BPP) & 0xFF)*/ WREG += PALETTE_ADDR(0, 4 BPP); fsrL_parapal = WREG; fsrH_parapal = PALETTE_ADDR(0, 4 BPP) / 0x100; //get x11111xx bits from first byte of pair
//	bit_loop = 8/2 -1;
//node groups are packed as byte pairs:
// pRRR RRGG, GGGB BBBB => pRRR RR00, pGGG GG00, pBBB BB00
// p = palette bank select, RRRRR = palette index for red, GGGGG = palette index for green, BBBBB = palette index for blue
//palette index * 4 = offset into palette space for a 4-byte palette entry (8 bits for 4 parallel nodes); takes 3 entries per node group
// 1 node group (4 parallel nodes) == 2 bytes == 4 * 24 RGB palette bits == 12 bytes => 6:1 compression
//256 bytes:
//  120 palette bytes + 120 node bytes = 30 palents + 60 node groups (240 nodes)
//  80 palette bytes + 160 node bytes = 20 palents + 80 node groups (320 nodes)
//  48 palette bytes + 192 node bytes = 12 palents + 96 node groups (384 nodes)
//1K RAM:
//  128 palette bytes + 880 node bytes = 32 palents + 440 node groups (1760 nodes)
//  80 palette bytes + 928 node bytes = 20 palents + 464 node groups (1856 nodes)
//  48 palette bytes + 960 node bytes = 12 palents + 480 node groups (1920 nodes)
	BANKSELECT(portX); //pre-select bank to prevent compiler from inserting bank selects below (interferes with bit I/O timing)
	front_panel(FP_NODEIO);
	for (;;) //each iteration sends out 4 nodes in parallel (1 node group), which occupies 2 bytes of node space (3 5-bit color indices + bank#)
	{
//send red (green) and handle serial pass-thru:
		SEND_PARALLEL_BITS(R7 hi @1034, FROMRAM, { swap_WREG(indf_parapal); portX = WREG; }, { nop1(); serial_waiting = FALSE; if (IsCharAvailable) serial_waiting = TRUE; }, notBankOverhead(pir1)); //send high nibble from palette entry; check if serial char available
		SEND_PARALLEL_BITS(R6 lo @1035, FROMRAM, portX = indf_parapal_auto, { nop2(); if (HasFramingError) serial_waiting = FALSE; }, notBankOverhead(rcsta)); //send low nibble; filter frame errors
		SEND_PARALLEL_BITS(R5 hi @1036, FROMRAM, { swap_WREG(indf_parapal); portX = WREG; }, { WREG = indf_autonode; WREG &= 3; svnode = WREG; if (serial_waiting) goto serial_passthru; nop1(); }, +0); //send high nibble from palette entry; start getting next palette index and branch for serial passthru
no_serial:
		SEND_PARALLEL_BITS(R4 lo @1037, FROMRAM, portX = indf_parapal_auto, { nop2(); nop4(); }, HAS_NOP4); //send low nibble
		SEND_PARALLEL_BITS(R3 hi @1038, FROMRAM, { swap_WREG(indf_parapal); portX = WREG; }, { nop2(); nop4(); }, HAS_NOP4); //send high nibble
		SEND_PARALLEL_BITS(R2 hi @1039, FROMRAM, portX = indf_parapal_auto, { nop2(); nop2(); if (ALWAYS) goto serial_done; }, ALTERS_PCL); //send low nibble
serial_passthru:
		SEND_PARALLEL_BITS(R4 lo @1040, FROMRAM, portX = indf_parapal_auto, { nop2(); inbuf = rcreg; }, notBankOverhead(rcreg)); //send low nibble; read serial char
		SEND_PARALLEL_BITS(R3 hi @1041, FROMRAM, { swap_WREG(indf_parapal); portX = WREG; }, { nop1(); WREG = inbuf; BANKSELECT(txreg) /*pre-select for shorter btf branch*/; if (WantEcho) txreg = WREG; }, notBankOverhead(txreg)); //send high nibble; echo serial char
		SEND_PARALLEL_BITS(R2 lo @1042, FROMRAM, portX = indf_parapal_auto, { nop1(); ++iochars.bytes[0]; if (EqualsZ) ++iochars.bytes[1]; if (EqualsZ) ++iochars.bytes[2]; }, +0); //send low nibble; update serial stats
serial_done:
//p111 1122, 2223 3333 => p111 1100, p222 2200, p333 3300
		SEND_PARALLEL_BITS(R1 hi @1043, FROMRAM, { swap_WREG(indf_parapal); portX = WREG; }, { svnode |= indf_node & 0xE0; if (fsrL_parapal & 0x80) svnode |= 4; swap(svnode); }, +0); //send high nibble from palette entry; finish collecting next palette index
		SEND_PARALLEL_BITS(R0 lo @1044, FROMRAM, portX = indf_parapal_auto, { rl_WREG(svnode); WREG += PALETTE_ADDR(0, 4 BPP); fsrL_parapal = WREG; bit_loop = 3; }, +0); //send low nibble; set ptr to next palette entry
//send green (red):
		for (;;)
		{
			SEND_PARALLEL_BITS(G7-5-3 hi @1045, FROMRAM, { swap_WREG(indf_parapal); portX = WREG; }, { nop2(); nop4(); }, HAS_NOP4); //send high nibble
			SEND_PARALLEL_BITS(G6-4-2 lo @1046, FROMRAM, portX = indf_parapal_auto, { nop3(); if (--bit_loop) continue; nop1(); }, +0); //send low nibble
			break;
		}
		SEND_PARALLEL_BITS(G1 hi @1047, FROMRAM, { swap_WREG(indf_parapal); portX = WREG; }, { nop2(); svnode = indf_autonode; bit_loop = 3; }, +0); //send high nibble
		SEND_PARALLEL_BITS(G0 lo @1048, FROMRAM, portX = indf_parapal_auto, { rl_nc(svnode); rl_nc_WREG(svnode); WREG &= 0x1F << 2; if (fsrL_parapal & 0x80) WREG |= 0x80; fsrL_parapal = WREG; }, +0); //send low nibble; set next palette entry ptr
//send blue, check for eof:
		for (;;)
		{
			SEND_PARALLEL_BITS(B7-5-3 hi @1049, FROMRAM, { swap_WREG(indf_parapal); portX = WREG; }, { nop2(); WREG = 0; if (bit_loop & 2) WREG = IIF(RAM_SCALE > 2, 0x40, 0x80) /*1<<(8-RAM_SCALE_BITS)*/; octnode += WREG; }, +0); //send high nibble; update node prescalar; CAUTION: Carry must be preserved here
			SEND_PARALLEL_BITS(B6-4-2 lo @1050, FROMRAM, portX = indf_parapal_auto, { if (Carry) --send_count; nop1(); if (--bit_loop) continue; nop1(); }, +0); //send low nibble; update remaining count
			break;
		}
		SEND_PARALLEL_BITS(B1 hi @1051, FROMRAM, { swap_WREG(indf_parapal); portX = WREG; }, { nop2(); if (!send_count) break; nop1(); }, +0); //send high nibble; check for eof
		SEND_PARALLEL_BITS(B0 lo @1052, FROMRAM, portX = indf_parapal_auto, { WREG = indf_node & 0x3F << 2;/* if (PALETTE_ADDR(0, 4 BPP) & 0xFF)*/ WREG += PALETTE_ADDR(0, 4 BPP); fsrL_parapal = WREG; continue; }, ALTERS_PCL); //send low nibble; go start next node pair
	}
//finish off last node (no setup for next node):
	SEND_PARALLEL_BITS(B0 eof @1053, FROMRAM, portX = indf_parapal_auto, { nop2(); nop4(); }, HAS_NOP4); //send low nibble
//	ABSGOTO(NodeTrailer); //re-use trailer logic from series node I/O; no need to repeat it all here since it's the same
//	IFPORTA(porta = 0, EMPTY);
//	IFPORTBC(portbc = 0, EMPTY);
	portX = 0;
	front_panel(~FP_NODEIO);
#else
	/*if (ExtClockFailed)*/ { PROTOCOL_ERROR(PROTOERR_SPEED); return; } //not running fast enough for this type of node I/O
#endif //CLOCK_FREQ >= 32 MHz
#undef portX
#define portX  porta

//8 parallel strings can be driven; however, Renard-PX1 PCB only makes C0 - C3 available for other uses, so just implement 4 parallel strings for now

//1600 series nodes == 48 msec refresh; 1600x4 == 6400 parallel quad-nodes
//800 series nodes == 24 msec refresh; 800x4 = 3200 parallel quad-nodes
//384 series nodes ~= 11.5 msec refresh; 384x4 == 1536 parallel quad-nodes
//256 series nodes ~= 7.7 msec refresh; 256x4 == 1K parallel quad-nodes
//these numbers are good enough for first implementation

//parallel palette entries contain 24 bits for 4 nodes == 12 bytes each; each group of 4 parallel nodes takes 1 byte (4*24:1*8 == 12:1 compression)
//384 series nodes only takes 11.5 msec to refresh, so there's no advantage to running <= 384 nodes in parallel
//a 256-byte PIC can drive 384 series nodes, so there's also no advantage to running parallel nodes with <= 256 bytes
//therefore parallel strings are targetted at > 384 nodes on PICs with > 256 bytes
//256 bytes ram (16f688, 12f1840) allows up to 1920 nodes (480x4) == max 240 node bytes (480x1/2 byte per quad node) vs. max 240 palette bytes (20x12)
//384 nodes (96x4) == 96 node bytes + max 144 palette bytes (12x12) == 240 bytes, useless case (16f688, 12f1840)
//384 bytes ram (16f1827) allows up to 2208 nodes (552x4) == max 368 node bytes (552x2/3 bytes per quad-node) vs. max 368 palette bytes (~30x12)
//   or 1472 nodes (368x4) == max 368 node bytes
//1K bytes ram (16f1825) allows up to 4032 nodes (1008x4) == 1008 node bytes vs. max 1K palette bytes (84x12)
//2K nodes (512x4) == 512 node bytes + max 512 palette bytes (~42x12) == 1008 bytes (16f1825)

//obsolete:
//parallel palette entries contain 8 bits for 4 parallel nodes == 4 bytes each; each group of 4 parallel nodes takes 2 bytes:
//3x5 bit palette entry + 1 spare bit (palette bank select?) == 2 bytes/4 nodes == 6:1 compression (4*24:8*2)
//256 nodes (64x4) == 128 node bytes + max 112 palette bytes (28x4) == 240 bytes (16f688, 12f1840)
//768 nodes (192x4) == 192 node bytes + max 48 palette bytes (12x4) == 240 bytes
//2K nodes (512x4) == 512 node bytes + max 512 palette bytes (128x4) == 1008 bytes (16f1825)
//3520 nodes (880x4) == 880 node bytes + max 128 palette bytes (32x4) == 1008 bytes (16f1825)

//#else
//not quite enough instr bandwidth to use palette; would be restricted to 10x8 == 80 parallel nodes with no palette, so not worth doing
//	/*if (ExtClockFailed)*/ { inc_nowrap(protocol_errors); return; } //not running fast enough for this type of node I/O
//	PROTOCOL_ERROR(0x11); //not running fast enough for this type of node I/O; just ignore one byte and continue processing packet
//	if (ExtClockFailed) { PROTOCOL_ERROR(0x10); return; } //not running fast enough for this type of node I/O
//#endif // CLOCK_FREQ >= 32 MHz
#undef SEND_PARALLEL_BITS
}
#else //def WANT_WS2811
 #define send_ws2811_parallel4_4bpp()  PROTOCOL_ERROR(PROTOERR_EXCLUDED)
#endif //def WANT_WS2811


#ifdef WANT_GECE
//wait for 1/3 of bit time:
//bit time is 30 usec and divided into 3 parts:
//  ______          ______________
//    all \    0   /  1   /  all  \
//         \______/______/         \_
//all bits start with high-to-low transition
//"0" bits rise after 1/3 of the bit time; "1" bits rise after 2/3 of the bit time
//all bits end with a low-to-high transition, which is used as a data clock
//If const bits are used, compiler should optimize out the false cases, resulting in fewer instructions.
//Should not be inlined; called many times from within GECE_thread.
//This adds 4 instr overhead (call + return), but cuts down a lot on code space.
//3n + 3 instr (excl stmt):
#define WANT_TIMER  TRUE
#define NO_TIMER  FALSE
#define GECE_WAITBIT(stmt /*, idle_before, idle_after*/)  \
{ \
	/*if (idle_before)*/ FinishWaiting4Timer(Timer0, EMPTY); /*wait(0, EMPTY)*/; /*wait for previous third to finish*/ \
	stmt; \
	/*if (idle_after) {*/ StartTimer(Timer0, 10 usec, CUMULATIVE_BASE); /*} wait(10 usec - 3, break)*/; /*start next third; relative adjustment to prevent accumulation of timing errors*/ \
}
//	if (idle_after) { tmr0 = TimerPreset(10 usec, 3, Timer0); Timer0Wrap = FALSE; } /*wait(10 usec - 3, break)*/; /*start next third*/
//	if (idle_after) { tmr0 += TimerPreset(10 usec, 0, Timer0, CLOCK_FREQ); Timer0Wrap = FALSE; } /*wait(10 usec - 3, break)*/; /*start next third; relative adjustment to prevent accumulation of timing errors*/


//RGB => IBGR mapping:
//can this be done in-PIC? (to make palette manip xparent to sender/caller)
// I = max(R, G, B)
// B,G,R = 15 * x/I
// I = max(0xCC)
// #FFFFFF => CC,F,F,F
// #000000 => 00,x,x,x
// #CC1004 => CC,0,1,F
// #FF8040 => CC,4,8,F


//#ifdef WANT_GECE
// #undef WANT_GECE
// non_inline void gece_init(void) { ONPAGE(0); }
//#endif


//memory layout during GECE node I/O:
//8*50 = 400 nodes takes 200 bytes
//16*2.5 = 40 bytes full palette
//8*.5 = 4 bytes node ptr cache
//8*.5 = 4 bytes node data cache
//full bitmap = 50 * 8 = 400 nodes == 200 bytes ~= 39 msec I/O
//4bpp: rcv 4 bytes ~= 176 usec; send 1x8 nodes = 780 usec; => rcv 4.4x tx: rx,rx,rx,rx,tx,rx,rx,rx,rx,tx,... => +4.4-1+4.4-1+4.4-1... => (+50-11)x6 = 234, room for 28x
//1bpp: rcv 6 bytes ~= 264 usec; send 1x8 nodes = 780 usec; => rcv 3x tx: rx,rx,rx,tx,rx,rx,rx,tx,... => +3-1+3-1+3-1... 17x, room for 28x
//NOTE: NodeList for GECE must be 50 or less unique addresses (39 msec I/O time) for 50 msec frame rate
//;;;;8 bytes over => steal 4 palette entries
//;;;;for ramsc 1, 8*50 nodes, getbmp wraps, nodelist overwrites last 4 palents
//0x20 palette: II,BG,R-,II,BG,R-,II,BG,R-,II,BG,R-,II,BG,R-,II
//0x30:         BG,R-,II,BG,R-,II,BG,R-,II,BG,R-,II,BG,R-,II,BG
//0x40:         R-,II,BG,R-,II,BG,R-,II,BG,R-,II,BG,R-,II,BG,R-
//0x50 cache:   dd,dd,dd,dd,dd,dd,dd,dd,pp,pp,pp,pp,pp,pp,pp,pp
//0x60 fifo:    xx,xx,xx,xx,xx,xx,xx,xx,xx,xx,xx,xx,xx,xx,xx,xx
//0xa0: fifo:   NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN
// :
//0xe0: fifo:   NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN
//0x120: fifo:  NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN
// :
//0x160: fifo:  NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN,NN
//fifo contains 2 types of entries: control and node data
//node data is a byte with 0x40 (0x80 = eof marker), followed by 4 bytes of palette indexes (for 8 nodes @4 bpp)
//control data is an address byte (node start address <= 0x3F), followed by an adresss mask
//entries are never split; this reduces bank gap/wrap checking overhead
//examples:
//  ramsc 1, set nodes starting at 17: 0x02,0xFF,NN,NN,NN,NN, 0x40,NN,NN,NN,NN, 0xC0,NN,NN,NN,NN
//  ramsc 1, set all + node list: 0x3F,0xFF,NN,NN,NN,NN, 0x08,0x80,NN,NN,NN,NN, 0x0
//entries are multiples of 5 bytes to reduce bank gap/wrap overhead
//GetBitmap: len, clr/append+stadrs (gece only), first node grp, (start io), ...
//NodeList: clr/append+stadrs (gece only), palinx, nodes, ..., (start io)
//FIFO:
//set stadrs,
//mode=00:adrs(1..62),pos(0..7),color(0..15) = 2 bytes/node (room for 96 entries)
//mode=11:adrs(1..62), 8 colors (4 bytes) = 5 bytes/node grp (room for 38 entries)
//mode=01:adrs(1..62),
//with 256 bytes RAM on smaller devices, there is not enough to hold 8*50 = 400 nodes (200 bytes) + 16 palette entries (16*3 = 48 bytes) + 8 byte node cache + 16 bytes working storage = 256+16 bytes total
//there are 3 ways to handle this:
//- if <= 10 palette entries are needed, the extra nodes can be stored in unused palette space and everything fits statically; this approach is used by demo/test mode, and might be used for live playback mode
//- overlapped I/O while a full bitmap is being received from sender (and > 10 palette entries); node space acts as a FIFO and everything fits (dynamically over time); this is used for live playback
//- for inverted lists (set all + node lists) and > 10 palette entries; send the first 32 nodes separately and wait for them to flush before sending the rest; requires packet interleave, and can be used for live playback; TODO: INCOMPLETE
//;;;;NOTE: first 16 nodes are interleaved with last half-byte of palette entries, and palette bytes grouped by color element; this happens as data received
//0x20..0x2F = first 8 bits of each IBGR palette entry 0..F (I)
//0x30..0x3F = next 8 bits of each IBGR palette entry 0..F (B,G)
//0x40..0x4F = last 4 bits of each IBGR palette entry 0..F (R) in upper 4 bits, nodes 1..16 in lower 4 bits (decreasing order), interleaved
//0x50..0x6F = nodes 337..400 (decreasing order)
//0xa0..0xEF = nodes 177..336 (decreasing order)
//0x120..0x16F = nodes 17..176 (decreasing order)
//;;;;third palette byte is interleaved with first 16 nodes (palette bits upper 4 bits, nodes lower 4 bits):
//;;;;first 8 nodes are also used as a cache for each subsequent group of 8 nodes (first 8 nodes are overwritten)
//extra data used by gece node I/O background thread:

//extra data used by gece node I/O background thread:
//NOTE: there is not enough space to hold the last 48 nodes statically, but overlapped I/O frees up enough space for them as FIFO drains
//volatile struct
//{
//NOTE: there is not enough space to hold the last 48 nodes statically, but overlapped I/O frees up enough space for them as FIFO drains
#define GECE_FIFO_ADDR  PALETTE_ADDR(16, 4 BPP) //place gece fifo info immediately after palette; NOTE: reduces amount of node space available
volatile uint8 gece_ioresume @MakeBanked(adrsof(GECE_FIFO) + 0); //background I/O thread state (consolidate jump tables); CAUTION: must align with global var of same name
volatile uint8 gece_cmd_state @MakeBanked(adrsof(GECE_FIFO) + 1); //which fifo command is being executed
//kludge: reuse bottom 4 bits of cmd_state since they will normally be 0; these are reset automatically when new fifo command is read
 volatile bit gece_new_ptrs @MakeBanked(adrsof(GECE_FIFO) + 1).2;
 volatile bit gece_reload_cache @MakeBanked(adrsof(GECE_FIFO) + 1).1;
 volatile bit gece_demo_skip @MakeBanked(adrsof(GECE_FIFO) + 1).0; //@BITADRS(adrsof(statebit_0x20)); //kludge: slip alternating nodes, to be consistent with non-gece demo behavior
volatile uint8 gece_node_color @MakeBanked(adrsof(GECE_FIFO) + 2); //node color to set
volatile uint8 gece_node_adrs @MakeBanked(adrsof(GECE_FIFO) + 3); //adrs of next node to send
volatile uint8 gece_adrs_mask @MakeBanked(adrsof(GECE_FIFO) + 4); //which parallel strings to send to
volatile uint8 gece_send_count @MakeBanked(adrsof(GECE_FIFO) + 5); //#nodes to send
volatile uint8 gece_outbuf @MakeBanked(adrsof(GECE_FIFO) + 6); //@adrsof(LEAF_PROC); //CAUTION: not persistent across calls
volatile uint8 gece_tmr0_preset /*unused*/ @MakeBanked(adrsof(GECE_FIFO) + 7);
volatile uint8 gece_rxptrL @MakeBanked(adrsof(GECE_FIFO) + 8), gece_rxptrH @MakeBanked(adrsof(GECE_FIFO) + 9); //fifo head and tail ptrs to data (quad-bytes)
volatile uint8 gece_txptrL @MakeBanked(adrsof(GECE_FIFO) + 10), gece_txptrH @MakeBanked(adrsof(GECE_FIFO) + 11);
volatile uint8 gece_svptrL @MakeBanked(adrsof(GECE_FIFO) + 12), gece_svptrH @MakeBanked(adrsof(GECE_FIFO) + 13); //save caller's fsr (might be different from committed rxptr)
volatile uint8 gece_unused2 @MakeBanked(adrsof(GECE_FIFO) + 14);
volatile uint8 gece_node_cache[8] @MakeBanked(adrsof(GECE_FIFO) + 16); //cached palette byte for current node group
volatile uint8 gece_node_ptrs[8] @MakeBanked(adrsof(GECE_FIFO) + 24); //cached ptrs to palette entries for current node group
//	uint8 bit33[1]; //start of bit array
//		uint8 send_count; //#nodes to send (fifo size)
//		volatile uint8 repeat @MakeBanked(PALETTE_ADDR(16, 4 BPP) + 23); //#nodes to send in next block
//} gece_fifo @MakeBanked(adrsof(GECE_FIFO)); //bit array immediately follows
//volatile uint2x8 gece_txptr @MakeBanked(adrsof(GECE_FIFO) + 0); //fifo head ptr
#define GECE_FIFO_END_ADDR  (GECE_FIFO_ADDR + 32) //;;;;NOTE: doesn't include fifo entry bit array
//volatile uint8 gece_palette_not_full @MakeBanked(PALETTE_ADDR(10, 4 BPP)); //palette_bytes[3*10 + 0]; //first byte of 10th entry used to detect empty space for node overflow
//#define GECE_FIFO_MAPLEN  ((TOTAL_BANKRAM - (3<<4) - (adrsof(GECE_FIFO_END) - adrsof(GECE_FIFO))) * 8 / 33) //#entries fifo can hold; each entry is 33 bits (a quad-byte + 1 control bit); 256 RAM => 40 entries, 1K RAM => 226 entries



//check if GECE I/O is enabled:
//status.Z holds the results of the compare
volatile bit IsGece @adrsof(STATUS).Z; //flag returned to caller; must be checked immediately or it will be lost
inline void is_gece(void)
{
	swap_WREG(NodeConfig); WREG ^= RENXt_GECE(DONT_CARE); WREG &= 0x0F & ~1; //WREG = NodeConfig >> 4; //split up expr to avoid BoostC generating a temp
//	bitcopy(EqualsZ, gece);
}

//fifo is empty when txput == rxptr; caller must check
// #define IsGeceFifoEmpty(/*value*/)   (gece_txptr.bytes[2] == gece_rxptr.bytes[2]) //4-5 instr //((gece_txptr.bytes[0] == gece_rxptr.bytes[0]) && (gece_txptr.bytes[1] == gece_rxptr.bytes[1])) //(gece_outptr == gece_inptr)
#define IsGeceFifoEmpty(/*value*/)  ((gece_txptrL == gece_rxptrL) && ((RAM_SCALE < 2) || (gece_txptrH == gece_rxptrH))) //4-8 instr?

//initialize fifo to empty:
non_inline void gece_init(void)
{
	ONPAGE(LEAST_PAGE); //put on same page as other initialization; avoids cross-page chained goto problem; //PROTOCOL_PAGE); //put on same page as protocol handler to reduce page selects

	is_gece();
	if (!IsGece) return;
//	IFPORTA(porta = ~0, EMPTY); IFPORTBC(portbc = ~0, EMPTY); //also set initial port state (first bit starts with high-to-low transition)
	gece_rxptrL = NODE_ADDR(0, 4 BPP); //ptr to first available fifo entry (quad-byte data)
	gece_txptrL = NODE_ADDR(0, 4 BPP);
	if (RAM_SCALE > 1)
	{
		gece_rxptrH = NODE_ADDR(0, 4 BPP) / 0x100; 
		gece_txptrH = NODE_ADDR(0, 4 BPP) / 0x100;
	}
//	gece_rxptr.bytes[2] = 0; //next available fifo entry#
//	gece_txptr.bytes[2] = 0;
//	set_all(0); //must send to all nodes to initialize addresses;;;; incoming serial data ignored during init (echo is off)
}

//save fsrH is redundant with top bit of fsrL for low-memory GECE:
#define gece_fsrH_save(ptrH)  if (RAM_SCALE > 1) fsrH_save(ptrH) //save caller's fsrH
#define gece_fsrH_restore(ptrH)  /*restore my fsrH*/ \
{ \
	if (RAM_SCALE > 1) fsrH_restore(ptrH /*& 1*/); \
	else if (NODE_ADDR(0, 4 BPP) / 0x100) fsrH = NODE_ADDR(0, 4 BPP) / 0x100; /*linear addressing with small memory; fsrH will always be the same*/ \
	else fsrH_restore(!fsrL.7 /*& 0x80*/); \
}

//gece fifo commands or state flags:
//NOTE: these values were chosen so that a single btf command can be used rather than xor
//#define GECE_SETALL  0x80 //fifo command
//#define GECE_BITMAP  0x40 //fifo command
//#define GECE_SETNODE  0x20 //fifo command
//#define GECE_NEEDCMD  0x08 //last data bit for node; need to get another command
//#define GECE_GOTDATA  0x04 //already have node data for next command
//#define GECE_NEEDDATA  0x02 //need more node data
//volatile bit gece_cmd_SETALL @MakeBanked(adrsof(GECE_FIFO) + 1).7;
//volatile bit gece_cmd_BITMAP @MakeBanked(adrsof(GECE_FIFO) + 1).6;
//volatile bit gece_cmd_SETNODE @MakeBanked(adrsof(GECE_FIFO) + 1).5;
//volatile bit gece_cmd_NEEDCMD @MakeBanked(adrsof(GECE_FIFO) + 1).3;
//volatile bit gece_cmd_GOTDATA @MakeBanked(adrsof(GECE_FIFO) + 1).2;
//volatile bit gece_cmd_NEEDDATA @MakeBanked(adrsof(GECE_FIFO) + 1).1;
//volatile bit gece_cmd_XDELAY @MakeBanked(adrsof(GECE_FIFO) + 1).0;


//allocate or update new fifo entry for gece command:
//rxptr points to next available fifo entry; fsr points to current entry
#define ALLOC  0 //5 instr
#define COMMIT  1 //21-24 instr
//#define APPEND  2 //? + 20-23 instr
#define CONSUME  3 //14-17 instr
#define gece_new_fifo_entry(cmd)  \
{ \
	if (cmd == ALLOC) { fsrL = gece_rxptrL; gece_fsrH_restore(gece_rxptrH); } /*restore address of previously filled fifo entry; 5 instr*/ \
	else if (cmd == COMMIT) \
	{ \
		gece_add_banked(4, IsDecreasing(NODE_DIRECTION), 0, gece_rxptr); /*advance to next fifo entry; 11-14 instr*/ \
		/*if (cmd != MOVE_NEXT) { gece_rxptrL = fsrL; fsrH_save(gece_rxptrH); }*/ /*save address of new fifo entry; 5 instr*/ \
		/*iobusy = TRUE; gece_ioresume = NodeConfig >> 4*/; /*if (!iobusy) send_gece_4bpp()*/; /*start sending immediately because GECE I/O takes so long*/ \
	} \
	else gece_add_banked(4, IsDecreasing(NODE_DIRECTION), 0, gece_txptr); /*advance to next fifo entry; 11-14 instr*/ \
}
//		if (cmd != MOVE_NEXT) { gece_rxptrL = fsrL; fsrH_save(gece_rxptrH); } /*save address of new fifo entry; 5 instr*/

//lower overhead special-case memory wrap check for gece fifo:
//NOTE: assumes <= 256 bytes RAM for older PICs
#ifdef PIC16X //linear addressing; might be > 256 bytes RAM
//10 - 19 instr @8 MIPS
 #define gece_add_banked(amt, direction_ignored, extra_ignored, ptr)  \
 { \
	fsrL |= 3; /*rewind entry in case only partially used*/ \
	fsrL -= amt; ptr##L -= WREG /*amt*/; /*if (Borrow) fsrH_adjust(fsrH, -1)*/; /*NOTE: it's less expensive to update ptr here at same time as fsr*/ \
	if (Borrow) { --fsrH; --ptr##H; } \
	if ((fsrL < ((adrsof(GECE_FIFO_END) + /*GECE_FIFO_MAPLEN +*/ 3) & 0xFF)) && ((RAM_SCALE < 2) || (fsrH == adrsof(GECE_FIFO_END) / 0x100))) /*wrap; avoid collision with fifo control info*/ \
	{ \
		fsrL = NODE_ADDR(0, 4 BPP); ptr##L = WREG /*NODE_ADDR(0, 4 BPP)*/; \
		fsrH = NODE_ADDR(0, 4 BPP) / 0x100; if (RAM_SCALE > 1) ptr##H = WREG /*NODE_ADDR(0, 4 BPP) / 0x100*/; \
	} \
 }
#else //assume RAM_SCALE < 2 (256 bytes RAM) for older PICs to reduce overhead
//11 or 16 instr @5 MIPS
 #define gece_add_banked(amt, direction_ignored, extra_ignored, ptr)  \
 { \
	fsrL |= 3; /*rewind entry in case only partially used*/ \
	fsrL -= amt; \
	ptr##L -= WREG /*amt*/; /*NOTE: it's less expensive to update ptr here at same time as fsr*/ \
	WREG = fsrL & ~0x80; \
	WREG += 0-SFRLEN; \
	if (Borrow) /*bank wrap; avoid SFR; bypass fifo control info in Bank 0*/ \
	{ \
		fsrL -= GPRGAP; ptr##L -= WREG /*GPRGAP*/; \
		bitcopy(NotBorrow, fsrH); /*Borrow => bank 2 -> 1, !Borrow => bank 1 -> 2*/ \
		/*gece_rxptrH = 0; rl_nc(gece_rxptrH)*/; \
	} \
 }
#endif
//pre-expand to reduce code space:
non_inline void gece_add_banked_4_TRUE_0_gece_rxptr(void)
{
	ONPAGE(PROTOCOL_PAGE); //used by demo + protocol handler
	gece_add_banked(4, TRUE, 0, gece_rxptr); //advance to next fifo entry; 11-14 instr
}
non_inline void gece_add_banked_4_TRUE_0_gece_txptr(void)
{
	ONPAGE(IOH_PAGE); //used by I/O handler only
	gece_add_banked(4, TRUE, 0, gece_txptr); //advance to next fifo entry; 11-14 instr
}
non_inline void gece_add_banked_4_FALSE_0_gece_rxptr(void) //not used; only here to resolve ref
{
	ONPAGE(LEAST_PAGE);
	gece_add_banked(4, FALSE, 0, gece_rxptr); //advance to next fifo entry; 11-14 instr
}
non_inline void gece_add_banked_4_FALSE_0_gece_txptr(void) //not used; only here to resolve ref
{
	ONPAGE(LEAST_PAGE);
	gece_add_banked(4, FALSE, 0, gece_txptr); //advance to next fifo entry; 11-14 instr
}
#undef gece_add_banked
#define gece_add_banked(amt, direction, extra, ptr)  \
{ \
	if (direction) gece_add_banked_##amt##_TRUE_##extra##_##ptr(); \
	else gece_add_banked_##amt##_FALSE_##extra##_##ptr(); \
}
	

//save caller's fsr and restore mine:
//7 instr @5 MIPS, 10 instr @8 MIPS
//NOTE: fsrH is *not* redundant with msb fsrL if caller is executing ReadReg or WriteReg
#define gece_my_fsr(restore_mine)  \
{ \
	gece_svptrL = fsrL; /*gece_*/fsrH_save(gece_svptrH); /*save caller's fsr (GetBitmap might still be in progress); 2 or 5 instr*/ \
	if (restore_mine) { fsrL = gece_txptrL; gece_fsrH_restore(gece_txptrH); } /*restore my fsr; points to current fifo entry (quad-byte); 5 instr*/ \
}


//restore caller's fsr:
//mine was already saved
//7 instr @5 MIPS, 10 instr @8 MIPS
//NOTE: fsrH is *not* redundant with msb fsrL if caller is executing ReadReg or WriteReg
#define gece_caller_fsr(save_mine)  \
{ \
	if (save_mine) { gece_txptrL = fsrL; gece_fsrH_save(gece_txptrH); } /*save ptr to next fifo entry; 2 or 5 instr*/ \
	fsrL = gece_svptrL; /*gece_*/fsrH_restore(gece_svptrH); /*restore caller's fsr (GetBitmap might still be in progress); 5 instr*/ \
}


//caller must check if fifo is empty before calling
inline void gece_setall_begin_read(void) //15 instr
{
//			gece_cmd_state = RENXt_SETALL(0);
	gece_node_ptrs[1] = indf_autonode; //remember which color to set for later
	gece_node_ptrs[3] = WREG;
	gece_node_ptrs[5] = WREG;
	gece_node_ptrs[7] = WREG;
//			gece_adrs_mask = indf_autonode; //_autonode; //1 or all strings
	WREG = ~0; if (!ParallelNodes) WREG = 0x40; //1 string on pin A2
	gece_adrs_mask = WREG; //indf_autonode; //_autonode; //1 or all strings
//			if (!SentNodes) //need to send to each individual node (initial address assignment)
//			{
	gece_node_adrs = indf_autonode; //start address
	gece_send_count = indf; //_autonode; //#nodes to send (on each pin)
	if (gece_demo_skip) ++gece_node_adrs; //kludge: slip alternating nodes, to be consistent with non-gece demo behavior
//				if (RAM_SCALE > 1) gece_send_count <<= 1;
//				if (RAM_SCALE > 2) gece_send_count <<= 1;
//			}
//			else //send to all nodes using broadcast address
//			{
//#warning "TODO: check if adrs 0x3F works with colors and allows subsequent non-full colors"
//				gece_send_count = 1;
//				gece_node_adrs = ~0; //special address for all pixels (0x3F)
//			}
//			fsrL -= 2; //skip remaining portion of entry
//			add_banked(0, IsDecreasing(NODE_DIRECTION), 0);
}
inline void gece_setall_end_read(void) {}
inline void gece_setall_continued(void) //9 instr
{
//don't need data; already got it
//rewind palette ptrs:
//1+8 instr
	WREG = -3;
	gece_node_ptrs[0] += WREG;
	gece_node_ptrs[1] += WREG;
	gece_node_ptrs[2] += WREG;
	gece_node_ptrs[3] += WREG;
	gece_node_ptrs[4] += WREG;
	gece_node_ptrs[5] += WREG;
	gece_node_ptrs[6] += WREG;
	gece_node_ptrs[7] += WREG;
}

inline void gece_bitmap_begin_read(void) //24 instr
{
//			gece_cmd_state = RENXt_BITMAP(0);
	gece_send_count = indf_autonode; //expected count (quad-bytes); could be less
	gece_adrs_mask = indf_autonode;
	gece_node_adrs = indf + 1; //_autonode; //next node address
//			++fifo.ioresume; // = GECE_BITSTATE - 1; //special state to collect node data next time
//		return; //38 instr to here
	gece_new_fifo_entry(CONSUME); //consume this fifo entry, advance to next; 16 instr
}
inline void gece_bitmap_end_read(void) //worst case 15 instr
{
	if (IsGeceFifoEmpty()) { gece_send_count = 1; return; } //ahould be at least one node group following Bitmap command; try to recover gracefully
//finish collecting node data:
//4*3-1 = 11 instr:
	gece_node_ptrs[1] = indf_autonode; //3 instr each for non-extended instr set
	gece_node_ptrs[3] = indf_autonode;
	gece_node_ptrs[5] = indf_autonode;
	gece_node_ptrs[7] = indf; //_autonode;
}
inline void gece_bitmap_continued(void) //worst case 16 instr
{
	if (ParallelNodes) return;
//already got data, so just rearrange it
//advance to next series node:
//14 instr
	gece_node_ptrs[1] = gece_node_ptrs[2];
	gece_node_ptrs[2] = gece_node_ptrs[3];
	gece_node_ptrs[3] = gece_node_ptrs[4];
	gece_node_ptrs[4] = gece_node_ptrs[5];
	gece_node_ptrs[5] = gece_node_ptrs[6];
	gece_node_ptrs[6] = gece_node_ptrs[7];
	gece_node_ptrs[7] = gece_node_ptrs[0];
}

inline void gece_nodelist_begin_read(void) //13 instr
{
//		WREG ^= RENXt_NODELIST(0) ^ /*undo prev:*/ RENXt_BITMAP(0);
//		if (gece_cmd_SETNODE) //EqualsZ)
//		{
//			gece_cmd_state = RENXt_NODELIST(0);
//only one left is NODELIST; don't need to check it
	gece_send_count = 1;
	gece_node_ptrs[1] = indf_autonode; //remember which color to set for later
	gece_node_ptrs[3] = WREG;
	gece_node_ptrs[5] = WREG;
	gece_node_ptrs[7] = WREG;
	gece_adrs_mask = indf_autonode;
	gece_node_adrs = indf; //_autonode; //actual start address (or 0x3F for broadcast)
}
inline void gece_nodelist_end_read(void) {}
inline void gece_nodelist_continued(void) {}


//sort command values for more efficient compares:
#define GECE_COMMAND_MIN  MIN(MIN(RENXt_BITMAP(0), RENXt_NODELIST(0)), RENXt_SETALL(0))
#define GECE_COMMAND_MID  (RENXt_BITMAP(0) ^ RENXt_NODELIST(0) ^ RENXt_SETALL(0) ^ GECE_COMMAND_MIN ^ GECE_COMMAND_MAX)
#define GECE_COMMAND_MAX  MAX(MAX(RENXt_BITMAP(0), RENXt_NODELIST(0)), RENXt_SETALL(0))
//kludge: avoid BoostC macro body length error
#if GECE_COMMAND_MID == RENXt_BITMAP(0)
 #undef GECE_COMMAND_MID
 #define GECE_COMMAND_MID  RENXt_BITMAP(0)
#else
#if GECE_COMMAND_MID == RENXt_SETALL(0)
 #undef GECE_COMMAND_MID
 #define GECE_COMMAND_MID  RENXt_SETALL(0)
#else
#if GECE_COMMAND_MID == RENXt_NODELIST(0)
 #undef GECE_COMMAND_MID
 #define GECE_COMMAND_MID  RENXt_NODELSIT(0)
#else
 #error "[ERROR] Unknown GECE command"
#endif
#endif
#endif


//perform partial processing on a fifo command:
//worst case 9 + ?? instr
//Bitmap has highest overhead and is checked first with the comparison order used below
#define gece_command(phase)  \
{ \
	WREG = gece_cmd_state & 0xF0; WREG += 0 -GECE_COMMAND_MID; /*there are only 3 commands, so a single compare to middle value can check all 3 cases*/ \
	if (Borrow) \
	{ \
		if (RENXt_BITMAP(0) < GECE_COMMAND_MID) gece_bitmap_##phase(); \
		if (RENXt_NODELIST(0) < GECE_COMMAND_MID) gece_nodelist_##phase(); \
		if (RENXt_SETALL(0) < GECE_COMMAND_MID) gece_setall_##phase(); \
	} \
	else if (EqualsZ) \
	{ \
		if (RENXt_BITMAP(0) == GECE_COMMAND_MID) gece_bitmap_##phase(); \
		if (RENXt_NODELIST(0) == GECE_COMMAND_MID) gece_nodelist_##phase(); \
		if (RENXt_SETALL(0) == GECE_COMMAND_MID) gece_setall_##phase(); \
	} \
	else /*if (NotBorrow)*/ \
	{ \
		if (RENXt_BITMAP(0) > GECE_COMMAND_MID) gece_bitmap_##phase(); \
		if (RENXt_NODELIST(0) > GECE_COMMAND_MID) gece_nodelist_##phase(); \
		if (RENXt_SETALL(0) > GECE_COMMAND_MID) gece_setall_##phase(); \
	} \
	/*else unrecognized fifo command*/ \
/*		PROTOCOL_ERROR(0x42);*/ \
/*		gece_cmdstate = RENXt_NOOP;*/ \
}


//gece nodes:
//30 usec per bit * 26 bits == .78 msec/node
//50 ct *.78 == 39 msec, which is a reasonable max for 20 fps (80% bandwidth); max gece string length is 63, but 63 ct * .78 ~= 49 msec, which is too long for 20 fps (98% bandwidth)
//since this runs so long, additional packet bytes *must* be handled during node I/O (unlike WS2811, where max I/O ~= 20 msec)
//max gece string length is 63 nodes:
// if #nodes <= 63, assume one series string
// if #nodes > 63, assume multiple (8 parallel) 50 ct strings
//if only one string is attached there is no harm in sending out 8 parallel strings (other 7 strings will be garbage data)
//therefore the same code can be used for 1 or multiple parallel strings
//palette entries are 2.5 bytes, since 16 * 2.5 + 8 * 50 / 2 == 240, 8x50 strings takes 240 bytes @4bpp
//gece data formats from sender:
// bitmap_4bpp sync,adrs,bmp4bpp, nodes (0,1), (2,3), (4,5), ... up to 400 nodes
//  stored as: (#400,#350),(#300,#250),(#200,#150),(#100,#50), (#399,#349),(#299,#249),(#199,#149),(#99,#49), ..., (#351,#301),(#251,#201),(#151,#101),(#51,#1)
//  - rearranged as received, allows I/O to start while data is still coming
// sync,adrs,set_all,nodelist,...:
// sync,adrs,bitmap_2bpp:
// sync,adrs,bitmap_1bpp:
//#ifdef WANT_GECE
//#define FIFO_COMMAND(val)  ((val) & ~0x80) //top bit must be off
non_inline void send_chplex(void); //fwd ref
non_inline void send_gece_4bpp(void)
{
//	volatile bit parallel @BITADDR(adrsof(statebit_0x20)); //parallel vs. series strings
//	volatile uint8 txptrH_byte @adrsof(statebit_0x80)/8; //upper bit of node ptr; 9-bit adrs allows 1<<9 == 512 bytes linear or 4 * 80 == 320 bytes banked
//	volatile bit txptrH @BITADDR(adrsof(statebit_0x80)); //upper bit of node ptr; 9-bit adrs allows 1<<9 == 512 bytes linear or 4 * 80 == 320 bytes banked
//#ifdef IRP //older PIC has STATUS.IRP instead of BSR
// #if IRP != 7
//  #error "txptrH bit does not align with status.IRP: "IRP"" //xchg assumes bits are in same position
// #endif
//#endif
//	volatile uint8 txptrL @adrsof(PROTOCOL_STACKFRAME3) + 0; //lower byte of current node pair address; must be non-banked to prevent interference with bit loop timing
//	volatile bit rxptrH @BITADDR(adrsof(statebit_0x40)); //upper bit of node ptr; 9-bit adrs allows 1<<9 == 512 bytes linear or 4 * 80 == 320 bytes banked
//	volatile uint8 rxptrL @adrsof(LEAF_PROC); //lower byte of current node pair address; does not need to be persistent across calls; must be non-banked to prevent interference with bit loop timing
//	volatile uint8 send_count @adrsof(PROTOCOL_STACKFRAME3) + 2; //#bytes to send; also used as node address; count is more efficient than address compare (bytes not contiguous: 0x50..0x70, 0xa0..0xf0, 0x120..0x170); NOTE: units are byte pairs for 512 RAM or byte quads for 1K RAM
//	volatile uint8 octnode @adrsof(statebit_0x20)/8; //3-bit node octet counter; together with send_count this gives an 11-bit node counter, which is enough for 1K bytes @4 bpp; NOTE: must be upper bits so Carry will not affect other bits; kludge: BoostC doesn't support bit sizes
//	volatile uint8 svstats[4] @NODE_ADDR(8 -1, 4 BPP); //kludge: move stats to banked RAM
//NOTE: last 2 palette entries are used to hold temp copy of current node group (8 nodes)
//	volatile uint8 nodegrp[5] @PALETTE_ADDR(14, 4 BPP) + 1; //octnode[8/2] @NODE_ADDR(8 -1, 4 BPP); //adrsof(IOCHARS); //holding area for 8 node values; must use starting nodes because later nodes might not have arrived yet and RAM location would be overwritten
//	volatile bit parity @adrsof(statebit_0x20)/8.(8-RAM_SCALE_BITS); //@BITADRS(statebit_0x80); //parity (bottom bit) of current node#; even nodes are in upper nibble, odd nodes are in lower nibble (4 bpp)
//	volatile bit SerialWaiting @BITADRS(adrsof(statebit_0x10)); //serial input waiting; filters out framing errors to avoid sending junk to downstream controllers
//	volatile uint8 morebits @PALETTE_ADDR(16 - divup(16/2, 3), 4 BPP) + 0;
//	volatile uint8 outbuf @adrsof(LEAF_PROC); //CAUTION: not persistent across calls
//	volatile uint8 tmr0_preset @MakeBanked(PALETTE_ADDR(16, 4 BPP) - 21); //calculate timer0 preset once; CAUTION: timing will be trashed if this is overwritten, but that's the trade-off between caching in memory vs. re-calculating each time
	ONPAGE(IOH_PAGE); //must be on same page as send_chplex (for ioresume)

//timing:
//50 nodes * 26 * 30 usec == 39 msec vs. 50 mec/frame == 11 msec slack time
//16 palette entries == 48 bytes ~= 2.1 msec @250k baud; palette can't change while doing node I/O, so SetPalette is part of 11 msec slack time
//400 nodes @4bpp/2bpp/1bpp == 200/100/50 bytes == 8.8/4.4/2.2 msec @250k baud; always shorter than node I/O so start node I/O immediately when node data starts arriving
//sender needs to wait >= 39 + 2.1 msec before sending again
//	SentNodes = TRUE; //don't allow changes to node config after this point
//	volatile bit svsent @BITADDR(adrsof(statebit_0x20));
//	rxptrL = fsrL; bitcopy(fsrH, rxptrH); //when called during GetBitmap opcode, caller's fsr is last rcv ptr and must be preserved
	if (!iobusy) { gece_ioresume = NodeConfig >> 4; } //start new I/O
//continue previous I/O:
//	BANKSELECT(bank1[0]); //kludge: force bank select below
ABSLABEL(ResumeNodeIO); //alternate entry point to resume previous I/O
//NOTE: to reduce overhead, chplex continuation also comes thru here, so that resuming node I/O does not need to go thru 2 separate jump tables
//NOTE: no back select at this point
	WREG = gece_ioresume++; //NodeConfig >> 4;
	INGOTOC(TRUE); //#warning "CAUTION: pclath<2:0> must be correct here"
	pcl += WREG;
	PROGPAD(0); //jump table base address
//first part of send_nodes jump table merged here to avoid an additional jump table:
	JMPTBL64(RENXt_NULLIO) return; //0x00 (0) = null I/O
	JMPTBL64(1) return; //0x01 = reserved for custom front panel
	JMPTBL64(RENXt_PWM(ACTIVE_LOW)) { send_chplex(); return; } //goto resume_chplex; //0x02 = pwm, Common Anode
	JMPTBL64(RENXt_PWM(ACTIVE_HIGH)) { send_chplex(); return; } //goto resume_chplex; //0x03 = pwm, Common Cathode
	JMPTBL64(RENXt_CHPLEX(ACTIVE_LOW)) { send_chplex(); return; } //goto resume_chplex; //0x04 = chplex, Common Anode
	JMPTBL64(RENXt_CHPLEX(ACTIVE_HIGH)) { send_chplex(); return; } //goto resume_chplex; //0x05 = chplex, Common Cathode
	JMPTBL64(RENXt_GECE(SERIES)) goto first_time; //newio; //first_adrs_bit_from_fifo; //0x06
	JMPTBL64(RENXt_GECE(PARALLEL)) goto first_time; //newio; //first_adrs_bit_from_fifo; //0x07
//	JMPTBL64(GECE_BITSTATE - 1) goto masked_adrs_bit; //special case
//additional I/O states for gece:
#define GECE_BITSTATE  RENXt_GECE(PARALLEL)+2
	JMPTBL64(GECE_BITSTATE - 1) goto more_gap; //check_eof; //gap between nodes
	JMPTBL64(GECE_BITSTATE + 0) goto next_bit; //first_adrs_bit; //0x20
	JMPTBL64(GECE_BITSTATE + 1) goto next_bit; //second_adrs_bit; //0x10
	JMPTBL64(GECE_BITSTATE + 2) goto next_bit; //adrs 0x08
	JMPTBL64(GECE_BITSTATE + 3) goto next_bit; //adrs 0x04
	JMPTBL64(GECE_BITSTATE + 4) goto next_bit; //adrs 0x02
	JMPTBL64(GECE_BITSTATE + 5) goto least_bit; //adrs 0x01
	JMPTBL64(GECE_BITSTATE + 6) goto next_bit; //IBGR 0x80000 (first palette byte)
	JMPTBL64(GECE_BITSTATE + 7) goto next_bit; //IBGR 0x40000
	JMPTBL64(GECE_BITSTATE + 8) goto next_bit; //IBGR 0x20000
	JMPTBL64(GECE_BITSTATE + 9) goto next_bit; //IBGR 0x10000
	JMPTBL64(GECE_BITSTATE + 10) goto next_bit; //IBGR 0x08000
	JMPTBL64(GECE_BITSTATE + 11) goto next_bit; //IBGR 0x04000
	JMPTBL64(GECE_BITSTATE + 12) goto next_bit; //IBGR 0x02000
	JMPTBL64(GECE_BITSTATE + 13) goto least_bit; //IBGR 0x01000
	JMPTBL64(GECE_BITSTATE + 14) goto next_bit; //IBGR 0x00800 (second palette byte)
	JMPTBL64(GECE_BITSTATE + 15) goto next_bit; //IBGR 0x00400
	JMPTBL64(GECE_BITSTATE + 16) goto next_bit; //IBGR 0x00200
	JMPTBL64(GECE_BITSTATE + 17) goto next_bit; //IBGR 0x00100
	JMPTBL64(GECE_BITSTATE + 18) goto next_bit; //IBGR 0x00080
	JMPTBL64(GECE_BITSTATE + 19) goto next_bit; //IBGR 0x00040
	JMPTBL64(GECE_BITSTATE + 20) goto next_bit; //IBGR 0x00020
	JMPTBL64(GECE_BITSTATE + 21) goto least_bit; //IBGR 0x00010
	JMPTBL64(GECE_BITSTATE + 22) goto next_bit; //IBGR 0x00008 (third palette byte)
	JMPTBL64(GECE_BITSTATE + 23) goto next_bit; //IBGR 0x00004
	JMPTBL64(GECE_BITSTATE + 24) goto next_bit; //IBGR 0x00002
	JMPTBL64(GECE_BITSTATE + 25) goto next_bit; //IBGR 0x00001
//	JMPTBL64(GECE_BITSTATE + 26) goto start_node; //check_eof; //one last low transition or gap between nodes;;first_adrs_bit_in_progress; //0x20
//	JMPTBL64(GECE_BITSTATE + 28) goto check_eof; //gap between nodes
//	JMPTBL64(GECE_BITSTATE + 27) goto second_adrs_bit_in_progress; //0x10
	INGOTOC(FALSE); //check jump table doesn't span pages

//resume_chplex:
//	send_chplex();
//	if (ALWAYS) return; //avoid dead code removal

//newio:
//	iobusy = FALSE;
//	if (!NodeBytes) return; //nothing to do?
//	if (IsGeceFifoEmpty()) return; //nothing to do
//		WREG = (64 - 1)/2/RAM_SCALE - WREG; //compare to max series nodes
//		bitcopy(Borrow, parallel); //assume >= 64 nodes are parallel strings of 50 ct; CAUTION: NodeBytes must have been already set for this to be correct
//convert to node I/O loop count:
// s/p  ramsc  NodeBytes  I/O loop
//  s     1    strlen/2   2 * NB == strlen (max 63)
//  s     2    strlen/4   4 * NB == strlen (max 63)
//  s     4    strlen/8   8 * NB == strlen (max 63)
//  p     1    strlen*4   NB/4 == strlen (max 50)
//  p     2    strlen*2   NB/2 == strlen (max 50)
//  p     4    strlen*1   NB == strlen (max 50)
//		if (WREG >= 64/2/RAM_SCALE) //assume >= 64 nodes are parallel strings of 50 ct or less; CAUTION: NodeBytes must have been already set for this to be correct
//		if (ParallelNodes)
//		{
//			if (RAM_SCALE < 2) { send_count >>= 1; if (Carry) ++send_count; }
//			if (RAM_SCALE < 4) { send_count >>= 1; if (Carry) ++send_count; }
//		}
//		else //series nodes;; must be < 64 nodes; CAUTION: will wrap if >= 64
//		{
//			send_count <<= 1; if (Carry) send_count |= 0x80;
//			if (RAM_SCALE > 1) { send_count <<= 1; if (Carry) send_count |= 0x80; }
//			if (RAM_SCALE > 2) { send_count <<= 1; if (Carry) send_count |= 0x80; }
//		}
//		send_count |= 1; //kludge: lsb is used to select upper vs. lower nibble; set it for correct upper/lower interleave first time; sending an extra node is benign
//		WREG = (0x3F - 1) - send_count; //valid gece node addresses are 0..62 (63 sends to all); 0 is reserved for use as null address (to maintain transparency), leaving 62 valid addresses
//		if (Borrow) send_count += WREG; //62 - send_count < 0; set max 62
//		send_count--) return; //nothing to do?
//		if (EqualsZ) --send_count; //gece uses 6-bit addresses and 0x3F means "all", so max address is 62
//		if (parallel) send_count = 50/2;
//		send_count <<= 1; //convert to actual #nodes to simplify loop control; since max #gece nodes == 63, don't need to scale down #bytes
//?	portA = ~0; portC = WREG; //bit end = low-to-high transition
//	fsrH = 0;
//9 instr:
//		xchg(fsrL, fifo.txptr.bytes[0]); fsrH9_xchg(fifo.txptr.bytes[1]); //save caller rx ptr (GetBitmap opcode might still be in progress within caller); 9 instr
//		fifo.svfsrL = fsrL; fsrH_save(fifo.svfsrH);
//		fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //initialize fifo read ptr
//		fifo.send_count = indf_autonode;
//		fifo.node_adrs = indf_autonode;
//		fifo.adrs_mask = indf_autonode;
//		if (ParallelNodes) --fsrL; //all fifo entries are multiple of 4 bytes to reduce bank gap checking overhead
//		xchg(fsrL, fifo.txptr.bytes[0]); fsrH9_xchg(fifo.txptr.bytes[1]); //save my tx ptr, restore caller rx ptr (GetBitmap opcode might still be in progress within caller); 9 instr
//		fifo.txptrL = fsrL; fsrH_save(fifo.txptrH);
//		fsrL = fifo.svfsrL; fsrH_restore(fifo.svfsrH);
//		if (!fifo.send_count) return; //nothing to do?
//	if (ALWAYS) goto start_bit;
//	morebits = 0x20 + 20; //bypass undo first time (no previous)
//	wait(1 usec, break); //tmr0 = -4; //set Timer 0 to wrap soon; not necessary, but reduces wait for first node
//	if (ALWAYS) goto first_time;
//			wait(0); //wait for end of last bit-third of previous data bit
//	portA = ~0; portC = WREG;
//	portA = 0; nop1(); portC = 0; //bit start = high-to-low transition
//		wait(??); //reset period
//		morebits = 0x20;
//		gece_cmd = NOOP; //get next command from fifo
//		fifo.ioresume = GECE_BITSTATE + 0; //set initial loop state
//		rotate_palette(2, -4); //kludge: make consistent with previous state in regular pipeline flow; 16 instr
//		if (ALWAYS) goto first_adrs_bit_from_fifo;
//fall thru
//	gece_send_count = 1; //force read fifo command
//	iobusy = TRUE; //continue I/O asynchronously in background
//	outbuf = 0; //data line starts low, goes high for 10 usec before sending data (end of next_bit)
//	wait(4 usec, ASYNC_TIMER); //set dummy timer to expire soon, but give enough time for initialization
//	tmr0 += TimerPreset(10 usec, 0, Timer0); Timer0Wrap = FALSE; /*wait(10 usec - 3, break)*/; /*start next third; relative adjustment to prevent accumulation of timing errors*/ \
//	if (ALWAYS) goto first_time;


//collect info for next command during gap between nodes:
//NOTE: timing is a little sloppy in this section; timer intervals might overrun while processing commands, but the length of gap between nodes doesn't appear to be critical because gece nodes sync on leading bit transition anyway
start_node:
//data line goes low for a while between nodes:
//NOTE: data line stays low after last bit, so eof logic occurs after this transition
//3n + 6 instr:
	GECE_WAITBIT({ IFPORTA(porta = 0, EMPTY); nop1(); IFPORTBC(portbc = 0, EMPTY); }); //, WANT_TIMER, WANT_TIMER); //CLOCK_FREQ > 20 MHz); //high-to-low transition = bit start; 5 MIPS has no idle time after
//first stage of pipeline:
//check eof and read next fifo command before start of next node adrs bits
//		gece_cmd_XDELAY = FALSE; //need extra delay between nodes; flag is inverted so it will be correct after gece_read_next_command()
//collect data for next node:
	if (gece_demo_skip) ++gece_node_adrs; //kludge: slip alternating nodes, to be consistent with non-gece demo behavior
	if (--gece_send_count && (++gece_node_adrs < 0x3F)) gece_command(continued); //<= 16+9+16+2 == 43 instr; already have partial data for next node (previous set-all, series bitmap, or parallel bitmap still active); 0x3E is max individual adrs; 0x3F => broadcast to all nodes
	else
	{
eof_check:
		if (IsGeceFifoEmpty()) //I/O is finished; leave data lines low; fifo fills faster than it drains, so if fifo is empty then I/O is complete
		{
			front_panel(~FP_NODEIO);
			iobusy = FALSE; if (ALWAYS) return;
first_time: //jump directly to here, bypasses above so timer needs to be set
			if (ExtClockFailed && (MAXINTOSC_FREQ < 18 MHz /*CLOCK_FREQ < 32 MHz*/)) { PROTOCOL_ERROR(PROTOERR_SPEED); return; } //not running fast enough for this type of node I/O as coded here
//			tmr0 = TimerPreset(10 usec, 0, Timer0, CLOCK_FREQ); Timer0Wrap = FALSE; //set timer to avoid much idle time before first bit goes out
//#ifdef ASSUME_NOZCCHANGE //just choose preset value once at start of I/O handler; conditions are not likely to change later during I/O so this is reasonably safe
//			WREG = TimerPreset(10 usec, EVENT_OVERHEAD, Timer0, CLOCK_FREQ); //assume clock valid and then adjust if not
//			if (ExtClockFailed) //try to maintain correct timing with slower clock
//				WREG = TimerPreset(10 usec, EVENT_OVERHEAD, Timer0, MAXINTOSC_FREQ);
//			gece_tmr0_preset = WREG;
//			StartTimer_WREG(Timer0, ABSOLUTE_BASE); //set timer to avoid much idle time before first bit goes out
//#endif
			StartTimer(Timer0, 10 usec, ABSOLUTE_BASE); //set timer to avoid much idle time before first bit goes out
			front_panel(FP_NODEIO);
			iobusy = TRUE;
//	if (!NodeBytes) return; //nothing to do?
			goto eof_check;
		}
//read next command from fifo; 22 instr to here
//NOTE: caller is responsible for using valid node addresses; node#s > 0x3F will wrap
		gece_my_fsr(TRUE); //7 or 10 instr
		gece_cmd_state = indf_autonode;
		gece_command(begin_read); //worst case 9+24 == 33 instr
		gece_new_ptrs = TRUE;
	}
//3n + 3 instr:
	GECE_WAITBIT(/*{ IFPORTA(porta = 0, EMPTY); nop1(); IFPORTBC(portbc = 0, EMPTY); }*/ EMPTY); //, WANT_TIMER, WANT_TIMER); //CLOCK_FREQ > 20 MHz); //high-to-low transition = bit start; 5 MIPS has no idle time after
	if (gece_new_ptrs)
	{
		gece_new_ptrs = FALSE; //don't interfere with command branching
		gece_command(end_read); //worst case 9+15 == 24 instr
		gece_new_fifo_entry(CONSUME); //consume this fifo entry, advance to next; 16 instr
		gece_caller_fsr(FALSE); //5 instr
		gece_new_ptrs = TRUE;
	}
	gece_ioresume = GECE_BITSTATE - 1; //leave gap before sending first bit; state might have wrapped or first time jumped in, so need to explicitly set this here
//3n + 6 instr:
	GECE_WAITBIT(/*{ IFPORTA(porta = 0, EMPTY); nop1(); IFPORTBC(portbc = 0, EMPTY); }*/ EMPTY); //, WANT_TIMER, WANT_TIMER); //CLOCK_FREQ > 20 MHz); //high-to-low transition = bit start; 5 MIPS has no idle time after
	if (ALWAYS) return; //go handle serial port for a while


//finish setting up node cache and new palette ptrs prior to sending first bit:
more_gap:
//data line should still be low at this point, no need to change it so just use timer to maintain uniform timing
//3n + 3 instr:
	GECE_WAITBIT(/*{ IFPORTA(porta = 0, EMPTY); nop1(); IFPORTBC(portbc = 0, EMPTY); }*/ EMPTY); //, WANT_TIMER, WANT_TIMER); //CLOCK_FREQ > 20 MHz); //high-to-low transition = bit start; 5 MIPS has no idle time after
	if (gece_new_ptrs) //convert new palette indexes to ptrs
	{
//split even/odd node data:
//8 instr:
		swap_WREG(gece_node_ptrs[1]); gece_node_ptrs[0] = WREG; 
		swap_WREG(gece_node_ptrs[3]); gece_node_ptrs[2] = WREG; 
		swap_WREG(gece_node_ptrs[5]); gece_node_ptrs[4] = WREG; 
		swap_WREG(gece_node_ptrs[7]); gece_node_ptrs[6] = WREG; 
//9 instr:
		WREG = 0x0F;
		gece_node_ptrs[0] &= WREG;
		gece_node_ptrs[1] &= WREG;
		gece_node_ptrs[2] &= WREG;
		gece_node_ptrs[3] &= WREG;
		gece_node_ptrs[4] &= WREG;
		gece_node_ptrs[5] &= WREG;
		gece_node_ptrs[6] &= WREG;
		gece_node_ptrs[7] &= WREG;
//start converting palette index to ptr:
//1+6*3 = 19 instr:
		Carry = FALSE; //NOTE: Carry assumed to stay false after each rotate (small address values should not wrap)
		rl_nc_WREG(gece_node_ptrs[0]); WREG += PALETTE_ADDR(0, 4 BPP); gece_node_ptrs[0] += WREG;
		rl_nc_WREG(gece_node_ptrs[1]); WREG += PALETTE_ADDR(0, 4 BPP); gece_node_ptrs[1] += WREG;
		rl_nc_WREG(gece_node_ptrs[2]); WREG += PALETTE_ADDR(0, 4 BPP); gece_node_ptrs[2] += WREG;
		rl_nc_WREG(gece_node_ptrs[3]); WREG += PALETTE_ADDR(0, 4 BPP); gece_node_ptrs[3] += WREG;
		rl_nc_WREG(gece_node_ptrs[4]); WREG += PALETTE_ADDR(0, 4 BPP); gece_node_ptrs[4] += WREG;
		rl_nc_WREG(gece_node_ptrs[5]); WREG += PALETTE_ADDR(0, 4 BPP); gece_node_ptrs[5] += WREG;
	}
//3n + 3 instr:
	GECE_WAITBIT(/*{ IFPORTA(porta = 0, EMPTY); nop1(); IFPORTBC(portbc = 0, EMPTY); }*/ EMPTY); //, WANT_TIMER, WANT_TIMER); //CLOCK_FREQ > 20 MHz); //high-to-low transition = bit start; 5 MIPS has no idle time after
	if (gece_new_ptrs) //finish converting new palette indexes to ptrs
	{
//1+2*3+1 = 8 instr:
		Carry = FALSE; //NOTE: Carry assumed to stay false after each rotate (small address values should not wrap)
		rl_nc_WREG(gece_node_ptrs[6]); WREG += PALETTE_ADDR(0, 4 BPP); gece_node_ptrs[6] += WREG;
		rl_nc_WREG(gece_node_ptrs[7]); WREG += PALETTE_ADDR(0, 4 BPP); gece_node_ptrs[7] += WREG;
		gece_new_ptrs = FALSE;
	}
//clear node cache:
//8 instr:
	gece_node_cache[0] = 0;
	gece_node_cache[1] = 0;
	gece_node_cache[2] = 0;
	gece_node_cache[3] = 0;
	gece_node_cache[4] = 0;
	gece_node_cache[5] = 0;
	gece_node_cache[6] = 0;
	gece_node_cache[7] = 0;
//preload node cache with address bits so data sending logic can be used for address bits also:
	rl_WREG(gece_node_adrs); gece_outbuf = WREG; add_WREG(gece_outbuf); //shift 6 address bits into msb position
//load adrs into node cache:
//2*8 = 16 instr:
	if (gece_adrs_mask & 0x80) gece_node_cache[0] = WREG;
	if (gece_adrs_mask & 0x40) gece_node_cache[1] = WREG;
	if (gece_adrs_mask & 0x20) gece_node_cache[2] = WREG;
	if (gece_adrs_mask & 0x10) gece_node_cache[3] = WREG;
	if (gece_adrs_mask & 0x08) gece_node_cache[4] = WREG;
	if (gece_adrs_mask & 0x04) gece_node_cache[5] = WREG;
	if (gece_adrs_mask & 0x02) gece_node_cache[6] = WREG;
	if (gece_adrs_mask & 0x01) gece_node_cache[7] = WREG;
//low-to-high transistion in prep for first bit:
//3n + 6 instr:
	GECE_WAITBIT({ IFPORTA(porta = ~0, EMPTY); IFPORTBC(portbc = ~0, EMPTY); }); //, WANT_TIMER, WANT_TIMER); //low-to-high transition = bit end; 5 MIPS has no idle time before
	if (ALWAYS) return; //go handle serial port for a while


//special case for last bit of each byte:
//also preload node cache for next byte
least_bit:
	gece_reload_cache = TRUE;
//fall thru
//for each parallel node, collect next bit from node cache and send:
next_bit:
//	volatile bit doing_lsb @BITADDR(adrsof(statebit_0x20));
//3n + 6 instr:
	GECE_WAITBIT({ IFPORTA(porta = 0, EMPTY); nop1(); IFPORTBC(portbc = 0, EMPTY); }); //, WANT_TIMER, WANT_TIMER); //CLOCK_FREQ > 20 MHz); //high-to-low transition = bit start; 5 MIPS has no idle time after
//assemble next output byte from next bit of 8 parallel strings:
	gece_outbuf = 0;
//8*2 = 16 instr:
	rl_nc(gece_node_cache[0]); rl_nc(gece_outbuf); //shift msb out of cache and into outbuf; 2 instr each
	rl_nc(gece_node_cache[1]); rl_nc(gece_outbuf);
	rl_nc(gece_node_cache[2]); rl_nc(gece_outbuf);
	rl_nc(gece_node_cache[3]); rl_nc(gece_outbuf);
	rl_nc(gece_node_cache[4]); rl_nc(gece_outbuf);
	rl_nc(gece_node_cache[5]); rl_nc(gece_outbuf);
	rl_nc(gece_node_cache[6]); rl_nc(gece_outbuf);
	rl_nc(gece_node_cache[7]); rl_nc(gece_outbuf);
	compl(gece_outbuf); //outbuf = ~outbuf; //"0"s go high; "1"s stay low
//adjust node ptrs to next byte of palette:
//NOTE: this is speculative; it's not needed for last palette byte, but it's always done since there's nothing else to do here
	if (gece_reload_cache)
//5 or 6 instr:
//	WREG = gece_ioresume - (GECE_BITSTATE+1 + 6 -1); //which palette bit (1..26), or -5..0 for adrs bits)
//	WREG &= 7;
//	if (EqualsZ) //sending last bit of byte (adrs lsb or bit 7 or 15 of palette): node cache is empty; reload node cache from next palette byte
	{
//		iobusy = FALSE; //allow faster check next time; kludge: can reuse shared flag here as long as it's restored before return
		gece_my_fsr(FALSE); //save caller's fsr (GetBitmap might still be in progress); 5 instr
		fsrH = PALETTE_ADDR(0, 4 BPP) / 0x100;
		/*if (ParallelNodes)*/ { fsrL = gece_node_ptrs[0]++; gece_node_cache[0] = indf; } //copy next palette byte into cache for series string on pin A2; 4 instr each
		fsrL = gece_node_ptrs[1]++; gece_node_cache[1] = indf; //always so series I/O pin
	}
//send next adrs or data bit:
	WREG = gece_outbuf >> 4; WREG += 8; //kludge: move 0x08 to 0x10 for A4; 3 instr
//3n + 6 instr:
	GECE_WAITBIT({ IFPORTA(porta = WREG, EMPTY); IFPORTBC(portbc = gece_outbuf, EMPTY); }); //, WANT_TIMER, WANT_TIMER); //CLOCK_FREQ > 20 MHz, TRUE); //CLOCK_FREQ > 20 MHz); //send actual data bit values; 5 MIPS has no idle time before/after
//continue loading next palette byte into node cache:
	if (gece_reload_cache) //&& ParallelNodes) //copy palette byte into cache for parallel nodes on other I/O pins
	{
//	if (!iobusy) //continued from above
//	WREG = gece_ioresume - (GECE_BITSTATE+1 + 6 -1); //which palette bit (1..26), or -5..0 for adrs bits)
//	WREG &= 7;
//	if (EqualsZ) //sending lsb (adrs lsb or bit 7 or 15 of palette): node cache is empty; reload node cache from next palette byte
//	{
//6*5 = 30 instr:
//		fsrL = gece_node_ptrs[0]++; gece_node_cache[0] = indf;
		fsrL = gece_node_ptrs[2]++; gece_node_cache[2] = indf;
		fsrL = gece_node_ptrs[3]++; gece_node_cache[3] = indf;
		fsrL = gece_node_ptrs[4]++; gece_node_cache[4] = indf;
		fsrL = gece_node_ptrs[5]++; gece_node_cache[5] = indf;
		fsrL = gece_node_ptrs[6]++; gece_node_cache[6] = indf;
		fsrL = gece_node_ptrs[7]++; gece_node_cache[7] = indf;
		gece_caller_fsr(FALSE); //restore caller's fsr (GetBitmap might still be in progress); 5 instr
		gece_reload_cache = FALSE;
	}
//8 instr:
//		++gece_node_ptr[0]; //advance to next palette byte; 1 instr each
//		++gece_node_ptr[1];
//		++gece_node_ptr[2];
//		++gece_node_ptr[3];
//		++gece_node_ptr[4];
//		++gece_node_ptr[5];
//		++gece_node_ptr[6];
//		++gece_node_ptr[7];
//		iobusy = TRUE; //restore state flag
//	}
//3n + 6 instr:
	GECE_WAITBIT({ IFPORTA(porta = ~0, EMPTY); IFPORTBC(portbc = ~0, EMPTY); /*WREG = TimerPreset(50 usec, 0, Timer0); if (!gece_cmd_XDELAY) tmr0 += WREG*/; }); //, WANT_TIMER, WANT_TIMER); //CLOCK_FREQ > 20 MHz, TRUE); //low-to-high transition = bit end; 5 MIPS has no idle time before
	if (ALWAYS) return; //go handle serial port for a while
//	return; //go handle serial port for a while
//	TRAMPOLINE(2); //kludge: compensate for page select or address tracking bug
}
//#undef protocol()  //kludge: redirect BusyPassthru back to regular protocol handler
#else //def WANT_GECE
 #define IsGece  FALSE
 #define gece_init(ignored)
 #define is_gece()
// #define gece_init()
// non_inline void gece_init(void) { ONPAGE(0); } //put on same page as protocol handler to reduce page selects; needs to be real func because fwd ref used
 #define gece_new_fifo_entry(ignored)
// #define gece_more_fifo_data(ignored)
 #define send_gece_4bpp()  PROTOCOL_ERROR(PROTOERR_EXCLUDED)
#endif //def WANT_GECE

//kludge: slip alternating nodes, to be consistent with non-gece demo
#define set_all_gece_alternating(palinx, want_alternating)  \
{ \
	volatile bit gece_demo_skip @BITADDR(adrsof(statebit_0x10)); \
	bitcopy(want_alternating, gece_demo_skip); \
	WREG = palinx; \
	ABSCALL(SetAll_GECEAlternating); /*demo kludge: set alternating gece nodes, to make demo behavior consistent*/ \
	if (NEVER) set_all_WREG(); /*avoid dead code removal*/ \
}


//overlap ZC timing info with palette (mutually exclusive usage):
//NOTE: last palette entry must be re-initialized if switching between pwm/chplex and smart nodes (won't happen under normal operation, because props are assumed to have dedicated controllers so node type will not change once selected)
//demo code only uses first half of palette, so this data will still be available if chplex/pwm I/O is selected
#define ZC_STATE  PALETTE_ADDR(16, 4 BPP) - 2
volatile bit zc_present @MakeBanked(ZC_STATE).7; //adrsof(statebit_0x80));
volatile bit zc_60hz @MakeBanked(ZC_STATE).6; //adrsof(statebit_0x40));
volatile uint8 zc_filter @MakeBanked(PALETTE_ADDR(16, 4 BPP) - 3);
volatile uint16 zc_count @MakeBanked(PALETTE_ADDR(16, 4 BPP) - 5); //ZC sampled @30 usec for 1 sec => max 33K samples; ZC should be 100 or 120, but use a 16-bit counter just in case
volatile uint24 zc_samples @MakeBanked(PALETTE_ADDR(16, 4 BPP) - 8); //ZC sampled @30 usec for 1 sec => max 33K samples; ZC should be 100 or 120, but use a 16-bit counter just in case
#define zclo_width_ADDR  MakeBanked(PALETTE_ADDR(16, 4 BPP) - 11)
#define zchi_width_ADDR  MakeBanked(PALETTE_ADDR(16, 4 BPP) - 14)
volatile struct { uint8 min, this, max; } zclo_width @adrsof(zclo_width); //capture ZC low width for possible use with ZC diagnostics
volatile struct { uint8 min, this, max; } zchi_width @adrsof(zchi_width); //capture ZC high width for possible use with ZC diagnostics

//volatile uint8 sv_zccount @PALETTE_ADDR(15, 4 BPP); //save for debug (overwritten by smart node palette)

//rising/falling ZC edge patterns:
//these are typically inverse of each other, but can be skewed if needed
//only falling edge is used for dimming; rising edge is only for ZC profiling for debug/dev
#define IsFallingEdge(zcfilt)  ((zcfilt) == 0b11110000)
#define IsRisingEdge(zcfilt)  ((zcfilt) == 0b00001111)
#define zc_sample() \
{ \
	/*wait(30 usec, EMPTY)*/; \
	bitcopy(porta & (1<<PINOF(ZC_PIN)), Carry); \
	/*porta += -1<<IOPIN(ZC_PIN);*/ \
	rl_nc(zc_filter); /*shift new sample into lsb of zc filter; room for 8 samples*/ \
}
//wrapper to avoid recursion in wait() macro:
inline void zc_sample_rate(void)
{
//NOTE: actual sample interval here doesn't matter as long as ZC pulse is > 4 samples to allow edge detection; therefore the 10 usec time does not need to be adjusted based on int/ext clock
	wait(10 usec, EMPTY); //NOTE: incoming char will NO LONGER pre-empt delay time, causing sample to be acquired too early; however, since ZC is filtered for falling edge, this will not cause an incorrect zc count
	zc_sample();
//not doing anything else right now, so might as well gsther some interesting ZC profile info:
	inc24(zc_samples); //calibration check; with 50 usec sample interval, should be 20K after 1 sec or 40K after 2 sec; might be too large; with 10 usec sample interval, should be 100K after 1 sec or 200K after 2 sec
//	fsrL = adrsof(zclo_width) + 1; fsrH = adrsof(zclo_width) / 0x100;
	if (zc_filter & 1) //ZC pulled up => opto is off (open); happens when AC voltage < minimum around zero-crossing time
	{
		++zclo_width.this;
		if (IsFallingEdge(zc_filter)); else return; //zc falling edge; can't turn on triacs/scrs during ZC so wait until after (~120 usec)
//		{
			++zc_count;
			zclo_width.this = 0; //start watching low width
			if (zc_count < 2) return; //first interval might have been partial; skip it
			WREG = zchi_width.this - zchi_width.min;
			if (Borrow) zchi_width.min += WREG; //this < min; update min
			WREG = zchi_width.this - zchi_width.max;
			if (NotBorrow) zchi_width.max += WREG; //max <= this; update max
//		}
//		fsrL += adrsof(zchi_width) - adrsof(zclo_width);
	}
	else //ZC grounded => opto is on (closed); happens when AC voltage >= minimum
	{
		++zchi_width.this;
		if (IsRisingEdge(zc_filter)); else return; //rising edge only used for SSR doubling (TODO)
//		{
			zchi_width.this = 0; //start watching high width
			if (zc_count < 2) return; //first interval might have been partial; skip it
			WREG = zclo_width.this - zclo_width.min;
			if (Borrow) zclo_width.min += WREG; //this < min; update min
			WREG = zclo_width.this - zclo_width.max;
			if (NotBorrow) zclo_width.max += WREG; //max <= this; update max
//		}
	}
//update zc low or high stats at falling or rising edge:
//	if (zc_count < 2) return; //first interval might have been partial; skip it
//	WREG = indf; --fsrL; WREG -= indf; //WREG = zchi_width.this - zchi_width.min;
//	if (Borrow) indf += WREG; //zchi_width.min += WREG; //this < min; update min
//	++fsrL; WREG = indf; ++fsrL; WREG -= indf; //WREG = zchi_width.this - zchi_width.max;
//	if (NotBorrow) indf += WREG; //zchi_width.max += WREG; //max <= this; update max
}


//wait for 2 sec to allow nodes to stabilize when first powered up:
//used at start of main() NOT: and again for idle/delay loop since a function is already defined and duration doesn't matter
//since chplex/pwm I/O needs to know ZC rate, do sampling during this time also (results will be overwritten if not needed)
#undef delay_loop_adjust
inline void delay_loop_adjust(uint8& counter)
{
//	swap_WREG(NodeConfig); WREG ^= RENXt_GECE(DONT_CARE); WREG &= 0x0E; //WREG = NodeConfig >> 4; //split up expr to avoid BoostC generating a temp
	is_gece();
//	WREG += 0-(RENXt_GECE + 1); //if (WREG >= RENXt_GECE+1) Borrow = FALSE
//	if (Borrow) { counter += counter; counter += counter; }; //need to wait the full 2 sec instead of 1/2 sec (for node stabilization or ZC count)
	if (IsGece) counter <<= 1; //+= counter;
}

non_inline void wait_2sec_zc(void)
{
//	volatile bit shorter_wait @BITADDR(adrsof(statebit_0x10));
//	uint8 wait_count @Linear2Banked(2); //PALETTE_ADDR(0, 4 BPP) + 1); //adrsof(PROTOCOL_STACKFRAME3) + 1;
	ONPAGE(LEAST_PAGE);

//	WREG = NodeConfig >> 4; //split up expr to avoid BoostC generating a temp
//	WREG += 0-(RENXt_GECE + 1); //if (WREG >= RENXt_GECE+1) Borrow = FALSE
//	WREG = 4; //wait for 2 sec
//	if (NotBorrow) WREG = 1; //don't care about ZC and don't need to wait full 2 sec; only wait 1/2 sec
//	wait_count = WREG;
//	zc_sample(FALSE); //initialize zc samples
	zc_filter = 0; zc_count = 0; //CAUTION: overwrites first palette entry (only if ZC signal is present); this is redundant since bkg is initialized to 0 anyway
	zero24(zc_samples); //this should be 25K after 1 sec or 50K after 2 sec; used for dev debug/calibration only, but leave it here in case needed
	zclo_width.min = 0xFF; zclo_width.max = 0; zclo_width.this = 0; //min/max ZC width; used only for dev/calibration of ZC logic
	zchi_width.min = 0xFF; zchi_width.max = 0; zchi_width.this = 0; //min/max ZC width; used only for dev/calibration of ZC logic
//#define protocol()  continue //kludge: to get an accurate ZC count Timer 1 must not be interrupted here; since inner wait already handled serial port, don't need to do it again here
//	for (;;) //wait in 1/2 sec intervals to allow same code to be used for shorter or longer wait times
//	{
//1 sec == 16 *
		wait(1 sec, zc_sample_rate()); //update zc samples while waiting; delay_loop_adjust() will change this to 2 sec depending on node type
//		if (!--wait_count) break;
//	}
//#undef protocol
	/*wait(30 usec, EMPTY)*/; \
//	wait(2 sec, EMPTY);
//	WREG = NodeConfig >> 4; //split up expr to avoid BoostC generating a temp
//	WREG += 0-(RENXt_GECE + 1); //if (WREG >= RENXt_GECE+1) Borrow = FALSE
//	if (Borrow) zc_count += zc_count; { counter += counter; counter += counter; }; //need to wait the full 2 sec instead of 1/2 sec (for node stabilization or ZC count)
//	swap_WREG(NodeConfig); WREG ^= RENXt_GECE(DONT_CARE); WREG &= 0x0E; //WREG = NodeConfig >> 4; //split up expr to avoid BoostC generating a temp
	is_gece();
	if (IsGece) zc_count >>= 1; //only want ZC count for 1 sec, but needed to wait 2 sec for gece to stabilize
// sv_zccount = zc_count;
	bitcopy(zc_count >= 10, zc_present); //treat anything >= 10 Hz as a sync signal
	bitcopy(zc_count > 2 * 55, zc_60hz); //if >= 2 * 55 rising edges were seen per sec, treat it as 60 Hz AC
//no; leave it there	zc_filter = 0; zc_count = 0; //reset first palette entry in case demo pattern needs it
}
#define delay_loop_adjust(ignored)  EMPTY //overridable by caller; use this instead of another macro param
//non_inline void wait_2sec_nozc(void)
//{
//	ONPAGE(DEMO_PAGE); //put on same page as demo code to reduce page selects
//	wait_2sec();
//	zc_filter = 0; zc_count = 0; //restore first palette entry
////	SetPalette(BLACK_PALINX, BLACK); //kludge: restore first palette entry (wait_2sec uses it for ZC info)
//}


//Chplex I/O handler:
//Turns individual channels on/off according to a display event list
//;Runs in a continuous loop as a background I/O thread, only returns to caller to handle protocol, then caller must return
//If 100/120 Hz ZC signal is present on A3, chplex cycles are synced to it; otherwise a sender-determined free-running cycle is used
//PWM is a special case of chipiplexing (only one row)
//NOTE: this I/O handler must run continuously in order to refresh the dumb channel; this requires extra variable space; RGB palette is used, since dumb channels don't use the palette
#if 0
inline void resume_chplex(void)
{
	resume_io(); //check prev cases first
	WREG += 0 - (LAST_DUMBIO + 1); //preserve WREG
	if (Borrow) { ABSCALL(ResumeChplex); return; } //use chplex handler for all non-null dumb I/O
	WREG += LAST_DUMBIO + 1; //restore WREG for next check in this chain
}
#undef resume_io
#define resume_io()  resume_chplex() //wedge for resume I/O
#endif

//chipiplex/pwm handler:
//supported combinations are pwm, muxed-pwm or chipiplex, common cathode or anode, synced with AC (50 or 60 Hz) or free-running at sender-defined refresh rate (typically 50 or 100 Hz) = 18 combinations
//TODO: is double buffering needed to avoid flicker during frame buf refresh?
//NOTE: this I/O handler must be called at least every ~ 30 usec in order to maintain dimming timing
#ifdef WANT_CHPLEX
//check if Chplex I/O is enabled:
//status.Z holds the results of the compare
inline void is_chplex(void)
{
	swap_WREG(NodeConfig); WREG ^= RENXt_CHPLEX(DONT_CARE); WREG &= 0x0F & ~1; //WREG = NodeConfig >> 4; //split up expr to avoid BoostC generating a temp
//	bitcopy(EqualsZ, gece);
}

non_inline void send_chplex(void)
{
//NOTE: chplex state must be persistent across returns to caller, so don't put it all in shared memory
//#warning "TODO: check if protocol handler will overwrite this data"
//	volatile uint8 zc_filter @adrsof(PROTOCOL_STACKFRAME3) + 0;
//	volatile uint8 zc_count @adrsof(PROTOCOL_STACKFRAME3) + 1;
//	volatile bit zc60hz @BITADDR(adrsof(statebit_0x10));
//	volatile bit CommonCathode @adrsof(NODE_CONFIG).4;
	volatile bit chplex @MakeBanked(ZC_STATE).5; //BITADDR(adrsof(statebit_0x20)); //turn outputs off after one cycle; NOTE: 0xc0 are already used by ZC flags
//	volatile bit evtptrH @BITADDR(adrsof(statebit_0x40)); //upper bit of node ptr; 9-bit adrs allows 1<<9 == 512 bytes linear or 4 * 80 == 320 bytes banked
//	volatile uint8 evtptrL @adrsof(PROTOCOL_STACKFRAME3) + 0; //lower byte of current node pair address; must be non-banked to prevent interference with bit loop timing; CAUTION: also holds current palette base address
//	volatile uint8 send_count @adrsof(PROTOCOL_STACKFRAME3) + 1; //#bytes to send; count is more efficient than address compare (bytes not contiguous: 0x50..0x70, 0xa0..0xf0, 0x120..0x170); NOTE: units are byte pairs for 512 RAM or byte quads for 1K RAM
//	volatile uint8 timeslice @adrsof(PROTOCOL_STACKFRAME3) + 0;
	volatile uint8 evtdelay @MakeBanked(PALETTE_ADDR(16, 4 BPP) - 15); //active length for current display event
#ifdef PIC16X //linear addressing; TODO: generalize this macro
	volatile uint8 nodeptrH @MakeBanked(PALETTE_ADDR(16, 4 BPP) - 16); //upper bit of node ptr; 9-bit adrs allows 1<<9 == 512 bytes linear or 4 * 80 == 320 bytes banked
#else
	volatile bit nodeptrH @MakeBanked(PALETTE_ADDR(16, 4 BPP) - 16).IRP; //CAUTION: must be same bit position as IRP for faster save/restore; BITADDR(adrsof(statebit_0x10)); //upper bit of node ptr; 9-bit adrs allows 1<<9 == 512 bytes linear or 4 * 80 == 320 bytes banked
#endif
	volatile uint8 nodeptrH_byte @MakeBanked(PALETTE_ADDR(16, 4 BPP) - 16); //parent byte of nodeptrH
	volatile uint8 nodeptrL @MakeBanked(PALETTE_ADDR(16, 4 BPP) - 17); //adrsof(PROTOCOL_STACKFRAME3) + 0; //lower byte of current node pair address; must be non-banked to prevent interference with bit loop timing
	volatile uint2x8 portbuf @MakeBanked(PALETTE_ADDR(16, 4 BPP) - 19); //put port buffers in bank 0 since port regs are there anyway
	volatile uint2x8 trisbuf @adrsof(PROTOCOL_STACKFRAME3) + 1; //put tris buffers in non-banked RAM to reduce bank selects during prep and flush
	volatile uint8 last_overflow @MakeBanked(PALETTE_ADDR(16, 4 BPP) - 20); //adrsof(PROTOCOL_STACKFRAME3) + 0; //lower byte of current node pair address; must be non-banked to prevent interference with bit loop timing
	volatile uint8 tmr0_preset @MakeBanked(PALETTE_ADDR(16, 4 BPP) - 21); //calculate timer0 preset once; CAUTION: timing will be trashed if this is overwritten, but that's the trade-off between caching in memory vs. re-calculating each time
	volatile uint8 all_off @MakeBanked(PALETTE_ADDR(16, 4 BPP) - 22); //remember what value to use for all channels off; calculate once since I/O type will not change
//	volatile uint8 dummy_eof @Linear2Banked(5);
// volatile uint2x8 here@0x29;
	ONPAGE(IOH_PAGE); //put on same page as protocol handler to reduce page selects

//Triacs require ~ 12V to turn on and latch; 120 VAC line is ~ 170V peak-to-peak; this gives a minimum phase angle of 12/170 ~= 4 of 360 degrees for Triac dimming
//AC cycle time is 8.333 msec (60 Hz) or 10.0 msec (50 Hz); 1/90 of this is ~ 93 usec (60 Hz) or ~ 111 usec (50 Hz), so ~ 6 timeslots are lost from the range 0 - 255
//to give a full usable range of 0 - 255, compensate by using 255+6 timeslots in the timing calculation
//therefore phase angle of 12V on a 120VAC line (170V peak) ~= 4 of 360 degrees ~= 93 usec, so ~ 180 usec of AC waveform is lost ~= 6 timeslots)"
#define DIMSLICE(hz)  (1 sec/(255+6)/(2 * hz)) //timeslice to use for 255 dimming levels at given AC rate; should be ~ 32 usec (60 Hz) or ~ 43 usec (50 Hz)
#define EVENT_OVERHEAD  2 //20 //approx #instr to flush display event and start next dimming timeslot; all prep overhead occurs before start of timeslot to reduce latency and jitter

#ifndef WANT_GECE //define entry point for async/background I/O thread
//	BANKSELECT(bank1[0]); //kludge: force bank select below
ABSLABEL(ResumeNodeIO); //alternate entry point to resume previous I/O
#endif
// zc_present = FALSE;
//startio:
// ++here.as16;
	front_panel(FP_NODEIO); //NOTE: runs asynchronously until explicitly cancelled
	ioresume = NodeConfig >> 4; //come back here again next time (I/O continues indefinitely); CAUTION: GECE I/O resume increments this, so must set each time
	xchg(fsrL, nodeptrL); fsrH9_xchg(nodeptrH_byte); //save caller data, restore my event pointer since this I/O handler continues to run later
#warning "TODO: streamline fsr save/restore"
	if (!iobusy) //first time here after new display event list arrived (for each animated frame in demo mode, but only once for live protocol); initialize
	{
//	if (!iobusy) return; //ignore redudant I/O requests from sender; previous I/O request remains active until node type is changed or new data arrives
//NOTE: this code assumes that smart nodes will not be used, so re-initializing tris later is not needed
//if node I/O type is changed in EEPROM, controller must be reset since ZC rate is only sampled at startup and results overwritten by smart node I/O (since the info wasn't needed)
//#warning "TODO: call send_nodes an extra time when changing I/O handlers"
//	WREG = NodeConfig >> 4; //check next I/O handler type
//	WREG += 0 - MIN(MIN(RENXt_PWM(0xCA), RENXt_PWM(0xCC)), MIN(RENXt_CHPLEX(0xCA), REBRGB_CHPLEX(0xCC)));
//	WREG += -4;
//	if (NotBorrow) //sender selected a new I/O handler type; set tris for node I/O
//	{
//		IFPORTA(porta = 0, EMPTY);
//		IFPORTBC(portbc = 0, EMPTY);	
//		IFPORTA(trisa = TRISA_INIT(NODES), EMPTY); //set all node I/O pins to Output
//		IFPORTBC(trisbc = TRISBC_INIT(NODES), EMPTY);
//		return;
//	}
//	IFPORTA(trisa = TRISA_INIT(CHPLEX), EMPTY); //set all node I/O pins to high-Z; reset controller to undo this
//	IFPORTBC(trisbc = TRISBC_INIT(CHPLEX), EMPTY);
//choose timeslice based on refresh rate:
//	zc_filter = 0; zc_count = 0;
//	wait(2 sec, zc_sample(zc_filter, zc_count)); //measure AC frequency
//	wait_2sec(); //sample zc rate
//	WREG = 0 - rdiv(uSec2Instr(1 sec/255/(2 * 50)), Timer0_Prescalar); //Timer 0 preset to use for 255 dimming slots @50 Hz
//	if (zc60hz) WREG = 0 - rdiv(uSec2Instr(1 sec/255/(2 * 60)), Timer0_Prescalar); //preset for 60 Hz
//	timeslice = 
//	bitcopy(NodeConfig >= MIN(RENXt_CHPLEX(0xCC), RENXt_CHPLEX(0xCA)) << 4, chplex); //check chplex vs. dedicated (pwm)
//		chplex = FALSE; //pwm == chplex with no row active
//		trisbuf.bytes[0] = TRISA_INIT(NODES); //set all pins to low Output for pwm
//		trisbuf.bytes[1] = TRISBC_INIT(NODES);
		WREG = 0;
		if (!ActiveHigh) WREG = ~0; //off is high instead of low
		all_off = WREG;
		portbuf.bytes[0] = WREG;
		portbuf.bytes[1] = WREG;
//		WREG = NodeConfig >> 4;
//		WREG ^= RENXt_CHPLEX(0xCC); //preserve WREG
//		if (EqualsZ) chplex = TRUE;
//		WREG ^= RENXt_CHPLEX(0xCA) ^ /*undo prev:*/ RENXt_CHPLEX(0xCC);
//		if (EqualsZ) chplex = TRUE;
		is_chplex();
		bitcopy(EqualsZ, chplex); //remember to adjust tris each time; NOTE: this only affects transistor duty cycle; PWM mode can be used with chipiplexing to leave the transistor turned on longer
//		if (chplex) //skip the overhead of checking and always do this
//		{
			trisbuf.bytes[0] = TRISA_INIT(CHPLEX); //set all rows to high-Z
			trisbuf.bytes[1] = TRISBC_INIT(CHPLEX);
//		}

//get first display event:
//8-pin display events take 3 bytes: delay, row mask, column mask
//max #display events with 8 I/O pins == 8*7 == 56
//240 bytes of RAM would hold up to 80 display events with 3 bytes/event or 60 events at 4 bytes/event
//	gece_node_setup(-1); //adjust ptrs for first time thru loop
//	morebits = 0x20; //only send 6 address bits
		iobusy = TRUE; //continue below after processing incoming serial data; chplex/pwm I/O handler repeats until new data received
//		ioresume = NodeConfig >> 4; //already done above
#ifdef ASSUME_NOZCCHANGE //just choose preset value once at start of I/O handler; conditions are not likely to change later during I/O so this is reasonably safe
		WREG = TimerPreset(DIMSLICE(50), EVENT_OVERHEAD, Timer0, CLOCK_FREQ); //assume 50 Hz and then adjust if it was 60 Hz
		if (zc_60hz) WREG = TimerPreset(DIMSLICE(60), EVENT_OVERHEAD, Timer0, CLOCK_FREQ);
		if (ExtClockFailed) //try to maintain correct timing with slower clock
		{
			WREG = TimerPreset(DIMSLICE(50), EVENT_OVERHEAD, Timer0, MAXINTOSC_FREQ); //assume 50 Hz and then adjust if it was 60 Hz
			if (zc_60hz) WREG = TimerPreset(DIMSLICE(60), EVENT_OVERHEAD, Timer0, MAXINTOSC_FREQ);
		}
		tmr0_preset = WREG;
#endif
//		tmr0 = WREG; Timer0Wrap = FALSE; //start with full time slice, avoid false trigger of overflow check
		StartTimer_WREG(Timer0, ABSOLUTE_BASE); //set timer to avoid much idle time before first bit goes out
//	goto start_dimming;
//	tmr0 = ~0; //kludge: set minimum delay period so first bit does not wait
//n0,n1,n2,n3,...n23 (3 * 8)
//PWM (row 0): pins start all on, then each display event turns off at least one pin => max 8 display events => 16 bytes
//chplex (8 rows 1..8, 7 cols): => max 8*7=56 display events => 112 bytes
//rowmask,colpins,delay,rowmask,colpins,delay,rowmask,colpins,delay,...,idle until next ZC when delay == 0
		NOPE(zc_filter = 0); //don't clear samples collected so far; if updated framebuf occurs just before ZC go ahead and start immediately
		if (!zc_present) goto next_cycle; //++evtdelay; //set first event to expire immediately; no ZC falling edge to wait for
		evtdelay = 0; //set max delay on first event; zc falling edge will occur first
//		fsrL = PALETTE_ADDR(0, 4 BPP) + 5; fsrH = (PALETTE_ADDR(0, 4 BPP) + 5)/0x100;
//		indf = 0; //set dummy eof marker to force display event list rewind first time
//		continue;
	}
// ++here;
//TODO: check if NodeConfig changed
//TODO: check if display event list changed? caller should reset fsr to point to eof marker 
//#warning "caller should preserve fsrL/fsrH, leave indf == 0 if display event list changed"
//			fsrL = evptrpL; bitcopy(evtptrH, fsrH);
//			if (iobusy) //caller changed display event list
//			{
//			}
//	for (;;) //process next dimming timeslot
//	{
	if (IsFallingEdge(zc_filter)) goto next_cycle; //{ iobusy = TRUE; break; } //start new dimming cycle; truncate current display event if still active
//wait for current display event to finish:
	if (--evtdelay) //current display event is still active
	{
//			if (IsFallingEdge(zc_filter)) goto next_cycle; //{ iobusy = TRUE; break; } //cancel current event and start new dimming cycle
//		if (chplex) //turn outputs off again after 1 cycle (requires latching SSRs); sender could force this, but event list size would double do it automatically instead
//NOTE: don't do this for muxed pwm of dumb LEDs; select pwm I/O type and set a row
//		{
			trisbuf.bytes[0] = TRISA_INIT(CHPLEX); //set all rows to high-Z
			trisbuf.bytes[1] = TRISBC_INIT(CHPLEX);
//		}
		goto wait_next; //wait for timeslot to finish, update chplex I/O pins (might not be necessary, but this allows timing logic to be reused)
//			Wait4Timer(Timer0, 0, EMPTY, EMPTY); //wait for dimming timeslot to finish
//			continue;
	}
//		if (/*iobusy / *|| IsFallingEdge(zc_filter)* / || (/ *!zc_present &&*/ !indf) //rewind display event list; 0 delay after first event => end of list
//TODO: use NodeBytes instead of eof marker?
	if (!indf) //end of list => rewind display event list; NOTE: 0 delay for first event => max wait, not rewind
	{
		if (zc_present) goto wait_next; //don't rewind until ZC falling edge; NOTE: falling edge is assumed to occur before evtdelay 255 expires, but if it doesn't this will prevent next display event and avoid random dimming/flicker due to insufficient Triac voltage
next_cycle:
		fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP)/0x100; //initialize ptr back to first display event
//			iobusy = FALSE;
	}
//unpack next display event:
	evtdelay = indf_autonode; //get duration of next event; can be "0" for max delay on first event only; otherwise "0" means end of list
//TODO: make this generic or add cases for other pics; currently hard-coded for 4 of 6 pins on Port A, Port C
#if 0 //row then cols; doesn't work with row 0; 8+7+4+3 instr
//get row output (no row for pwm), split bits off by port:
	swap_WREG(indf); WREG &= 0x0F; //port A row bit
//TODO: make this generic or add cases for other pics; currently hard-coded for 4 of 6 pins on Port A, Port C
	/*if (indf & 0x80) WREG |= 0x10; /// *if (indf & 0x80)*/ WREG += 8; //A3 is input-only; shift it to A4
	portbuf.bytes[0] = WREG; //set row output pin high, columns low; these will be inverted below if they are common cathode
//		if (CommonCathode) compl(portbuf.bytes[0]); //= ~portbuf.bytes[0]; //rows are low, columns are high; BoostC doesn't seem to know about the COM instr
	WREG ^= TRISA_INIT(CHPLEX); //set row pin to Output ("0"), columns to Input (Hi-Z) = "1", special output pins remain as-is (SEROUT), and A4 is hard-wired to Input
	trisbuf.bytes[0] = WREG;
	if (ActiveHigh) portbuf.bytes[0] = WREG; //also update row + col pins with inverted values; //compl(portbuf.bytes[0]); //= ~portbuf.bytes[0]; //rows are low, columns are high; BoostC doesn't seem to know about the COM instr
	WREG = indf_autonode; WREG &= 0x0F; //port C row bit
	portbuf.bytes[1] = WREG; //set row output pin high, columns low
//		if (CommonCathode) compl(portbuf.bytes[1]); //= ~portbuf.bytes[1]; //rows are low, columns are high; BoostC doesn't seem to know about the COM instr
	WREG ^= TRISBC_INIT(CHPLEX); //set row pin to Output ("0"), columns to Input, special output pins remain as-is
	trisbuf.bytes[1] = WREG;
	if (ActiveHigh) portbuf.bytes[1] = WREG; //also update row + col pins with inverted values; //compl(portbuf.bytes[1]); //= ~portbuf.bytes[1]; //rows are low, columns are high; BoostC doesn't seem to know about the COM instr
//get column outputs, split bits off by port:
	swap_WREG(indf); WREG &= 0x0F; //port A column bits
	/*if (indf & 0x80) WREG |= 0x10; / /if (indf & 0x80)*/ WREG += 8; //A3 is input-only; shift it to A4
//		if (!CommonCathode) WREG = ~WREG; //cols are low instead of high
	trisbuf.bytes[0] ^= WREG; //set column pins also to Output (should have been set to Input when row was set above); A4 will be incorrect, but it's hard-wired to Input so it doesn't matter
	WREG = indf_autonode; WREG &= 0x0F; //port C column bits
//		if (!CommonCathode) WREG = ~WREG; //cols are low instead of high
	trisbuf.bytes[1] ^= WREG; //set column pins also to Output (should have been set to Input when row was set above)
#else //row then cols; 5+4+6+6
//paranoid: prevent multiple rows: 7 LEDs per I/O pin might damage I/O pins by over-driving them
#if 0 //def DEBUG
#warning "[INFO] Multi-row paranoid check is on; causes additional overhead"
//	rl_WREG(indf);
//	WREG &= indf;
//	if (!EqualsZ) PROTOCOL_ERROR(PROTOERR_MULTIROW);
	WREG = 0;
	if (indf & 1) ++WREG;
	if (indf & 2) ++WREG;
	if (indf & 4) ++WREG;
	if (indf & 8) ++WREG;
	if (indf & 16) ++WREG;
	if (indf & 32) ++WREG;
	if (indf & 64) ++WREG;
	if (indf & 128) ++WREG;
	if (WREG > 1) PROTOCOL_ERROR(PROTOERR_MULTIROW);
//NOTE: don't need to check row vs. col overlap because row tris will be remain input and everything will stay off; column effectively overrides row role for any given pin
#endif //DEBUG2
//common anode: invert columns (on = low), leave row transistor as-is (on = high)
//common cathode: columns as-is (on = high), invert row transistor (on = low)
//get row output (no row changes for pwm), split bits off by port:
	swap_WREG(indf); WREG &= 0x0F; //port A row bit
	/*if (indf & 0x80)*/ WREG += 8; //A3 is input-only; shift it to A4
	WREG ^= TRISA_INIT(CHPLEX); //set row pin to Output ("0"), columns to Input (Hi-Z) = "1", special output pins remain as-is (SEROUT), and A4 is hard-wired to Input
	trisbuf.bytes[0] = WREG;
	WREG = indf_autonode; WREG &= 0x0F; //port C row bit
	WREG ^= TRISBC_INIT(CHPLEX); //set row pin to Output ("0"), columns to Input, special output pins remain as-is
	trisbuf.bytes[1] = WREG;
//get column outputs, split bits off by port:
	swap_WREG(indf); WREG &= 0x0F; //port A column bits
	/*if (indf & 0x80)*/ WREG += 8; //A3 is input-only; shift it to A4
	trisbuf.bytes[0] ^= WREG; //set column pins also to Output (should have been set to Input when row was set above); A4 will be incorrect, but it's hard-wired to Input so it doesn't matter
	if (!ActiveHigh) WREG ^= 0xFF; //also update row + col pins with inverted values; //compl(portbuf.bytes[0]); //= ~portbuf.bytes[0]; //rows are low, columns are high; BoostC doesn't seem to know about the COM instr
	portbuf.bytes[0] = WREG; //set column output pins high (CA) or low (CC), row low (CA) or high (CC); these are inverted for common cathode
	WREG = indf_autonode; WREG &= 0x0F; //port C column bits
	trisbuf.bytes[1] ^= WREG; //set column pins also to Output (should have been set to Input when row was set above)
	if (!ActiveHigh) WREG ^= 0xFF; //also update row + col pins with inverted values; //compl(portbuf.bytes[1]); //= ~portbuf.bytes[1]; //rows are low, columns are high; BoostC doesn't seem to know about the COM instr
	portbuf.bytes[1] = WREG; //set column output pins high (CA) or low (CC), row low (CA) or high (CC)
#endif
//#ifndef PIC16X //check for bank wrap
////		add_banked
////TODO: add "reserved" param to add_banked and merge this code
//	WREG = fsrL & ~BANKLEN; /*use separate expr to avoid BoostC temp*/
//	WREG += 0 -(SFRLEN + 3 -1); //need 3 contiguous bytes for next display event
//	if (Borrow) /*bank wrap*/
//	{
//		fsrL -= WREG; //skip partial display event
//		fsrL -= GPRGAP; //move to next bank
//		if (Borrow) fsrH_adjust(-1);
//	}
//#endif
	if (PROTOCOL_PAGE != IOH_PAGE) TRAMPOLINE(1); //kludge: compensate for page selects
	add_banked(0, IsDecreasing(NODE_DIRECTION), 2); //adjust fsr for bank gaps/wrap; make sure 3 bytes are available for next time
//wait for time to flush next display event:
//up to ~ 66 instr + caller activity from dimming slot start until now ~= 12 usec @5 MIPS
wait_next:
//finish prep for next timeslot:
//#warning "TODO: tmr0 runs over by 3 @50 Hz; need to trim a little, but doesn't really matter since timing logic will catch up"
	if (Timer0Wrap && (tmr0 > 4)) { last_overflow = tmr0; PROTOCOL_ERROR(PROTOERR_TIMESLOT); } //firmware bug: overran dimming timeslot; not critical since we can "catch up" for next time
//Timer 0 with 4:1 scalar @18.432 MHz ~= .9 usec/inc, @32 MHz == .5 usec/inc
//50 Hz AC / 256 ~= 39 usec, 60 Hz AC / 256 ~= 32.6 usec; 1 dimming timeslot looks to Timer 0 like +36 (60 Hz @18 MHz), +43 (50 Hz @18 MHz), +65 (60 Hz @32 MHz), or +78 (50 Hz @32 MHz)
//Timer 0 preset should be one of: 212 = 0xd4 (50 Hz @18 MHz), 178 = 0xb2 (50 Hz @32 MHz), 220 = 0xdc (60 Hz @18 MHz), 190 = 0xbe (60 Hz @32 MHz)
#ifdef ASSUME_NOZCCHANGE //assume timing won't change and just use pre-calculated value
	WREG = tmr0_preset; //CAUTION: timing will be messed up if this gets overwritten
#else //ifndef ASSUME_NOZCCHANGE //re-calculate timer preset each time in case memory is overwritten or ZC conditions change
	WREG = TimerPreset(DIMSLICE(50), EVENT_OVERHEAD, Timer0, CLOCK_FREQ); //assume 50 Hz and then adjust if it was 60 Hz
	if (zc_60hz) WREG = TimerPreset(DIMSLICE(60), EVENT_OVERHEAD, Timer0, CLOCK_FREQ);
	if (ExtClockFailed) //try to maintain correct timing with slower clock
	{
		WREG = TimerPreset(DIMSLICE(50), EVENT_OVERHEAD, Timer0, MAXINTOSC_FREQ); //assume 50 Hz and then adjust if it was 60 Hz
		if (zc_60hz) WREG = TimerPreset(DIMSLICE(60), EVENT_OVERHEAD, Timer0, MAXINTOSC_FREQ);
	}
#endif
	FinishWaiting4Timer(Timer0, EMPTY); //wait for previous dimming timeslot to finish
	StartTimer_WREG(Timer0, CUMULATIVE_BASE); //start next dimming timeslot immediately; don't block
//2 instr to restart timer + 12 instr to flush ~= 3 usec @5 MIPS, 2 usec @8 MIPS, so total timing jitter is minimal; a 5 MIPS PIC is more than adequate for the task
//flush I/O pin changes:
	if (chplex) //NOTE: conditional stmt introduces a slight delay (2-3 instr), but it will be consistent so no jitter is introduced here
	{
		WREG = all_off;
		IFPORTA(porta = WREG, EMPTY); //kludge (anti-ghosting): set all PORT pins low before turning on TRIS to avoid extraneous output signals; safe for dedicated/pwm as well as chplex
		IFPORTBC(portbc = WREG, EMPTY);
		IFPORTA(trisa = trisbuf.bytes[0], EMPTY);
		IFPORTBC(trisbc = trisbuf.bytes[1], EMPTY);
	}
	IFPORTA(porta = portbuf.bytes[0], EMPTY);
	IFPORTBC(portbc = portbuf.bytes[1], EMPTY);
//start processing for next timeslot:
	zc_sample(); //sample ZC now, decide what to do during prep for next dimming timeslot
//handle serial data while waiting:
//must handle serial data >= 1x/40 usec for sustained 250k baud; CAUTION: some data can change here
//		if (IsCharAvailable || Timer1Wrap) //demo code uses Timer1
//		{
	xchg(fsrL, nodeptrL); fsrH9_xchg(nodeptrH_byte); //save my event pointer and restore caller's before returning
//			RETURN(); //return to caller (protocol handler); caller must return within ~ 32 usec (60 Hz is worst case) or next timeslot will be delayed
//			if (NEVER) WREG = eeadrL; //BANKSELECT(IsCharAvailable); //kludge: fake out compiler bank tracking to force bank select below
//ABSLABEL(ResumeChplex); //caller returns here after processing char
//			xchg(fsrL, nodeptrL); fsrH_xchg(nodeptrH_byte); //save caller data, restore my event pointer
//		}
//		if (iobusy) goto startio;
//	}
//set all I/O pins low ("0" is off for all other I/O types):
//this must be done if switching to a different I/O handler (some I/O pins might be left high-Z)
//	IFPORTA(porta = 0, EMPTY);
//	IFPORTBC(portbc = 0, EMPTY);	
//	IFPORTA(trisa = TRISA_INIT(NODES), EMPTY); //set all node I/O pins to Output
//	IFPORTBC(trisbc = TRISBC_INIT(NODES), EMPTY);
//	iobusy = TRUE;
//	TRAMPOLINE(2); //kludge: compensate for page select or address tracking bug
}
#else //def WANT_CHPLEX
 #define send_chplex()  PROTOCOL_ERROR(PROTOERR_EXCLUDED)
 #define is_chplex()  EqualsZ = FALSE
#endif //def WANT_CHPLEX


volatile bit SentNodes @BITADDR(adrsof(SENT_NODES));

//dispatch to active I/O handler:
//this code JUMPS to I/O handler, so return from I/O returns thru here also (saves one extra level of call/return overhead)
#define resume_nodes()  ABSCALL(ResumeNodeIO)
non_inline void send_nodes(void)
{
	ONPAGE(IOH_PAGE); //put on same page as I/O handlers to reduce page selects

//	iobusy = FALSE; //cancel async I/O (gece, pwm/chplex) and start fresh
	SentNodes = TRUE; //don't allow changes to node config after this point
//ABSLABEL(ResumeNodeIO); //alternate entry point to resume previous I/O
	WREG = NodeConfig >> 4;
	INGOTOC(TRUE); //#warning "CAUTION: pclath<2:0> must be correct here"
	pcl += WREG;
	PROGPAD(0); //jump table base address
	JMPTBL16(RENXt_NULLIO) return; //0x00 (0) = null I/O
	JMPTBL16(RENXt_FRPANEL) goto unimplemented; //0x01 = reserved for use with front panel (custom) or other in-line diagnostics
	JMPTBL16(RENXt_PWM(ACTIVE_LOW)) goto chplex; //0x03 = pwm, Common Cathode
	JMPTBL16(RENXt_PWM(ACTIVE_HIGH)) goto chplex; //0x02 = pwm, Common Anode
	JMPTBL16(RENXt_CHPLEX(ACTIVE_LOW)) goto chplex; //0x05 = chplex, Common Cathode
	JMPTBL16(RENXt_CHPLEX(ACTIVE_HIGH)) goto chplex; //0x04 = chplex, Common Anode
	JMPTBL16(RENXt_GECE(SERIES)) goto gece; //0x06 = GECE strings (max 63 ct in series)
	JMPTBL16(RENXt_GECE(PARALLEL)) goto gece; //0x07 = GECE strings (max 8x50 in parallel)
	JMPTBL16(/*RENXt_LPD6803(SERIES)*/ 8) goto unimplemented; //0x08 //0x16 (22) = LPD6803 strings
	JMPTBL16(/*RENXt_LPD6803(PARALLEL)*/ 9) goto unimplemented; //0x09 //0x16 (22) = LPD6803 strings
	JMPTBL16(RENXt_WS2811(SERIES)) goto ws2811_series; //0x0A (10) = WS281X LED strip, 1 series string
	JMPTBL16(RENXt_WS2811(PARALLEL)) goto ws2811_parallel; //0x0B (11) = WS281X LED strip, 4 parallel strings
	JMPTBL16(/*RENXt_WS2801(SERIES)*/ 12) goto unimplemented; //  0x0C //0x17 (23) = WS2801 strings
	JMPTBL16(/*RENXt_WS2801(PARALLEL)*/ 13) goto unimplemented; //  0x0D //0x17 (23) = WS2801 strings
	JMPTBL16(/*RENXt_TM1809(SERIES)*/ 14) goto unimplemented; //  0x08 //0x18 (24) = TMS1809 LED strip
	JMPTBL16(/*RENXt_TM1809(PARALLEL)*/ 15) goto unimplemented; //  0x09 //0x18 (24) = TMS1809 LED strip
	INGOTOC(FALSE); //check jump table doesn't span pages

//pwm/chplex:
chplex:
	send_chplex();
//	ABSGOTO(StartChplexIO);
	if (ALWAYS) return; //exit but prevent dead code removal
	
ws2811_series:
	send_ws2811_series_4bpp();
	if (ALWAYS) return; //exit but prevent dead code removal

ws2811_parallel:
	send_ws2811_parallel4_4bpp();
	if (ALWAYS) return; //exit but prevent dead code removal

gece:
	send_gece_4bpp();
	if (ALWAYS) return; //exit but prevent dead code removal

//gece_parallel:
////	if (NodeConfig & 0xF) send_parallel8_gece(); //enough bandwidth to always send parallel
//	send_gece_5mips();
//	if (ALWAYS) return; //exit but prevent dead code removal

unimplemented:
	PROTOCOL_ERROR(PROTOERR_UNIMPLOP); //just update error count; all opcodes in this group are same length, so remainder of packet can still be parsed correctly
}


//;==================================================================================================
//;Protocol handler
//;==================================================================================================

//address of this uController:
//uint8 MyAdrs @adrsof(MYADRS);
//volatile bit ProtocolListen @BITADDR(adrsof(PROTOCOL_LISTEN)); ///8.(adrsof(PROTOCOL_LISTEN) % 8); //initial 2 sec interval has passed
//volatile bit WantEcho @BITADDR(adrsof(WANT_ECHO)); ///8.(adrsof(PROTOCOL_LISTEN) % 8); //initial 2 sec interval has passed

volatile uint8 MyAdrs @adrsof(MYADRS);
volatile bit EscPending @BITADDR(adrsof(ESC_PENDING));
volatile uint8 bpp_0x03 @adrsof(ISBPP2)/8; //2-bit bpp; kludge: BoostC doesn't support bit sizes


//initialize protocol data:
/*non_*/inline void protocol_init(void)
{
//	ONPAGE(0);
	stats_init(); //prev init first

//	MyAdrs = ADRS_ALL; //respond to all packets until a specific address is assigned to me
//	protocol_errors = 0; //track unhandled opcodes and invalid packets (useful for diagnostics)
	ProtocolErrors = 0;
}
#undef stats_init
#define stats_init()  protocol_init() //initialization wedge, for compilers that don't support static initialization



//ignore remainder of current packet:
//#define OPCODE_BAD  TRUE
//#define OPCODE_OK  FALSE
//#define passthru(is_err)  ABSGOTO(is_err? BadOpcode: WaitForSync) //NOTE: can only be used with inline code

//get and echo next char, handle special protocol bytes:
//returns with status.Z set if Sync received
//volatile bit WantEcho @BITADRS(adrsof(PROTOCOL_ECHO)); //don't echo when modifying data stream
volatile bit IsPacketEnd @adrsof(STATUS).Z; //flag returned to caller; must be checked immediately or it will be lost
//volatile bit WantEcho @adrsof(STATUS).C_; //don't echo when modifying data stream; CAUTION: assumes no surrounding +/- arithmetic (otherwise status.C would be overwritten)
non_inline void GetRenardChar_WREG(void)
{
	ONPAGE(PROTOCOL_PAGE); //put on same page as protocol handler to reduce page selects

//	WantEcho = TRUE;
//	ABSLABEL(GetCharNoEcho); //alternate entry point (caller must initialize WantEcho)
//	protocol_inactive = FALSE; //tells send_node() helpers to not interrupt node I/O if data received
	for (;;) //keep trying to get a char
	{
//		if (iobusy) resume_nodes(); //resume I/O while waiting for next char
//		WaitChar_WREG();
//		BANKSELECT(pir1); //kludge: force bank select here to move it out of Wait4Char loop (gives lower wait loop overhead and latency)
		front_panel(~FP_RXIO); //WAIT_CHAR);
//		Wait4Char(); //busy-wait until char arrives
		for (;;)
		{
			SerialCheck(RECEIVE_FRAMERRS, break); //always return frame errors when caller wants serial data, so caller can decide how to handle it
			if (PROTOCOL_PAGE != IOH_PAGE) TRAMPOLINE(1); //kludge: compensate for page selects
			if (iobusy) resume_nodes();
			if (NEVER) break; //avoid dead code removal
		}
		front_panel(FP_RXIO); //GOT_CHAR);
		front_panel(~FP_COMMERR);
//NOTE: don't need to check for overruns as long as serial port has been polled every 44 usec (for sustained 250k baud at 8N2)
		if (HasFramingError) break; //goto comm_reset; //NOTE: must check for frame error before reading char
		GetChar_WREG();
//ABSLABEL(GotCharNeedsCheck);
		if (HasOverrunError) break; //goto comm_reset;
		if (!EscPending)
		{
#ifdef RENARD_PAD //pad chars are sent to compensate for clock differences between sender and receiver
			WREG ^= RENARD_PAD; //force compiler to preserve W
			if (EqualsZ) continue; //never echo Pad chars (sender is padding to avoid overrun due to faster clock)
			WREG ^= /*undo prev:*/ RENARD_PAD;
#endif
			if (WantEcho) PutChar_WREG(); //echo to downstream controllers
			WREG ^= RENARD_SYNC; //force compiler to preserve W
			if (!IsPacketEnd) //status.Z == IsPacketEnd
			{
//		if (IsPacketEnd /*EqualsZ*/) RETLW(RENARD_SYNC); //status.Z => got sync; preserve value in W for caller
				WREG ^= RENARD_ESCAPE ^ /*undo prev:*/ RENARD_SYNC; //force compiler to preserve W
				if (!EqualsZ) //Escape: take next char as-is
				{
					WREG ^= /*undo prev:*/ RENARD_ESCAPE; //restore char for caller
					IsPacketEnd = FALSE; //set this in case char was null
					return;
				}
				EscPending = TRUE;
				continue;
			}
			if (WantEcho) RETLW(RENARD_SYNC); //already echoed
			PutChar(RENARD_SYNC); //make sure Sync is echoed to downstream controllers
			WantEcho = TRUE; //start echo again for remainder of packet
			return; //CAUTION: IsPacketEnd (status.Z) should still be set here
		}
		if (WantEcho) PutChar_WREG();
		IsPacketEnd = FALSE; //treat as regular data even if it's a Sync
		EscPending = FALSE;
		return;
	}
	SerialErrorReset();
	front_panel(FP_COMMERR);
	WantEcho = FALSE; //remainder of packet is garbled, so don't pass it along
	IsPacketEnd = TRUE;
//			WREG = 0;
//			return; 
//			RETLW(RENARD_SYNC); //return dummy Sync char//NO; this can cause downstream PICs to acquire incorrect address, etc.
	RETLW(0);
//	TRAMPOLINE(1); //kludge: compensate for address tracking bug
}
#define GetCharRenard(inbuf, onsync, debug)  \
{ \
	GetRenardChar_WREG(); \
	if (IsPacketEnd) \
	{ \
		onsync; /*other special processing before start next packet; CAUTION: must preserve WREG if fall-thru*/ \
		nextpkt(EndOfPacket); \
	} \
	inbuf = WREG; /*don't overwrite if non-data char received*/ \
}


//pass thru data to downstream controllers while i'm still busy:
//NOTE: opcodes will be ignored; no local processing while another opcode is in progress
//#define BusyPassthru()  { if (IsCharAvailable) EchoChar_WREG(); } //don't process any special chars; 2 instr if false, 6+5 instr if true (optimized for false)


//prepare to send Renard byte:
//used only if caller wants to replace incoming char with something else; normally GetRenardChar() will echo incoming chars
//NOTE: this only does prep work; caller must send
non_inline void PutCharRenard_WREG(void)
{
	ONPAGE(PROTOCOL_PAGE); //put on same page as protocol handler to reduce page selects

	WantEcho = FALSE; //don't echo incoming char (will be replaced with something else)
//	if (IsRenardSpecial(WREG))
	WREG += 0 - RENARD_SPECIAL_MIN; //= WREG - MIN_RENARD_SPECIAL; //avoid operand swap
	if (NotBorrow) //byte >= MIN_RENARD_SPECIAL
	{
//#if RENARD_SPECIAL_MIN + 1 != RENARD_SPECIAL_MAX
		WREG += 0 - (RENARD_SPECIAL_MAX + 1) + /*undo prev:*/ RENARD_SPECIAL_MIN;
//#else
//		WREG += /*undo prev:*/ RENARD_SPECIAL_MIN;
//		WREG += 0 - (RENARD_SPECIAL_MAX + 1);
//#endif
		if (Borrow) //byte <= MAX_RENARD_SPECIAL; need Escape
		{
			GetCharRenard(WREG, /*NOECHO,*/ return, 401); //get placeholder input byte
//			GetRenardChar_WREG(); //defer Sync check to caller
			PutChar(RENARD_ESCAPE);
		}
	}
//	GetCharRenard(WREG, /*NOECHO,*/ return, 401); //get placeholder input byte
	GetRenardChar_WREG(); //defer Sync check to caller
//	PutChar(indf); //send requested data
}
#define PutCharRenard(byte, debug)  \
{ \
	WREG = byte; \
	PutCharRenard_WREG(); \
	if (IsPacketEnd) nextpkt(EndOfPacket); /*must be inline; can't be done within PutCharRenard_WREG()*/ \
	PutChar(byte); /*WREG was overwritten; reload and send*/ \
}


//unpack bits into 8 node values (using 4 bytes)
//node bit order is 7531 8642 => 0007 0008, 0005 0006, 0003 0004, 0001 0002 for more efficient unpacking
//14 instr on PIC16X, 17 instr on older PIC16 (+1 later)
inline void bpp1to4(uint8& inbuf)
{
	indf_autonode = inbuf & 0x11; rr_nc(inbuf);
	indf_autonode = inbuf & 0x11; rr_nc(inbuf);
	indf_autonode = inbuf & 0x11; rr_nc(inbuf);
	WREG = inbuf & 0x11;
}


//unpack bit pairs into 8 node values (using 2 bytes):
//node bit pair order is 3311 4422 => 0033 0044, 0011 0022 for more efficient unpacking
//7 instr on PIC16X, 8 instr on older PIC16 (+1 later)
inline void bpp2to4(uint8& inbuf)
{
	indf_autonode = inbuf & 0x33;
	rr_nc(inbuf); rr_nc(inbuf);
	WREG = inbuf & 0x33;
}


//total FIFO space needed for '688 + '1823 = 16 * 3 RGB + 8 * 50 / 2 data = 248 bytes; EXCEEDS FIFO
//total FIFO space needed for '1827 = 16 * 3 RGB + 12 * 50 / 2 data = 348 bytes; EXCEEDS FIFO
//FIFO capacity for '688 using overlapped GECE I/O:
//  available space: 2 * 80 bytes framebuf - 16*3 bytes RGB = 112 bytes
//  used space: #channels / 2 channels/byte * (1 - 87 usec/byte in / 800 usec/cmd * 8 channels/cmd / 2 channels/byte out)
//  capacity: 112 bytes * 2 channels/byte / (1 - 87 usec/byte in / 800 usec/cmd * 8 channels/cmd / 2 channels/byte out) ~= 396 channels; MAX 15 PALETTE ENTRIES or 49 CT STRING LENGTH
//FIFO capacity for '1823 using overlapped GECE I/O:
//  available space: 80 bytes framebuf - 16*3 bytes RGB = 32 bytes
//  used space: #channels / 2 channels/byte * (1 - 87 usec/byte in / 800 usec/cmd * 8 channels/cmd / 2 channels/byte out)
//  capacity: 32 bytes * 2 channels/byte / (1 - 87 usec/byte in / 800 usec/cmd * 8 channels/cmd / 2 channels/byte out) ~= 112 channels; MAX STRING LENGTH 14
//FIFO capacity for '1827 using overlapped GECE I/O (12 strings, 2 * 80 frame buffers):
//  available space: 2 * 80 bytes framebuf - 16*3 bytes RGB = 112 bytes
//  used space: #channels / 2 channels/byte * (1 - 87 usec/byte in / 800 usec/cmd * 12 channels/cmd / 2 channels/byte out)
//  capacity: 112 bytes * 2 channels/byte / (1 - 87 usec/byte in / 800 usec/cmd * 12 channels/cmd / 2 channels/byte out) ~= 644 channels; NO ISSUE

//16F688/1823: 8 strings, 50 nodes @4bpp = 200 bytes + palette
//16F1827: 12-13 strings, 50 nodes @4bpp (6-7 bytes/12-13 nodes) = 400 bytes + palette
//volatile bit rcv_bitmap @BITADDR(adrsof(RCV_BITMAP));
inline void get_bitmap_WREG(void)
{
//	volatile bit gece @BITADDR(adrsof(OVERLAPPED_IO)); //statebit_0x20)); //overlapped I/O flag (used only for GECE I/O during GetBitmap)
//NOTE: GetBitmap opcode not used for demo mode, so its RAM can be reused here; this allows SendNodes to use Protocol stack frame space and avoid RAM conflicts
	volatile uint8 rcv_count @adrsof(DEMO_STACKFRAME3) + 0; //PROTOCOL_STACKFRAME3) + 0; //#bytes to read (scaled)
	volatile uint8 skip_nodes @adrsof(DEMO_STACKFRAME3) + 1; //PROTOCOL_STACKFRAME3) + 0; //#bytes to read (scaled)
	volatile uint8 unpack_loop @adrsof(DEMO_STACKFRAME3) + 2; //PROTOCOL_STACKFRAME3) + 1;
//	volatile uint8 nodegrp @adrsof(statebit_0x20)/8; //3-bit node octet counter; together with send_count this gives an 11-bit node counter, which is enough for 1K bytes @4 bpp; NOTE: must be upper bits so Carry will not affect other bits; kludge: BoostC doesn't support bit sizes
//	volatile bit parity @adrsof(statebit_0x20)/8.(8-RAM_SCALE_BITS); //@BITADRS(statebit_0x80); //parity (bottom bit) of current node#; even nodes are in upper nibble, odd nodes are in lower nibble (4 bpp)
	volatile uint8 inbuf @adrsof(LEAF_PROC);
//	volatile bit overlapio @BITADDR(adrsof(OVERLAPPED_IO)); //overlapped I/O for GECE
	volatile bit gece @BITADDR(adrsof(statebit_0x20)); //gece special case

ABSLABEL(GetBitmapOpcode); //for debug; no functional purpose
//	iobusy = FALSE; //cancel async I/O (gece, pwm/chplex)
//	WREG = NodeConfig >> 4;
//	bitcopy(WREG == RENXt_GECE, gece); //GECE I/O is overlapped
//	GetCharRenard(NodeBytes, /*NO*/ECHO, EMPTY, 301 WHERE); //get bitmap length
//	count = NodeBytes;
//	SetBPP(4 BPP); //remember how framebuf is packed; determines #palette entries, node value size and packing
//	NodeBytes = MIN(Nodes2Bytes(150, 4 BPP), TOTAL_BANKRAM - (3<<4 BPP)) / RAM_SCALE; //set default node data size to 150 (5m * 30/m) for demo; determines #nodes
	unpack_loop = WREG & 7; //BPP_MASK; //;;only bottom 2 bits are used ("0" ==> 4)
	GetCharRenard(rcv_count, EMPTY, 301 WHERE); //NOTE: units are quad-bytes ALWAYS; TODO this is not necessarily the total size of the bitmap; it's only the size to process here before the next opcode (used for delayed opcodes), then bitmap will continue to arrive while next opcode is executing; this can be used to initiate overlapped I/O; 0 => no node I/O (Sync will arrive first)
	if (!SentNodes) NodeBytes = WREG; //rcv_count; //auto-set NodeBytes to total amount of data received; NOTE: this assumes io_delay is the full count, not just partial/delay; CAUTION: also assumes #expanded bytes < total RAM
//	bpp_0x03 |= WREG; //merge in new bits
	bpp_0x03 &= ~7;
	bpp_0x03 |= unpack_loop; //save packing mode
//	swap_WREG(NodeConfig); WREG ^= RENXt_GECE(DONT_CARE); WREG &= 0x0E; //WREG = NodeConfig >> 4; //split up expr to avoid BoostC generating a temp
	is_gece();
	bitcopy(IsGece, gece); //special case for gece fifo
	if (!gece) unpack_loop = 4; //RAM_SCALE; //IF(RAM_SCALE == 1, 4, RAM_SCALE); //use hard-coded ram_scale of 4 here to reduce bank check overhead
	else if (bpp_0x03 & 2) rcv_count <<= 1; //#bytes expanded after receive/unpack; bit pairs: 8 2-bit node values = 1 byte -> 2 bytes
	else if (bpp_0x03 & 1) rcv_count <<= 2; //#bytes expanded after receive/unpack; 8 1-bit node values: 1 byte -> 4 bytes
	GetCharRenard(skip_nodes, EMPTY, 301 WHERE); //node start offset; NOTE: units are quad-bytes ALWAYS (1 => byte ofs 4 == 8th node)
//;;;;NOTE: for GECE, last 16 nodes (8 bytes) are embedded within palette, which is sent before bitmap
	if (!SentNodes) //auto-set NodeBytes to total amount of data received; NOTE: this assumes io_delay is the full count, not just partial/delay; CAUTION: also assumes #expanded bytes < total RAM
	{
//TODO: take this out?
//ramsc   bpp   #rcv      NodeBytes
//  1    1,2,4  count x4  count x4,x2,x1
//  2    1,2,4  count x4  count x4x2/2,x2x2/2,x1x2/2
//  4    1,2,4  count x4  count x4x4/4,x2x4/4,x1x4/4 == count x4,x2,x1
//therefore total #bytes rcved depends on RAM_SCALE, but nothing else does
//for 1bpp on 16f1825: need to send 0x11, then #times thru loop (= # scaled bytes, / 4), then 4 * that many bytes, with total #bits == node count
//  0x11 8, 32 bytes => 256 nodes
		NodeBytes += skip_nodes; //= io_delay + skip_nodes;
//		if (bpp_0x03 & 2) NodeBytes += WREG; //#bytes expanded after receive/unpack; bit pairs: 8 2-bit node values = 1 byte -> 2 bytes
//		if (bpp_0x03 & 1) { NodeBytes += WREG; NodeBytes += WREG; NodeBytes += WREG; } //#bytes expanded after receive/unpack; 8 1-bit node values: 1 byte -> 4 bytes
		if (bpp_0x03 & 2) NodeBytes <<= 1; //#bytes expanded after receive/unpack; bit pairs: 8 2-bit node values = 1 byte -> 2 bytes
		if (bpp_0x03 & 1) NodeBytes <<= 2; //#bytes expanded after receive/unpack; 8 1-bit node values: 1 byte -> 4 bytes
//		if (RAM_SCALE < 2) { NodeBytes >>= 1; if (Carry) ++NodeBytes; } //divup; convert hard-coded ram_scale of 4 here to actual RAM_SCALE
//		if (RAM_SCALE < 4) { NodeBytes >>= 1; if (Carry) ++NodeBytes; } //divup
		if (RAM_SCALE < 2) NodeBytes <<= 1; //convert hard-coded ram_scale of 4 here to actual RAM_SCALE; CAUTION: assumes won't wrap
		if (RAM_SCALE < 4) NodeBytes <<= 1;
	}
//	if (gece)
//	{
//		GetCharRenard(node_adrs, EMPTY, 301 WHERE); //allow non-contiguous blocks for gece
//		if (!SentNodes) NodeBytes += WREG;
//	}
//	iobusy = FALSE; //cancel async I/O (gece, pwm/chplex)
	if (gece) //special case for gece: finish writing fifo command
	{
		gece_new_fifo_entry(ALLOC); //set fsr to next entry
//		if (fsrL == SFRLEN + 3) //kludge: don't separate GetBitmap and first node group
		indf_autonode = RENXt_BITMAP(0);
		indf_autonode /*gece_send_count*/ = rcv_count; //quad-bytes == node groups
		WREG = 0x40; if (ParallelNodes) WREG = ~0; indf_autonode = WREG; //mask
		indf /*gece_node_adrs*/ = skip_nodes + 1; //quad-bytes == node groups
//??		if (!ParallelNodes) indf <<= 2; //quad-bytes => individual nodes
//		++indf; //start adrs = 1
//		{
//			indf_autonode /*gece_adrs_mask*/ = ~0;
//			indf_autonode /*gece_node_adrs*/ = skip_nodes + 1; //quad-bytes == node groups
//			indf_autonode /*gece_send_count*/ = 0;
//		}
//		else
//		{
//			indf_autonode /*gece_adrs_mask*/ = 0x04; //series string on pin A2
//			indf_autonode /*gece_node_adrs*/ = skip_nodes << 2; //quad-bytes => individual nodes
//			++indf;
//			indf_autonode /*gece_send_count*/ = 0;
//		}
		gece_new_fifo_entry(COMMIT);
//		gece_more_fifo_data(FIRST_DATA); //alloc storage for next node group
	}
	else
	{
		fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //initialize ptr to first node
//skip ahead to requested node:
		if (RAM_SCALE > 2) { skip_nodes <<= 1; if (Carry) fsrH_adjust(fsrH, -2); } //assume linear addressing with > 256 bytes RAM
		if (RAM_SCALE > 1) { skip_nodes <<= 1; if (Carry) fsrH_adjust(fsrH, -1); }
		add_banked256(skip_nodes, IsDecreasing(NODE_DIRECTION), 0); //adjust fsr for bank gaps/wrap; only need to check every 4 bytes (16 actually, banked RAM will always be a multiple)
	}
//		WREG = skip_nodes + 1; //node start address: 0 => node 1, 1 => node 9, 2 => node 17, ...
//		WREG |= 0x80; //add eof marker in case next node group does not completely arrive
//		indf_autonode = WREG;
//		WREG = ~0; if (!ParallelNodes) WREG = 0x04; indf_autonode = WREG; //adrs mask; series string is on pin A2
//convert to actual #bytes to receive (for simpler flow control):
//	if (RAM_SCALE > 1) io_delay += WREG;
//	if (RAM_SCALE > 2) { io_delay += WREG; io_delay += WREG; }
//use an effective RAM_SCALE of 4 to reduce bank gap checking overhead and to prevent overlapped I/O from occurring before first node group fully received
//	nodegrp &= ~0xe0; //initialize node group counter
//	if (!SentNodes) NodeBytes = 0; //auto-set node count based on amount of data received; TODO: always set this?
	for (;;) //get next group of node values
	{
		GetCharRenard(/*indf_autonode*/ inbuf, EMPTY, 304 WHERE); //expecting packed node values
//kludge: expand to 4bpp here for uniform node storage; this takes more RAM, but allows the sender logic and max #nodes to be independent of encoding method
 		if (bpp_0x03 & 1) bpp1to4(inbuf); //unpack bits into 8 1-bit node values (1 byte -> 4 bytes); 17 instr
		if (bpp_0x03 & 2) bpp2to4(inbuf); //unpack bit pairs into 8 2-bit node values (1 byte -> 2 bytes); 8 instr
		indf_autonode = WREG;
//		if (!MyAdrs) { PROTOCOL_ERROR(PROTOERR_FIRMBUG); --MyAdrs; }
//#if RAM_SCALE > 1
//		++unpack;
//		WREG = unpack & (RAM_SCALE - 1);
//		if (!EqualsZ) continue; //node scaling is indivisible in order to allow a simple 8-bit count to be used
		if (--unpack_loop) continue; //node area scaling is indivisible in order to allow a simple 8-bit count to be used
//		unpack = 4; //bpp_0x03 & 7; //RAM_SCALE; //use hard-coded ram_scale of 4 to reduce add_banked overhead and ensure overlapped gece I/O doesn't start before first group of 8 nodes (4 bytes) received
//		nodegrp += 1<<(8-RAM_SCALE_BITS);
//		if (!Carry) continue;
//#endif
		if (gece)
		{
//			++gece_send_count;
			unpack_loop = bpp_0x03 & 7; //RAM_SCALE; //use hard-coded ram_scale of 4 to reduce add_banked overhead and ensure overlapped gece I/O doesn't start before first group of 8 nodes (4 bytes) received
			gece_new_fifo_entry(COMMIT); //APPEND); //add next node group to current fifo command
			if (PROTOCOL_PAGE != IOH_PAGE) TRAMPOLINE(1); //kludge: compensate for page selects
			if (!iobusy) send_gece_4bpp(); //iobusy = TRUE; //start sending immediately because GECE I/O takes so long
//			iobusy = TRUE; //if (!iobusy) send_gece_4bpp(); //start sending immediately because GECE I/O takes so long
//overlapped I/O:
//;;;;0 more bytes (@1 bpp), 1 more byte (@2 bpp), or 3 more bytes (@4 bpp) need to arrive before first data bit goes out
//;;;;worst case is 3 bytes * 44 usec == 132 usec; gece bit times are 30 usec, so at most only the first gece address (6 bits) could go out during this time
//			if (!iobusy) send_gece_4bpp(); //GECE I/O takes so long that we need to start sending as soon as first node group is available (overlapped I/O); rcv rate always faster than send rate so it's safe to start here, after first node group has been received
//			gece_node_overflow(==); //adjust fsrL for gece overflow nodes
//			bank_gaps_else(IsDecreasing(NODE_DIRECTION), 0, gece_fifo_wrap(<, 3)); //need at least 4 bytes left in fifo for next node group
//			gece_rxptrL = fsrL; fsrH_save(gece_rxptrH); //save ptr to last completed node group, in case next one is incomplete
//			indf_autonode = 0x80; //start of node data
//			io_delay -= bpp_0x03 & 7;
//			if (!--io_delay) break; //all node data received
		}
		else
		{
			unpack_loop = 4; //bpp_0x03 & 7; //RAM_SCALE; //use hard-coded ram_scale of 4 to reduce add_banked overhead and ensure overlapped gece I/O doesn't start before first group of 8 nodes (4 bytes) received
			add_banked(0, IsDecreasing(NODE_DIRECTION), 0); //adjust fsr for bank gaps/wrap; only need to check every 4 bytes (16 actually, banked RAM will always be a multiple)
		}
		if (!--rcv_count) break; //all node data received
//		if (--count) continue; //still room for more entries
//		if (!chksum_valid()) return;
//		if (!SentNodes) //TODO: always set this?
//		{
//			WREG = 1;
//			if (bpp_0x03 & 1) WREG = 4; //#bytes expanded after receive
//			if (bpp_0x03 & 2) WREG = 2; //#bytes expanded after receive
//			NodeBytes += WREG; //set node count based on amount of data received; don't commit until entire node group received
//		}
//memory:
//		if (gece) { send_gece_4bpp(); gece = FALSE; } //overlapped I/O; GECE I/O takes so long that we need to start sending immediately
//		if (NodeConfig == (RENXt_GECE << 4), gece); //GECE I/O is overlapped
//		if (!--io_delay) break; //all node data received
	}
//	rcv_bitmap = TRUE; //continue loading bitmap data during next opcode
//	WREG = NodeConfig >> 4; //split up expr to avoid BoostC generating a temp
//	if (WREG == RENXt_GECE) { iobusy = TRUE; /*SentNodes = TRUE*/; /*resume_nodes()*/ send_gece_4bpp(); nextpkt(WaitForSync); } //kludge: continue receiving bitmap during GECE node I/O
//	bitcopy(NodeConfig >= (RENXt_GECE << 4), iobusy); //cancel async I/O (gece, pwm/chplex) and start fresh
//	bitcopy(SentNodes, svsent); //save SentNodes for continuation of GetBitmap during GECE I/O
got_bitmap:
	if (PROTOCOL_PAGE != IOH_PAGE) TRAMPOLINE(1); //kludge: compensate for call across page boundary
	send_nodes(); //CAUTION: bitmap will continue to arrive during node I/O (only for gece)
//	SentNodes = TRUE; //don't allow changes to node config after this point
//	resume_nodes(); //send_nodes(); //continue remainder of GECE node I/O, or start node I/O for other types
//	iobusy = TRUE; //force node refresh when next opcode is processed
	nextpkt(CountWaitForSync); //unknown receive state (Sync might have arrived during node I/O); just pass thru remainder of this packet and wait for next
}


//set a list of nodes to designated palette entry:
inline void set_nodelist(uint8& palinx)
{
	volatile uint8 nextnodeL @adrsof(LEAF_PROC); //CAUTION: will be overwritten while processing opcode
	volatile uint8 nodecolor @adrsof(PROTOCOL_STACKFRAME3) + 0; //palette index# to set nodes to
//	volatile uint8 palptr @adrsof(PROTOCOL_STACKFRAME3) + 1; //indirect palette index: ptr to palette triplet
	volatile uint8 prevnodeL @adrsof(PROTOCOL_STACKFRAME3) + 2; //prev node ofs received
//	volatile bit prevnodeH @BITADRS(adrsof(statebit_0x10)); //upper bit of node#; 9-bit adrs allows 1<<9 == 512 bytes linear or 4 * 80 == 320 bytes banked
	volatile uint8 prevnodeH @adrsof(statebit_0x20)/8; //3-bit upper node#; together with prevnodeL this gives an 11-bit node# (actually, 8 * 240 == 1920), which is enough to fill 1K bytes @4 bpp; NOTE: must be upper bits so Carry/wrap will not affect other bits; kludge: BoostC doesn't support bit sizes
	volatile bit gece @BITADDR(adrsof(statebit_0x10)); //special case

ABSLABEL(SetNodeListOpcode); //for debug; no functional purpose
//	WREG = palinxL & 0xF; //strip off opcode
//	iobusy = FALSE; //cancel async I/O (gece)
//	swap_WREG(NodeConfig); WREG ^= RENXt_GECE(DONT_CARE); WREG &= 0x0E; //WREG = NodeConfig >> 4; //split up expr to avoid BoostC generating a temp
	is_gece();
	bitcopy(IsGece, gece); //special case for gece
//	if (gece && (RAM_SCALE < 2)) PROTOCOL_ERROR
	WREG = palinx;
	for (;;)
	{
		WREG &= 0xF; //strip off opcode; assumes 4bpp, but could be 1 or 2 bpp if value is smaller and duplicated
		if (EqualsZ) return; //0 => end of list; 0 is assumed to be the most frequent color so it came first; it never occurs within the node lists so use it as eof marker
		nodecolor = WREG;
		if ((CLOCK_FREQ >= 32 MHz) && ParallelNodes) set_parapal_ptr(nodecolor); //&& !gece) //use indirect index to get palette index triplet, then use that to set node groups
		swap_WREG(nodecolor); nodecolor |= WREG; //set even and odd nodes @ 4bpp
//		fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //initialize to first node + bank
		prevnodeL = 0; prevnodeH &= ~0xe0; //initialize prev node#
		for (;;) //read next node#
		{
			GetCharRenard(nextnodeL, EMPTY, 201 WHERE); //node#
	//		add_banked(1, IsDecreasing(PALETTE_DIRECTION)); //adjust fsr for bank gaps/wrap; NOTE: palette will always be in first bank
			WREG = nextnodeL - RENXt_NODELIST(0); //extract palette entry# from esc code
			if (EqualsZ) return; //0 => end of list; 0 is assumed to be the most frequent color so it came first; it never occurs within the node lists so use it as eof marker
			if (NotBorrow) //esc code; WREG == new palette entry# to use
			{
				WREG ^= nodecolor;
				WREG &= 0xF;
				if (!EqualsZ) { WREG ^= nodecolor; break; } //change to new color; restore WREG
				prevnodeH += (0x100>>3); //1<<(8-3); //no change to color => explicit bank switch; move to next node bank; 3 bits => 8 banks
				continue;
			}
//kludge: "next <= prev" is true for node ofs 0 but shouldn't be; disable implicit bank switch on all node#s mod 240; this requires sender to explicitly bank switch for this case
			if (nextnodeL && (nextnodeL <= prevnodeL)) prevnodeH += (0x100>>3); //0x20; //1<<(8-3); //implicit bank switch; move to next node bank; 3 bits => 8 banks
			prevnodeL = nextnodeL; //used for wrap check on implicit bank switch

			if (gece) //special case
			{
				gece_new_fifo_entry(ALLOC); //set fsr to next entry
				indf_autonode = RENXt_NODELIST(0);
				indf_autonode = nodecolor; //remember which color to set for later
				if (ParallelNodes)
				{
					WREG = nextnodeL; //which string and I/O pin (0..7)
					pin2mask_WREG();
					indf_autonode = WREG; //mask
					swap_WREG(nextnodeL); WREG &= 0x1F; indf = WREG; indf += WREG; //++indf; //divide by 8: node# 0..239 => gece node adrs 1..30
					if (prevnodeH) indf += 30; //node# 240..399 => gece node adrs 31..50
					++indf; //start adrs = 1
				}
				else
				{
					indf_autonode = 0x40; //series string on pin A2
					indf = nextnodeL + 1; //gece address of this node; ignore nextnodeH (node# can't be > 63)
				}
				gece_new_fifo_entry(COMMIT);
				if (PROTOCOL_PAGE != IOH_PAGE) TRAMPOLINE(1); //kludge: compensate for page selects
				if (!iobusy) send_gece_4bpp(); //iobusy = TRUE; //start sending immediately because GECE I/O takes so long
				continue;
			}
			fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //initialize to first node + bank
//move to upper banks (not needed when #nodes < 500):
			if ((RAM_SCALE > 2) && (prevnodeH & (0x20<<2))) //banks 4..7; can only skip 2 banks at a time due to 8-bit addressing
			{
				add_banked256(RENXt_NODELIST_BANKSIZE, IsDecreasing(NODE_DIRECTION), 0); //skip ahead 2 banks; adjust fsr for bank gaps/wrap
				add_banked256(RENXt_NODELIST_BANKSIZE, IsDecreasing(NODE_DIRECTION), 0); //skip ahead 2 more banks; adjust fsr for bank gaps/wrap
			}
			if ((RAM_SCALE > 1) && (prevnodeH & (0x20<<1))) //banks 2, 4, 6
			{
				add_banked256(RENXt_NODELIST_BANKSIZE, IsDecreasing(NODE_DIRECTION), 0); //skip ahead 2 banks; adjust fsr for bank gaps/wrap
			}
			rr_WREG(nextnodeL);
			if (prevnodeH.5 /*& 0x20*/) WREG += RENXt_NODELIST_BANKSIZE / 2; //banks 1, 3, 5, 7
//			WREG = node.bytes[1] + 0xFF; //pre-load Carry with node# & 256 so right-shift result is correct; works up to 512 nodes
//			rr_nc_WREG(node.bytes[0]);
//			SetNode_WREG(nextnodeL, nodecolor);
			add_banked256(WREG, IsDecreasing(NODE_DIRECTION), 0); //adjust fsr for bank gaps/wrap
//#warning "TODO: 1/2bpp"
			if ((CLOCK_FREQ >= 32 MHz) && ParallelNodes) //set node group (4 parallel nodes); NOTE: setting one node in group could change all nodes in group
			{
				fsrL |= 1; //point to start of node group
				copy_parapal2nodes();
				continue;
			}
			if (nextnodeL & 1) { indf &= 0xF0; WREG = nodecolor & 0xF; } //odd nodes in lower nibble
			else { indf &= 0xF; WREG = nodecolor & 0xF0; } //even nodes in upper nibble
			indf |= WREG;
//			if (!MyAdrs) { PROTOCOL_ERROR(PROTOERR_FIRMBUG); --MyAdrs; }
//			prevnodeL = nextnodeL; //used for wrap check on implicit bank switch
		}
	}
}


//convert series palette index to parallel palette index triplet:
//NOTE: there are 2 ways to set color with parallel palette entries: within the palette entries themselves (arbitrary 24-bit colors use 3 palette entries), or within the node palette index triplets (primary colors use combinations of black/0 and a single R/G/B value)
//inline void Series2Parallel(uint2x8& color)
//{
//	volatile uint8 gbits @adrsof(LEAF_PROC); //temp to hold rotated bits for green
//parallel palette indexing uses byte pairs: pRRR RRGG, GGGB BBBB
//	rl_WREG(color.bytes[0]); if (color.bytes[0] & 8) WREG += 8; color.bytes[0] += WREG; //convert palette entry# 0..15 to starting index 0..55; 0 => 0,1,2, 1 => 3,4,5, ..., 7 => 21,22,23, 8 => 32,33,34, 9 => 35,36,37, ..., 15 => 53,54,55; NOTE: top bit must be same for all 3 index#s (in same bank)
//	rl_nc_WREG(color.bytes[0]); WREG &= 0x1F << 1; gbits = WREG; swap(gbits); //copy and shift green index bits 0b345x 0012
//	color.bytes[1] = color.bytes[0] & 0x1F; //copy red index to blue index 0bxxxxxxxx, 0bxxxBBBBB
//	rl_nc(color.bytes[0]); rl_nc(color.bytes[0]); //move bank select and red index bits into position 0bpRRRRRxx, 0bxxxxxxxx
//	color.bytes[0] |= gbits & 3; color.bytes[1] |= gbits & 0xE0; //move green bits into position 0bxxxxxxGG, GGGxxxxx
//}


//set all nodes to selected palette entry:
//first palette entry is typically black, so this will turn off all nodes
//however, first palette entry can be set to alternate color or gradient for simple color effects across all nodes; used as "background" color
//NOTE: this function takes longer than 1 char time but does not consume further input, so Pad chars or noop opcodes *must* follow in current packet
#define set_all(palinx)  { WREG = palinx; set_all_WREG(); }
non_inline void set_all_WREG(void)
{
//	volatile bit gece @BITADRS(adrsof(statebit_0x40)); //interleave first 16 nodes with palette for GECE
	volatile uint8 count @adrsof(PROTOCOL_STACKFRAME3) + 0; //@adrsof(LEAF_PROC); //#node bytes to set
	volatile uint8 nodecolor @adrsof(PROTOCOL_STACKFRAME3) + 1; //palinx# to set nodes to
//	volatile uint8 nodecolor2 @adrsof(PROTOCOL_STACKFRAME3) + 2; //second byte for parallel palette entries
//	volatile uint8 quadbit_0xc0 @adrsof(statebit_0x80)/8; //2-bit node quadrature counter (avoids 4 copies of code); send_count+quadbit == 10-bit node counter; NOTE: must be upper bits so Carry can detect wrap; kludge: BoostC doesn't support bit sizes
//	volatile bit overlapio @BITADRS(adrsof(OVERLAPPED_IO)); //overlapped I/O for GECE
//	volatile bit gece @BITADDR(adrsof(statebit_0x20)); //special case
	volatile bit gece_demo_skip @BITADDR(adrsof(statebit_0x10));
	ONPAGE(PROTOCOL_PAGE); //put on same page as protocol handler to reduce page selects

	gece_demo_skip = FALSE;
ABSLABEL(SetAll_GECEAlternating); //alternate entry point
	iobusy = FALSE; //cancel async I/O (gece or pwm/chplex)
	front_panel(~FP_NODEIO);
	nodecolor = WREG & 0xF; //save value before WREG is lost; assume 4bpp; could be 1 or 2 bpp, but caller must replicate bits as appropriate
	if ((CLOCK_FREQ >= 32 MHz) && ParallelNodes) set_parapal_ptr(nodecolor); //&& !gece) //use indirect index to get palette index triplet, then use that to set node groups
	swap_WREG(nodecolor); nodecolor |= WREG; //set even and odd nodes @ 4bpp
//	swap_WREG(NodeConfig); WREG ^= RENXt_GECE(DONT_CARE); WREG &= 0x0E; //WREG = NodeConfig >> 4; //split up expr to avoid BoostC generating a temp
	is_gece();
//	bitcopy(IsGece, gece); //special case for gece
	if (IsGece) //special case for gece
	{
		gece_new_fifo_entry(ALLOC); //set fsr to next entry
		WREG = RENXt_SETALL(0); if (gece_demo_skip) WREG += 1; indf_autonode = WREG; //RENXt_SETALL(0);
		indf_autonode = nodecolor; //remember which color to set for later
#if 0
		if (SentNodes) //send to all nodes using broadcast address; NOTE: this can only be used to set brightness, not color
		{
			indf_autonode = ~0; //special address for all pixels (0x3F)
			indf = 1; //count
		}
		else //need to send to each individual node (initial address assignment)
#endif
		{
			indf_autonode = 0 + 1; //start address
			if (ParallelNodes)
			{
				indf = NodeBytes; //node pairs if RAM_SCALE == 1
				if (RAM_SCALE < 2) { indf >>= 1; if (Carry) ++indf; } //pairs ->quads
				if (RAM_SCALE < 4) { indf >>= 1; if (Carry) ++indf; } //quads -> node groups (1 node on each of 8 I/O pins)
			}
//BoostC uses a temp for this:			indf = NodeBytes << 1; //#nodes to send
			else { rl_WREG(NodeBytes); indf = WREG; } //#nodes to send (one pin only)
		}
		gece_new_fifo_entry(COMMIT);
		if (PROTOCOL_PAGE != IOH_PAGE) TRAMPOLINE(1); //kludge: compensate for page selects
		if (!iobusy) send_gece_4bpp(); //iobusy = TRUE; //start sending immediately because GECE I/O takes so long
		RETLW(0); //kludge: defeat chained goto here (crosses page boundaries)
		return;
	}
//initialize all nodes to first palette entry:
//	SetBPP(4 BPP); //default to 4 bpp; determines palette size and node packing
	fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //quadbit_0xc0 &= ~0xc0; //initialize first node ptr
//set all nodes to first palette entry (clears node data):
//this is only needed to support random access to nodes
//	NodeBytes = MIN(Nodes2Bytes(150, 4 BPP), TOTAL_BANKRAM - (3<<4 BPP)) / RAM_SCALE; //set default node data size to 150 (5m * 30/m) for demo; determines #nodes
	count = NodeBytes;
//#warning "TODO: stofs, len, palinx# ?  PWM/CHPLEX ?"
//#if CLOCK_FREQ >= 32 MHz
//	if (ParallelNodes) //use indirect index to get palette index triplet, then use that to set node groups
//	{
//;;4-bit parallel palette index points to 1 of 16 sub-palette triplets (stored as 2 bytes each); each of those points to 3 of 64 4-byte parallel node entries
//;;parallel palette sub-entries are 4 bytes each; up to 3 are used to set 24-bit color for 4 parallel nodes
//;;a triplet of 3 sub-entries
//		GetPalent(nodecolor.bytes[0]); //look up palette sub-entry triplet
//		nodecolor.bytes[0] &= 0x0F;
//		Series2Parallel(nodecolor); //convert to parallel palette triplet; packed as byte pair: pRRR RRGG, GGGB BBBB
//		nodecolor.bytes[1] += 0x12; //set BBBBB = 1 + GGGGG = 1 + RRRRR = 3 * palinx#, to allow arbitrary 24-bit colors to be set here
//		rl_WREG(nodecolor.bytes[0]); fsrL = WREG; WREG += PALETTE_ADDR(0, 4 BPP); fsrL += WREG; fsrH = PALETTE_ADDR(0, 4 BPP) / 0x100; //ptr to 1 of 16 palette triplets (2 bytes each)
//		nodecolor.bytes[0] = indf_autopal; nodecolor.bytes[1] = indf; //palette index triplet is stored as bRRRRRGG, GGGBBBBB; points to 3 other palette entries
//#endif //CLOCK_FREQ >= 32 MHz
	for (;;) //loop iteration takes approx 1 usec @8 MIPS or 1.6 usec @5 MIPS *4 => need one trailing NOOP for every 50 or so nodes (5 MIPS is worst case)
	{
		BusyPassthru(RECEIVE_FRAMERRS, 0); //sender must pad 1/87 bytes @115k baud (8 MIPS) or 1/55 bytes @115k baud (5 MIPS)
//CAUTION: above loop can take longer than a long time: 30 usec/node * 450 nodes == 13.5 msec; no further packets for this controller are accepted during that time
//NOTE: banked RAM will always be a multiple of 4 (16, actually), so fsrL does not need to be adjusted after each byte (reduces overhead)
//		WREG = 0; //palette entry# 0
		if ((CLOCK_FREQ >= 32 MHz) && ParallelNodes) //use indirect index to get palette index triplet, then use that to set node groups
		{
			copy_parapal2nodes();
			if (RAM_SCALE > 2) copy_parapal2nodes();
//			if (RAM_SCALE > 2) { copy_parapal2nodes(); copy_parapal2nodes(); } //MOVWI_NODE(fsrL); MOVWI_NODE(fsrL); } /*add_banked(1, NON_INLINED, NODE_DIRECTION);*/
			if (!--count) return; //|| !--count) return; //update remaining byte count (2 bytes per parallel node group); avoid fractional node groups
			if ((RAM_SCALE < 2) && (!--count)) return;
			add_banked(0, IsDecreasing(NODE_DIRECTION), 0); //adjust fsr for bank gaps/wrap; only need to check every 16 bytes
			continue;
		}
		indf_autonode = nodecolor; //MOVWI_NODE(fsrL); /*add_banked(1, NON_INLINED, NODE_DIRECTION);*/
		if (RAM_SCALE > 1) indf_autonode = WREG; //MOVWI_NODE(fsrL); /*add_banked(1, NON_INLINED, NODE_DIRECTION);*/
		if (RAM_SCALE > 2) { indf_autonode = WREG; indf_autonode = WREG; } //MOVWI_NODE(fsrL); MOVWI_NODE(fsrL); } /*add_banked(1, NON_INLINED, NODE_DIRECTION);*/
		if (!--count) break; //update remaining byte count

//do it a few more times inline here to cut down on loop overhead:	
		indf_autonode = WREG; //MOVWI_NODE(fsrL); /*add_banked(1, NON_INLINED, NODE_DIRECTION);*/
		if (RAM_SCALE > 1) indf_autonode = WREG; //MOVWI_NODE(fsrL); /*add_banked(1, NON_INLINED, NODE_DIRECTION);*/
		if (RAM_SCALE > 2) { indf_autonode = WREG; indf_autonode = WREG; } //MOVWI_NODE(fsrL); MOVWI_NODE(fsrL); } /*add_banked(1, NON_INLINED, NODE_DIRECTION);*/
		if (!--count) break; //update remaining byte count

		indf_autonode = WREG; //MOVWI_NODE(fsrL); /*add_banked(1, NON_INLINED, NODE_DIRECTION);*/
		if (RAM_SCALE > 1) indf_autonode = WREG; //MOVWI_NODE(fsrL); /*add_banked(1, NON_INLINED, NODE_DIRECTION);*/
		if (RAM_SCALE > 2) { indf_autonode = WREG; indf_autonode = WREG; } //MOVWI_NODE(fsrL); MOVWI_NODE(fsrL); } /*add_banked(1, NON_INLINED, NODE_DIRECTION);*/
		if (!--count) break; //update remaining byte count

		indf_autonode = WREG; //MOVWI_NODE(fsrL); /*add_banked(1, NON_INLINED, NODE_DIRECTION);*/
		if (RAM_SCALE > 1) indf_autonode = WREG; //MOVWI_NODE(fsrL); /*add_banked(1, NON_INLINED, NODE_DIRECTION);*/
		if (RAM_SCALE > 2) { indf_autonode = WREG; indf_autonode = WREG; } //MOVWI_NODE(fsrL); MOVWI_NODE(fsrL); } /*add_banked(1, NON_INLINED, NODE_DIRECTION);*/
		if (!--count) break; //update remaining byte count

		add_banked(0, IsDecreasing(NODE_DIRECTION), 0); //adjust fsr for bank gaps/wrap; only need to check every 16 bytes
//		bank_adjust(0, IsDecreasing(NODE_DIRECTION), if (gece) gece_fifo_wrap(0)); //adjust fsr for bank gaps/wrap; only need to check every 16 bytes
//		fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //quadbit_0xc0 &= ~0xc0; //initialize ptr to first node
//		count -= 16 / RAM_SCALE; //update remaining byte count
//		count -= 4 / RAM_SCALE; //update remaining byte count
//		if (/*count <= 0*/ Borrow || EqualsZ) break; //use separate stmt for avoid BoostC temp
	}
//TODO: send now?
//	if (!MyAdrs) { PROTOCOL_ERROR(PROTOERR_FIRMBUG); --MyAdrs; }
//	TRAMPOLINE(2); //kludge: compensate for page select or address tracking bug
//NOTE: must return to caller here
}
//#define set_all(palinx)  { WREG = palinx; set_all_WREG(); }


//set node values:
inline void set_nodes(uint8& adrsmode)
{
ABSLABEL(SetNodesOpcode); //for debug; no functional purpose
	WREG = adrsmode & 7; //0xF;
	INGOTOC(TRUE); //#warning "CAUTION: pclath<2:0> must be correct here"
//#ifndef PORTC //ADRS-KLUDGE
//	++pclath; //kludge for 12F1840
//#endif
	pcl += WREG;
	PROGPAD(0); //jump table base address
//	JMPTBL(RENXt_NOOP & 0xF) return; //0x00 (0) = noop
	JMPTBL16(RENXt_BITMAP(0 BPP)) goto clear_all_op; //0x02 (2) = all nodes are the same value
	JMPTBL16(RENXt_BITMAP(1 BPP)) goto bitmap_op; //0x02 (2) = use remaining bytes as full bitmap 1bpp
	JMPTBL16(RENXt_BITMAP(2 BPP)) goto bitmap_op; //0x03 (3) = use remaining bytes as full bitmap 2bpp
	JMPTBL16(0x3) goto unused; //0xF# = unused
	JMPTBL16(RENXt_BITMAP(4 BPP)) goto bitmap_op; //0x04 (4) = use remaining bytes as full bitmap 4bpp
	JMPTBL16(RENXt_DUMBLIST & 7) goto dumb_list_op; //goto dumb_displist_op; //0x05 (5) = use remaining bytes as dumb pixel display event list (chplex/pwm)
	JMPTBL16(RENXt_NODEBYTES & 7) goto node_count_op; //force node count (in prep for set_all/clear_all)
	JMPTBL16(0x7) goto unused; //goto nodestring_op; //0x07 (7) = parallel ledstrip strings
//	JMPTBL16(0x9) return; //0xF# = unused
//	JMPTBL16(0xA) return; //0xF# = unused
//	JMPTBL16(0xB) return; //0xF# = unused
//	JMPTBL16(0xC) return; //0xF# = unused
//	JMPTBL16(0xD) return; //0xF# = unused
//	JMPTBL16(0xE) return; //0xF# = unused
//	JMPTBL16(0xF) return; //0xF# = unused
	INGOTOC(FALSE); //check jump table doesn't span pages

dumb_list_op: //dumb display event list comes in same format as smart node bitmaps, so just reuse the code
	WREG = 0; //(RENXt_BITMAP(0 BPP) - 1); //force WREG to be 0 below to turn off special bit unpacking
bitmap_op:
//	WREG += 0 - (RENXt_BITMAP(0 BPP) - 1); //WREG = 1..4
//	SetBPP_WREG(); //framebuf packing determines #palette entries and node value size; CAUTION: bottom 2 bits of opcode are used here
	get_bitmap_WREG();
	if (ALWAYS) return; //exit but prevent dead code removal

node_count_op:
	GetCharRenard(WREG, EMPTY, 100); //TODO: only allow certain sizes? (to cut down on stray data problems)
	if (SentNodes) //NodeBytes should be already set; don't change it
	{
		WREG ^= NodeBytes;
		if (!EqualsZ) PROTOCOL_ERROR(PROTOERR_NODECFG); //count as error if doesn't match
		return;
	}
	NodeBytes = WREG;
	SentNodes = TRUE; //lock in NodeBytes value so it won't change; NOTE: this is needed if caller sends a bitmap before first I/O occurs
//allow sender to always deal in quad-bytes so config info doesn't need to change when devices are rearranged:
	if (RAM_SCALE < 2) { NodeBytes <<= 1; if (Carry) ++NodeBytes; } //byte pairs => node pairs
	if (RAM_SCALE < 4) { NodeBytes <<= 1; if (Carry) ++NodeBytes; } //quad bytes => node pairs
	if (ALWAYS) return; //exit but prevent dead code removal

clear_all_op:
	GetPalent(0);
	eeadrL = 3; //bkg color ofs in eeprom
	EEREAD(TRUE); DimmingCurve(eedatL); indf_autopal = WREG;
	EEREAD_AGAIN(TRUE); DimmingCurve(eedatL); indf_autopal = WREG;
	EEREAD_AGAIN(FALSE); DimmingCurve(eedatL); indf_autopal = WREG;
//	SetPalette(0, BLACK);
	set_all(0); //set all nodes to bkg color (first palette entry); NOTE: this also works for parallel if 0 or already in RAM
	nextpkt(CountWaitForSync); //unknown receive state (Sync might have arrived during node I/O); just pass thru remainder of this packet and wait for next
	if (ALWAYS) return; //exit but prevent dead code removal

unused:
	nextpkt(BadOpcode); //don't know how many bytes to consume for opcode, so ignore remainder of packet
}


//store RGB palette for smart nodes:
//this allows palette to be shared across controllers or frames, and makes bitmap size smaller for more efficient comm
//RGB palette is stored at start of frame buffer.
//normal order is R, G, B, R, G, B, ..., but sender should rearrange as needed for special node types (ie, WS2811 with swapped R/G or GECE)
//NOTE: GECE reorders to RRR...,GGG...,BBB... for more efficient palette addressing
//by convention, first palette entry is treated as "background" color; nodes that support random addressing (ie, GECE) can support transparency
inline void set_palette(uint8& palentsL)
{
	volatile bit palentsH @BITADDR(adrsof(statebit_0x20)); //upper bit of count; 9-bit adrs allows 1<<9 == 512 bytes linear or 4 * 80 == 320 bytes banked
	volatile bit gece @BITADDR(adrsof(statebit_0x40)); //gece special case

ABSLABEL(SetPaletteOpcode); //for debug; no functional purpose
	iobusy = FALSE; //cancel async I/O (gece and pwm/chplex)
	front_panel(~FP_NODEIO);
//	swap_WREG(NodeConfig); WREG ^= RENXt_GECE(DONT_CARE); WREG &= 0x0E; //WREG = NodeConfig >> 4; //split up expr to avoid BoostC generating a temp
	is_gece();
	bitcopy(IsGece, gece); //special case for gece
	WREG = palentsL & 0xF; //strip off opcode; leaves #palette entries
	if (EqualsZ) WREG = 16; //"0" => 16 entries (no reason to update 0 entries)
	x3_WREG(palentsL); palentsH = FALSE; //3 * palents
//NOTE: even though parallel palette addressing is currently implemented using 4-byte monochrome units, parallel palette entries are set in units of 24 bytes (assume R, G, and B monochrome values are needed); this allows up to 384 bytes of palette space
	if ((RAM_SCALE > 1) && ParallelNodes && !gece) { x8(palentsL); if (Carry) palentsH = TRUE; } //8 * palents; does not apply to GECE
#if 0 //doesn't work with parallel palette (9 bits)
	WREG = TOTAL_BANKRAM/RAM_SCALE - palents;
//	WREG -= palents;
//	WREG -= palents; //TOTAL_BANKRAM/RAM_SCALE - 3 * numents == max space available for node data
	WREG = NodeBytes - WREG;
	if (NotBorrow) NodeBytes -= WREG; //NodeBytes >= TOTAL_BANKRAM/RAM_SCALE - 3 * numents; reduce #nodes if overwritten by palette
#endif
//#warning "TODO: stofs/palent#, checksum?"
//	if (gece) gece_palette_not_full = 0xFF; //kludge: mark palette entry 10 so GetBitmap knows whether to skip or wrap; max brightness = 0xCC
	fsrL = PALETTE_ADDR(0, 4 BPP); fsrH = PALETTE_ADDR(0, 4 BPP) / 0x100; //initialize ptr to first palette entry
	for (;;) //read up to 16 * 3 bytes series palette data, or 16 * 24 bytes parallel palette data
	{
		GetCharRenard(indf_autopal, EMPTY, 201 WHERE); //expecting R/G/B byte
//		swap(indf); //kludge: prep for WS2811 I/O to port A
//		--fsrL;
//kludge: GECE palette entries are stored in a different order for more efficient addressing during node I/O; rearrange them here:
#if 0 //TODO: convert RGB to GECE IBGR?  not needed if sender does it
		if (gece) //RGB,RGB,RGB,... => RRR...,GGG...,BBB... (it's actually IBGR instead of RGB, but this code doesn't care); CAUTION: overwrites first 16 nodes
		{
//TODO: convert RGB to IBGR here? I = max(R, G, B), B = B / (I / 15), G = G / (I / 15), R = R / (I / 15)
//0xFF,FF,FF => 0xCC,F,F,F
//0x00,00,00 -=> 0x00,x,x,x
//0x40,80,FF => 0xCC,4,8,F => 0x36,6D,CC
//0xCC,FF,FF => 0xCC,C,F,F => 0xA3,CC,CC
//0x10,20,30 => 0x30,5,A,F
			fsrL += 15;
			if (fsrL >= PALETTE_ADDR(16, 4 BPP)) fsrL -= (3<<4) - 1; //wrap to beginning of next entry
		}
#endif
//		if (!MyAdrs) { PROTOCOL_ERROR(PROTOERR_FIRMBUG); --MyAdrs; }
#warning "TODO: limit R+G+B to 511? (to avoid excess current and brightness)"
		add_banked(0, IsDecreasing(PALETTE_DIRECTION), 0); //adjust fsr for bank gaps/wrap; NOTE: palette will always be in first bank, except
		if (--palentsL) continue; //still room for more entries
		if (palentsH) { palentsH = FALSE; continue; } //9-bit counter
//		if (!chksum_valid()) return;
		return; //received all palette entries; other opcodes may follow within same packet
	}
}


//set node type:
//only allow set if clear or reset if not clear; don't allow direct change from one type to another
inline void set_node_type(uint8& node_type)
{
ABSLABEL(SetNodeTypeOpcode); //for debug; no functional purpose

	iobusy = FALSE; //cancel async I/O (gece, pwm/chplex)
	front_panel(~FP_NODEIO);
//	IFPORTA(trisa = TRISA_INIT(NODES), EMPTY); //kludge: reset in case chplex was used
//	IFPORTBC(trisbc = TRISBC_INIT(NODES), EMPTY); //all Output on port B or C (serial port must be set to Output, doesn't matter on non-extended PICs)
	swap_WREG(node_type);
	WREG ^= NodeConfig;
	WREG &= 0xF0;
	if (EqualsZ) return; //no change
	if (SentNodes) //don't change it
	{
		PROTOCOL_ERROR(PROTOERR_NODECFG); //count as error if it changed
		return;
	}
#if 0
    WREG = NodeConfig & 0xF0;
    if (!EqualsZ) //node type is already set
    {
		swap_WREG(node_type); WREG &= 0xF0;
        if (node_type & 0x0F) { PROTOCOL_ERROR(PROTOERR_NODECFG); return; } //don't change it
        NodeConfig = 0; //&= 0xF0; //allow it to be reset
        return;
    }
#endif
	NodeConfig &= ~0xF0; //strip off old type
//	swap_WREG(node_type); WREG &= 0xF0; //strip off opcode
//	NodeConfig |= WREG; //set new node type
	NodeConfig |= node_type << 4; //strip off opcode, set new node type
	nodepin_init();
//NOTE: no need to set SentNodes here; GetBitmap will not change node type
//	WREG ^= RENXt_GECE(DONT_CARE) << 4; //WREG = NodeConfig >> 4; //split up expr to avoid BoostC generating a temp
//	WREG &= 0xE0; //ignore series vs. parallel
//	TRAMPOLINE(2); //kludge: compensate for page select or address tracking bug
//clear prev node data:
	if (LEAST_PAGE != PROTOCOL_PAGE) TRAMPOLINE(1); //kludge: compensate for call across page boundary
	gece_init(); //set initial gece port + fifo state
//takes too long	set_all(0); //must send to all nodes to initialize addresses;;;; incoming serial data ignored during init (echo is off)
}


//enumerate/self-assign next address:
//address enum command handler:
//Normally, incoming data is echoed as-is immediately.
//However, the incoming data stream must be altered in some way so each controller can choose a unique address.
//Since RenardRGB controllers are serial daisy-chained, the only ways to do this are to alter an existing byte or add a new byte to the outgoing data stream.
//This function will increment a data byte (adrs) in the pkt.  Sender should send a Pad char after, in case altered byte becomes a special Renard char and needs to be escaped.
//However, the logic below tries to avoid the need for a Pad char.
//NOTE: this mechanism is not compatible with half-duplex/broadcast-style DMX, because datastream is altered and each controller needs tx as well as rx.
#if 0 //obsolete
inline void EnumCtlr(void)
{
	volatile uint8 inbuf @adrsof(LEAF_PROC); //CAUTION: will be overwritten while processing opcode

	MyAdrs = 1; //initialize to first valid address
	for (;;)
	{
		GetCharRenard(inbuf, NOECHO, MyAdrs = ADRS_ALL, 401); //get next enumerated uController; leave my address unassigned if premature end of packet
		if (!inbuf) //auto-assign this address and tell sender I'm here
		{
#if IsRenardSpecial(DEVICE_CODE & 0xFF) || ((DEVICE_CODE & 0xFF) == ADRS_NONE) || ((DEVICE_CODE & 0xFF) == ADRS_ALL)
 #error "[ERROR] Device code conflict with protocol chars: "DEVICE""
#endif
			PutChar(DEVICE_CODE); //claim current address and tell sender what my capabilities are; bottom byte uniquely identifies uController type
			nextpkt(WaitForSync); //keep MyAdrs and ignore remainder of packet
		}
		PutChar_WREG();
//		for (;;) //compute next address
//		{
			++MyAdrs; //address belongs to another uController; skip over
			NOPE(if (!(MyAdrs + 1) || EqualsZ) ++MyAdrs /*continue*/); //don't use null address; NOTE: this will only happen with > 250 uControllers, so ignore it
//			WREG = MyAdrs;
//			WREG ^= RENARD_SYNC; //force compiler to preserve W
//			if (EqualsZ) continue; //don't use protocol chars for address
//			WREG ^= RENARD_PAD ^ /*undo prev:*/ RENARD_SYNC; //force compiler to preserve W
//			if (EqualsZ) continue; //don't use protocol chars for address
//			WREG ^= RENARD_ESCAPE ^ /*undo prev:*/ RENARD_PAD; //force compiler to preserve W
//			if (EqualsZ) continue; //don't use protocol chars for address
			WREG = MyAdrs - RENARD_SPECIAL_MIN;
			if (Borrow) continue; //MyAdrs < MIN_RENARD_SPECIAL; typical case (< 120 uControllers)
#if RENARD_SPECIAL_MIN + 1 != RENARD_SPECIAL_MAX
			WREG += 0 - RENARD_SPECIAL_MAX + 1 + /*undo prev:*/RENARD_SPECIAL_MIN;
			if (Borrow) MyAdrs = RENARD_SPECIAL_MAX + 1; //MyAdrs <= MAX_RENARD_SPECIAL; bump MyAdrs to avoid special protocol values
#else
			WREG += -1;
			if (EqualsZ) MyAdrs = RENARD_SPECIAL_MAX + 1; //MyAdrs <= MAX_RENARD_SPECIAL; bump MyAdrs to avoid special protocol values
#endif
//		}
	}
}
#endif


//read reg/memory data:
//used for status check or performance analysis by sender
//address & 0x8000 => flash, & 0x4000 => banked RAM, & 0x2000 => EEPROM
//sender must know exact addresses wanted, and pad packet to receive requested data
inline void ReadReg(void)
{
	volatile uint8 inbuf @adrsof(LEAF_PROC); //CAUTION: will be overwritten while processing opcode

ABSLABEL(ReadRegOpcode); //for debug; no functional purpose
	GetCharRenard(inbuf, EMPTY, 501); //reg address
	if (inbuf.7) // & (1<<7)) // & (INROM(0) / 0x100)) //read from ROM
	{
		WREG &= ~0x80;
		eeadrH = WREG; //inbuf & ~(INROM(0) / 0x100);
		GetCharRenard(eeadrL, EMPTY, 502);
		for (;;) //remainder of packet determines read length
		{
			PROGREAD(INC_ADRS); //gets 1.75 bytes each time; send back as 2 bytes
			PutCharRenard(eedatL, 401); //little endian; need to send 2 bytes for each prog word
			PutCharRenard(eedatH, 402);
//			if (!MyAdrs) { PROTOCOL_ERROR(PROTOERR_FIRMBUG); --MyAdrs; }
			if (NEVER) break; //avoid dead code removal
		}
	}
	if (inbuf.6) // & (1<<6)) // & (INRAM(0) / 0x100); //) //read from RAM; TODO: banked vs. linear?
	{
		WREG &= ~0xC0;
#ifdef PIC16X //linear addressing; no bank gaps; 9-bit counter allows up to 511 bytes (address space is 1-to-1)
		fsrH = WREG; //inbuf & ~(INRAM(0) / 0x100); //NOTE: only useful in linear addressing mode
#else
		bitcopy(!EqualsZ, fsrH); //only 1 bit in upper address byte
#endif
		GetCharRenard(fsrL, EMPTY, 502);
		for (;;) //remainder of packet determines read length
		{
			PutCharRenard(indf, 403);
//			if (!MyAdrs) { PROTOCOL_ERROR(PROTOERR_FIRMBUG); --MyAdrs; }
			++fsrL; if (EqualsZ) fsrH_adjust(fsrH, +1); //++fsrH; DON'T adjust for bank gaps; this allows SFR to be read
//			NOPE(add_banked(1, IsDecreasing(PALETTE_DIRECTION), 0)); //adjust fsr for bank gaps/wrap
			if (NEVER) break; //avoid dead code removal
		}
	}
	if (inbuf.5) // & (1<<5)) // & (INEEPROM(0) / 0x100); //) //read from EEPROM
	{
		WREG &= ~0xE0;
		eeadrH = WREG; //inbuf & ~(INEEPROM(0) / 0x100); //useless (<= 256 bytes EEPROM)
		GetCharRenard(eeadrL, EMPTY, 502);
		for (;;) //remainder of packet determines read length
		{
			EEREAD(TRUE);
			PutCharRenard(eedatL, 401);
//			if (!MyAdrs) { PROTOCOL_ERROR(PROTOERR_FIRMBUG); --MyAdrs; }
			if (NEVER) break; //avoid dead code removal
		}
	}
	nextpkt(IllegalOpcode); //unknown memory; ignore remainder of packet
//	add_banked256(WREG, IsDecreasing(PALETTE_DIRECTION), 0); //kludge: expand inline code once
}


//write reg data:
//used to alter run-time behavior
inline void WriteReg(void)
{
	volatile uint8 inbuf @adrsof(LEAF_PROC); //CAUTION: will be overwritten while processing opcode

ABSLABEL(WriteRegOpcode); //for debug; no functional purpose
	GetCharRenard(inbuf, EMPTY, 501); //reg address
#if 0
	if (inbuf.7) // & (1<<7)) // & (INROM(0) / 0x100)) //TODO: write to flash
	{
		WREG &= ~0x80;
		eeadrH = WREG; //inbuf & ~(INROM(0) / 0x100);
		GetCharRenard(eeadrL, EMPTY, 502);
		for (;;) //remainder of packet determines read length
		{
			PROGREAD(INC_ADRS); //gets 1.75 bytes each time
			PutCharRenard(eedatL, 401);
			PutCharRenard(eedatH, 402);
			if (NEVER) break; //avoid dead code removal
		}
	}
#endif
	if (inbuf.6) // & (1<<6)) // & (INRAM(0) / 0x100)) //write to RAM; TODO: banked vs. linear?
	{
		WREG &= ~0xC0;
#ifdef PIC16X //linear addressing; no bank gaps; 9-bit counter allows up to 511 bytes (address space is 1-to-1)
		fsrH = WREG; //inbuf & ~(INRAM(0) / 0x100); // & ~0x40;
#else
		bitcopy(!EqualsZ, fsrH);
#endif
		GetCharRenard(fsrL, EMPTY, 502);
		GetCharRenard(WREG, EMPTY, 501); //mini-checksum byte
		WREG ^= fsrL;
#ifdef PIC16X //linear addressing; might be > 256 bytes RAM
		WREG ^= fsrH;
#else
		if (fsrH) WREG ^= 1; // one bit
#endif
		WREG ^= RENXt_WRITE_REG;
		if (!EqualsZ) nextpkt(IllegalOpcode); //kludge: extra protection byte (mini-checksum) before clobbering memory
		for (;;) //remainder of packet determines write length
		{
			GetCharRenard(indf, EMPTY, 403);
//			if (!MyAdrs) { PROTOCOL_ERROR(PROTOERR_FIRMBUG); --MyAdrs; }
			++fsrL; if (EqualsZ) fsrH_adjust(fsrH, +1); //++fsrH; DON'T adjust for bank gaps; this allows SFR to be written
//			NOPE(add_banked(1, IsDecreasing(PALETTE_DIRECTION), 0)); //adjust fsr for bank gaps/wrap
		}
	}
	if (inbuf.5) // & (1<<5)) // & (INEEPROM(0) / 0x100)) //write to EEPROM
	{
		WREG &= 0xE0;
		eeadrH = WREG; //inbuf & ~(INEEPROM(0) / 0x100);
		GetCharRenard(eeadrL, EMPTY, 502);
		GetCharRenard(WREG, EMPTY, 501); //mini-checksum byte
		WREG ^= eeadrL;
		WREG ^= RENXt_WRITE_REG;
		if (!EqualsZ) nextpkt(IllegalOpcode); //kludge: extra protection byte (mini-checksum) before clobbering memory
		for (;;) //remainder of packet determines write length
		{
			GetCharRenard(eedatL, EMPTY, 401);
			EEWRITE(TRUE);
			if (NEVER) break; //avoid dead code removal
		}
	}
#warning "TODO: specific reg addresses here and above?"
	nextpkt(IllegalOpcode); //unknown memory; ignore remainder of packet
}

//convert single hex digit (lower nibble) to ASCII:
#define tohex(val)  { WREG = val; tohex_WREG(); }
non_inline void tohex_WREG(void)
{
	ONPAGE(PROTOCOL_PAGE);

	WREG &= 0x0F;
	WREG += 0-10;
	if (NotBorrow) WREG += 'A' - ('9' + 1);
	WREG += 10 + '0'; //undo prev -= and conv to ascii char
}


//return short ascii string to make it easier to check comm port using Putty:
inline void tty_test(void)
{
	volatile uint8 outbuf @adrsof(LEAF_PROC);

	PutCharRenard('#', 403);
	swap_WREG(MyAdrs); tohex(WREG); outbuf = WREG; PutCharRenard(outbuf, 403);
	tohex(MyAdrs); outbuf = WREG; PutCharRenard(outbuf, 403);
	PutCharRenard(' ', 403);
	PutCharRenard('v', 403);
	tohex(RENXt_VERSION >> 4); outbuf = WREG; PutCharRenard(outbuf, 403);
	PutCharRenard('.', 403);
	tohex(RENXt_VERSION); outbuf = WREG; PutCharRenard(outbuf, 403);
	PutCharRenard('\r', 403);
	PutCharRenard('\n', 403);
	WantEcho = TRUE; //resume normal processing of packet
}


//return packet status to sender (mostly for debug):
inline void packet_status(void)
{
ABSLABEL(PktStatusOpcode); //for debug; no functional purpose
	volatile uint8 outbuf @adrsof(LEAF_PROC);

	outbuf = 0; //TODO: put more status-at-a-glance in here
	if (ioerrs) outbuf |= 0x80;
	if (ProtocolErrors) outbuf |= 0x40;
//	if (WantEcho) outbuf |= 0x02;
	if (SentNodes) outbuf |= 0x10;
	outbuf |= bpp_0x03 & 0x0F;
	PutCharRenard(outbuf, 403);
	PutCharRenard(ioerrs, 403);
	PutCharRenard(ProtocolErrors, 403);
	PutCharRenard(NodeBytes, 403);
	PutCharRenard(wait4sync, 403);
	WantEcho = TRUE; //resume normal processing of packet
}


//re-sample ZC:
//normally this is done only once @startup (we assume it's not going to change)
//however, if ZC wasn't valid @startup, this opcode will allow it to be re-sampled
//also useful for hardware debug of ZC circuit
//PIC16X could re-sample continuously during pwm/chplex but older PICs don't have another timer available (Timer 0 is used for dimming timeslots, Timer 1 for demo pattern timing)
//NOTE: serial input is ignored during re-sample, for simplicity; this opcode is intended for hardware diagnosis only
inline void zc_resample(void)
{
	iobusy = FALSE; //cancel async I/O (gece, pwm/chplex) and start fresh
	front_panel(~FP_NODEIO);
	IFPORTA(trisa = TRISA_INIT(CHPLEX), EMPTY); //turn off channels to avoid leaving them on solid for 2 sec
	IFPORTBC(trisbc = TRISBC_INIT(CHPLEX), EMPTY);
	if (PROTOCOL_PAGE != LEAST_PAGE) TRAMPOLINE(1); //kludge: compensate for call across page boundary
	wait_2sec_zc(); //watch ZC for 1 sec
}


//restart controller (partial or full):
//partial restart clears I/O stats and enables acquisition of address and node type; also turn port pins back on
//full restart jumps to address 0, which basically reinitializes everything; NOTE: this will cause serial port errors in downstream PICs
inline void ctlr_restart(void)
{
	GetCharRenard(WREG, EMPTY, 101 WHERE); //expecting Sync only; echo turned back on when Sync received
	WREG ^= ~RENXt_RESET; //additional byte for safety (don't want controller to reset during a sequence!)
	if (EqualsZ) //soft reset run-time state only
	{
//		stats_init(); //this will be done in main()
//		InitNodeConfig();		
//reload default config from eeprom:
		eeadrL = 0; //start of EEPROM
		EEREAD(TRUE); NodeConfig = eedatL;
		EEREAD_AGAIN(TRUE); NodeBytes = eedatL;
		EEREAD_AGAIN(TRUE); MyAdrs = eedatL;
//		if (!MyAdrs) { PROTOCOL_ERROR(PROTOERR_FIRMBUG); --MyAdrs; }
//		if (EqualsZ) --MyAdrs; //kludge: acquire new adrs if invalid
//		IFPORTA(porta = 0, EMPTY); //set all (output) pins low; do this before setting tris to avoid stray pulses in case any pins were high
//		IFPORTBC(portbc = 0, EMPTY);
//		IFPORTA(trisa = TRISA_INIT(NODES), EMPTY); //kludge: recover if chplex was called
//		IFPORTBC(trisbc = TRISBC_INIT(NODES), EMPTY); //kludge: recover if chplex was called
		nodepin_init();
		SentNodes = FALSE; //allow node type, size to be set again
		return; //ret back to main()
	}
	WREG ^= RENXt_RESET ^ /*undo prev:*/ ~RENXt_RESET; //additional byte for safety (don't want controller to reset during a sequence!)
	if (EqualsZ) ABSGOTO(0); //pcl = 0; //0x78 (120) = total reset controller (software reset); use 11-bit address instead of just 8 bits so pclath doesn't need to be changed; also hard-return to demo mode (free-running demo/test pattern)
	PROTOCOL_ERROR(PROTOERR_UNGRANTED); //invalid restart opcode
}


//misc controller functions:
inline void ctlr_func(uint8& funccode)
{
ABSLABEL(CtlrFuncOpcode); //for debug; no functional purpose
	WREG = funccode & 0xF;
	INGOTOC(TRUE); //#warning "CAUTION: pclath<2:0> must be correct here"
//#warning "FIX THIS"
//#ifdef IRP //16f688 ADRS-KLUDGE
//	--pclath; //kludge: fix address tracking
//#endif
	pcl += WREG;
	PROGPAD(0); //jump table base address
//	JMPTBL16(/*RENXt_ENUM*/ 0) goto unused; //enum_opc; //0x70 (112) enumerate/assign address
	JMPTBL16(RENXt_CLEARSTATS) goto clear_stats_opc;
	JMPTBL16(RENXt_READ_REG) goto read_reg_opc; //0x72 (114) = read registers, address and length follow
	JMPTBL16(RENXt_WRITE_REG) goto write_reg_opc; //0x73 (115) = write registers, address, length and data follow
	JMPTBL16(RENXt_SAVE_EEPROM) goto unused; //goto save_eeprom_op; //0x76 (118) = save current palette + node values to EEPROM
	JMPTBL16(RENXt_ACK) goto pkt_status_op; //0x77 (119) = 
	JMPTBL16(RENXt_RESET) goto reset_ctlr_op; //ABSGOTO(0); //pcl = 0; //0x78 (120) = reset controller (software reset); use 11-bit address instead of just 8 bits so pclath doesn't need to be changed; also hard-return to demo mode (free-running demo/test pattern)
	JMPTBL16(RENXt_REFLASH) goto unused; //goto reflash_op; //0x79 (121) = bootloader (reflash)
	JMPTBL16(RENXt_NODEFLUSH) goto send_nodes_opc; //0x79 (121) = send out node data
	JMPTBL16(0x8) goto unused; //0xF# = unused
	JMPTBL16(0x9) goto unused; //0xF# = unused
	JMPTBL16(RENXt_ZCRESAMPLE) goto zc_resample_opc; //0xF# = unused
	JMPTBL16(RENXt_TTYACK) goto tty_test_op; //0xF# = unused
	JMPTBL16(0xC) goto unused; //0xF# = unused
	JMPTBL16(/*RENARD_PAD*/ 0xD) goto unused; //0xAC (172) = unused; DON'T USE = Pad (7D)
	JMPTBL16(RENARD_SYNC) goto unused; //0xAD (173) = unused; DON'T USE = Sync (7E)
	JMPTBL16(RENARD_ESCAPE) goto unused; //0xAE (174) = unused; DON'T USE = Escape (7F)
	INGOTOC(FALSE); //check jump table doesn't span pages

//bitmap_op:
//	SetBPP_WREG(); //framebuf packing determines #palette entries and node value size; CAUTION: bottom 2 bits of opcode are used here
//	get_bitmap();
//	if (ALWAYS) return; //exit but prevent dead code removal

//enum_opc:
//	EnumCtlr(); //enumerate/self-assign next address
//	if (ALWAYS) return; //exit but prevent dead code removal

read_reg_opc:
	ReadReg();
	if (ALWAYS) return; //exit but prevent dead code removal

write_reg_opc:
	WriteReg();
	if (ALWAYS) return; //exit but prevent dead code removal

//read_stats_opc:
//	read_stats();
//	if (ALWAYS) return; //exit but prevent dead code removal

//clear_stats_opc:
//	clear_stats();
//	if (ALWAYS) return; //exit but prevent dead code removal

//save_eeprom_opc:
//	if (ALWAYS) return; //exit but prevent dead code removal
//#warning "use this option to set EEPROM from plug-in (pre-programmed RenardCard message or RGB50 pattern)"

pkt_status_op:
	packet_status();
	if (ALWAYS) return; //exit but prevent dead code removal

reset_ctlr_op:
	ctlr_restart();
	if (ALWAYS) return; //exit but prevent dead code removal

//reflash_opc:
//	if (ALWAYS) return; //exit but prevent dead code removal

send_nodes_opc:
	iobusy = FALSE; //cancel async I/O; tells gece and pwm/chplex to restart if they were already running
	if (PROTOCOL_PAGE != IOH_PAGE) TRAMPOLINE(1); //kludge: compensate for call across page boundary
	send_nodes();
	nextpkt(CountWaitForSync); //unknown receive state (Sync might have arrived during node I/O); just pass thru remainder of this packet and wait for next
//	iobusy = TRUE; //force node refresh before next opcode is processed
//	send_nodes(); //OTOH, go ahead and do it now, while there is time remaining in current time slice
	if (ALWAYS) return; //exit but prevent dead code removal

clear_stats_opc:
	stats_init();
//	protocol_errors = 0;
	if (ALWAYS) return; //exit but prevent dead code removal

zc_resample_opc:
	zc_resample();
	if (ALWAYS) return; //exit but prevent dead code removal

tty_test_op:
	tty_test();
	if (ALWAYS) return; //exit but prevent dead code removal

unused:
	nextpkt(BadOpcode); //don't know how many bytes to consume for opcode, so ignore remainder of packet
}


//top-level opcode dispatch:
inline void opcode(uint8& opc)
{
ABSLABEL(OpcodeDispatch); //for debug; no functional purpose
	WREG = opc >> 4;
	INGOTOC(TRUE); //#warning "CAUTION: pclath<2:0> must be correct here"
	pcl += WREG;
	PROGPAD(0); //jump table base address
	JMPTBL16(RENXt_NOOP) return; //goto unused; //0x0# = unused; could define 15 new opcodes in this range if needed
	JMPTBL16(RENXt_SETNODE_OPGRP >> 4) goto set_nodes_op; //0x1# = set node values (in memory)
//	JMPTBL16(RENXt_IOH_OPGRP >> 4); goto ioh_select_op; //0x1# = I/O handler select
	JMPTBL16(RENXt_SETPAL(0 BPP) >> 4) goto set_palette_op; //0x2# = set palette
//	JMPTBL16(RENXt_NODEOFS >> 4); goto set_nodeofs_op; //0x3# = set node ofs
//	JMPTBL16(RENXt_FXFUNC_OPGRP >> 4); goto fx_func_op; //0x4# = various fx functions (future)
	JMPTBL16(RENXt_SETALL(0) >> 4) goto set_all_op; //0x3# = unused
	JMPTBL16(RENXt_SETTYPE(0) >> 4) goto set_node_type_op; //0x4# (64..79) = set node type if not already set
	JMPTBL16(0x5) goto unused; //0x5# = unused
	JMPTBL16(0x6) goto unused; //0x6# = unused
	JMPTBL16(RENXt_CTLFUNC_OPGRP >> 4) goto ctlr_func_op; //0x7# = various controller functions
	JMPTBL16(0x8) goto unused; //0x8# = unused
	JMPTBL16(0x9) goto unused; //0x9# = unused
	JMPTBL16(0xA) goto unused; //0xA# = unused
	JMPTBL16(0xB) goto unused; //0xB# = unused
//	JMPTBL16(RENXt_INTERLEAVE >> 4) goto set_interleave_op; //0xC# = set #parallel strings
//	JMPTBL16(RENXt_PALENT >> 4) goto set_palent_op; //0xD# = set palette entry#
//	JMPTBL16(RENXt_REPEAT >> 4) goto set_repeat_op; //0xE# = set repeat count
	JMPTBL16(0xC) goto unused; //0xC# = unused
	JMPTBL16(0xD) goto unused; //0xD# = unused
	JMPTBL16(0xE) goto unused; //0xE# = unused
//	JMPTBL16(RENXt_NODEESC_OPGRP >> 4) goto node_escapes_op; //0xF# = various node list escape codes
	JMPTBL16(RENXt_NODELIST(0) >> 4) goto nodelist_op; //0xF# = unused
	INGOTOC(FALSE); //check jump table doesn't span pages

set_nodes_op:
	set_nodes(opc);
	if (ALWAYS) return; //exit but prevent dead code removal

//ioh_select_op:
//	ioh_select(opc);
//	if (ALWAYS) return; //exit but prevent dead code removal

set_palette_op:
	set_palette(opc);				
	if (ALWAYS) return; //exit but prevent dead code removal

set_all_op:
	set_all(opc);				
	nextpkt(CountWaitForSync); //unknown receive state (Sync might have arrived during node I/O); just pass thru remainder of this packet and wait for next
	if (ALWAYS) return; //exit but prevent dead code removal

set_node_type_op:
	set_node_type(opc);
	if (ALWAYS) return; //exit but prevent dead code removal

//set_nodeofs_op:
//	set_nodeofs(opc);				
//	if (ALWAYS) return; //exit but prevent dead code removal

//set_interleave_op:
//	set_interleave(opc);
//	if (ALWAYS) return; //exit but prevent dead code removal
	
//fx_func_op:
//	fx_func(opc);				
//	if (ALWAYS) return; //exit but prevent dead code removal

ctlr_func_op:
	ctlr_func(opc);				
	if (ALWAYS) return; //exit but prevent dead code removal

//set_palent_op:
//	set_palent(opc);				
//	if (ALWAYS) return; //exit but prevent dead code removal

//set_repeat_op:
//	set_repeat(opc);				
//	if (ALWAYS) return; //exit but prevent dead code removal

nodelist_op:
	set_nodelist(opc);				
	if (ALWAYS) return; //exit but prevent dead code removal

//node_escapes_op:
//	node_escapes(opc);				
//	if (ALWAYS) return; //exit but prevent dead code removal

//passthru:
//forward remaining packet data to downstream PICs without processing it:
//	if (IsPacketEnd) return;
//	for (;;) //get next byte of data packet
//	{
//		ifnot_GetCharRenard(WREG, ECHO, return); //;Sync or timeout; end of packet
//		if (!AdrsAssigned) PutChar_raw_WREG(); //echo to downstream PICs
//	}
unused:
	/*if (opc != RENXt_NOOP)*/ nextpkt(BadOpcode); //don't know how many bytes to consume for opcode, so ignore remainder of packet
}


//main protocol handler:
//only called when serial data is wanted or detected (otherwise demo pattern runs)
non_inline void protocol(void)
{
	ONPAGE(PROTOCOL_PAGE);

#if 0
#warning "[INFO] DUMB ECHO TEST"
	GetChar_WREG();
	PutChar_WREG(); //echo to downstream controllers
#endif
#warning "TODO: baud rate auto-detect?"
//always clear frame errors and resume what caller was doing (regardless of whether protocol was active) -DJ 10/23/14
	if (HasFramingError) { comm_reset: SerialErrorReset(); WantEcho = FALSE; return; } //continue whatever caller was doing   /*NoSerialWaiting = TRUE*/ /*ignore frame errors so open RS485 line doesn't interrupt demo sequence*/ \
	if (!protocol_inactive) //just pass char to downstream controllers (if echo is on); avoid recursion
	{
//		if (HasFramingError) { comm_reset: SerialErrorReset(); WantEcho = FALSE; return; } //continue whatever caller was doing   /*NoSerialWaiting = TRUE*/ /*ignore frame errors so open RS485 line doesn't interrupt demo sequence*/ \
		GetChar_WREG();
		if (HasOverrunError) goto comm_reset;
		if (WantEcho) PutChar_WREG(); //echo to downstream controllers
		return; //resume wat caller was doing
	}
//	if (/*!NoSerialWaiting*/ ProtocolListen) //most common case is on, so check in here instead of making caller check it
//	{
//	if (protocol_inactive) //avoid recursion (thru send_ws2811 and set_all); NOTE: protocol is just pass-thru at this point
//	{
//activate protocol handler:
//	PROTOCOL_ERROR(PROTOERR_TRACE); //debug trace info
//or just set MiscBits = 0:
	protocol_inactive = FALSE; //tells send_node() helpers to not interrupt node I/O if data received
	SentNodes = FALSE; //allow node config changes (type and count) until first nodes sent
	WantEcho = FALSE; //don't echo anything until Sync received; echo should already be off, but reset it here just in case not
	ProtocolErrors = 0; //debug or diagnostics
//	protocol_errors = 0;
	iostats_init();

//#ifdef IRP //16f688; kludge: force orrect code page below for 16f688  ADRS-KLUDGE
//	GetCharRenard(WREG, EMPTY, 101 WHERE); //expecting Sync only; echo turned back on when Sync received
//	GetCharRenard(WREG, EMPTY, 101 WHERE); //expecting Sync only; echo turned back on when Sync received
//	GetCharRenard(WREG, EMPTY, 101 WHERE); //expecting Sync only; echo turned back on when Sync received
//#endif
//	}
//#define BusyPassthru()  { if (IsCharAvailable) EchoChar_WREG(); } //don't process any special chars; 2 instr if false, 6+5 instr if true (optimized for false)
//	EchoChar(); //consume char to avoid overrun error; don't echo until first Sync received
//	if (ALWAYS) return;

//start of packet:
//4 possible states:
// - Sync received during previous packet; start new packet
// - invalid opcode; pass thru remainder of packet and wait for next sync; echo without processing since it can't be reliably decoded but other controllers might want the data
// - comm error; discard remainder of packet (no echo) and wait for next sync, since it's missing data
// - all data received from previous packet; wait for next sync
	volatile uint8 inbuf @adrsof(LEAF_PROC); //CAUTION: will be overwritten while processing opcode
#if 0
	nextpkt(WaitForSync);
ABSLABEL(BadOpcode); //come here if packet contains invalid data (remainder of packet cannot be interpreted reliably)
	PROTOCOL_ERROR(PROTOERR_UNIMPLOP);
	WREG = 0; //fake out Sync check below
ABSLABEL(EndOfPacket); //inline code jumps here at end of packet; 2 cases: Sync received, or comm error (remainder of packet will be ignored since it got trashed)
	WREG ^= RENARD_SYNC; //check if Sync was received (still in WREG after return from GetRenardChar)
	if (!EqualsZ) //didn't get Sync, so wait for it
	{
//		SerialErrorReset();
ABSLABEL(WaitForSync); //inline code jumps here to ignore remainder of packet
		for (;;) //pass-thru while wait for Sync
		{
			GetCharRenard(WREG, EMPTY, 101 WHERE); //expecting Sync only; echo turned back on when Sync received
//			if (!MyAdrs) { PROTOCOL_ERROR(PROTOERR_FIRMBUG); --MyAdrs; }
			if (NEVER) break; //avoid dead code removal
		}
	}
#else
ABSLABEL(CountWaitForSync); //inline code jumps here to ignore remainder of packet
	wait4sync = 0;
	for (;;) //pass-thru while wait for Sync
	{
ABSLABEL(WaitForSync); //inline code jumps here to ignore remainder of packet
		inc_nowrap(wait4sync);
//	for (;;) PutCharRenard('W', 403); //allow caller to see when I finished
		GetCharRenard(WREG, EMPTY, 101 WHERE); //expecting Sync only; echo turned back on when Sync received
//		if (!MyAdrs) { PROTOCOL_ERROR(PROTOERR_FIRMBUG); --MyAdrs; }
//	if (!WantEcho) txreg = 'E';
		if (ALWAYS) continue;
ABSLABEL(BadOpcode); //come here if packet contains invalid data (remainder of packet cannot be interpreted reliably)
		PROTOCOL_ERROR(PROTOERR_UNIMPLOP);
//		unused3.bytes[1] = inbuf;
#if 0 //make it easier to see where the problem was in comm trace:
		WantEcho = FALSE;
		for (;;)
		{
			GetCharRenard(WREG, EMPTY, 101 WHERE); //expecting Sync only; echo turned back on when Sync received
			if (NEVER) break;
			txreg = 'B';
		}
#endif
//		WREG = 0; //fake out Sync check below
		if (ALWAYS) continue;
ABSLABEL(IllegalOpcode); //come here if packet contains illegal data (rejected); ignore remainder of packet
		PROTOCOL_ERROR(PROTOERR_UNGRANTED);
		if (ALWAYS) continue;
ABSLABEL(EndOfPacket); //inline code jumps here at end of packet; 2 cases: Sync received, or comm error (remainder of packet will be ignored since it got trashed)
		WREG ^= RENARD_SYNC; //check if Sync was received (still in WREG after return from GetRenardChar)
		if (!EqualsZ) { /*if (!WantEcho) txreg = 'S'*/; continue; } //didn't get Sync, so keep waiting for it
		break; //got sync
	}
#endif
//	TRAMPOLINE(2); //kludge: compensate for page select or address tracking bug

//TODO: FLIP(1)
//got sync, check recipient address:
	WantEcho = FALSE; //don't echo next incoming char in case it's for me (might need to replace it)
	GetCharRenard(inbuf, /*NOECHO,*/ EMPTY, 401); //get recipient address
	WantEcho = TRUE; //echo remainder of packet
//	if ((inbuf & 0x80) || (inbuf > MyAdrs)) //already processed or not addressed specifically to me
//	if ((inbuf & 0x80) || !inbuf || (inbuf > MyAdrs)) //already processed or not addressed specifically to me
//	if ((inbuf - 1 >= 0x7F) || (inbuf > MyAdrs)) //already processed or not addressed specifically to me; CAUTION: should be TRUE for inbuf == 0
//	if (!MyAdrs) { --MyAdrs; PROTOCOL_ERROR(PROTOERR_FIRMBUG); } //kludge: acquire new adrs if invalid; shouldn't happen
//	if (!inbuf) PROTOCOL_ERROR(0x25);
	WREG = inbuf - 1; //split up expr to avoid BoostC generating a temp
	WREG += 0-0x7F; //inbuf - 1 >= 0x80 - 1; true for 0 as well
	if (NotBorrow || ((inbuf != MyAdrs) && (MyAdrs != ADRS_ALL))) //already processed or not addressed specifically to me; CAUTION: should be TRUE for inbuf == 0 as well (invalid/null address)
	{
		PutChar(inbuf);
		if (inbuf != ADRS_ALL) nextpkt(WaitForSync); //pass-thru and wait for next Sync
	}
	else
	{
		MyAdrs = inbuf; //auto-assign first address seen to me; typically lowest address for easier enum, but not strictly requried
		WREG |= 0x80;  //mark this packet as processed so downstream controllers won't also take it
		PutChar_WREG();
//		if (!MyAdrs) { PROTOCOL_ERROR(PROTOERR_FIRMBUG); --MyAdrs; }
	}

//		if (!zc_stable) continue; //don't respond to Vixen data until ZC rate is sampled (or GECE node addresses have been assigned); don't check this until after baud rate auto-detect so controller can be reset during a live show without restarting the Vixen sequence
//		IFSTATS2(++rcv_pkts.as16); //count my packets only; assume it will be valid
	front_panel(~FP_OTHER);
	for (;;) //execute multiple function codes within this packet (as many as are present):
	{
		GetCharRenard(inbuf, EMPTY, 103 WHERE); //get function code; check for eopkt
//		if (!MyAdrs) { PROTOCOL_ERROR(PROTOERR_FIRMBUG); --MyAdrs; }
		opcode(inbuf);
		if (NEVER) break; //kludge: avoid dead code removal
	}
//		if (NEVER) break; //avoid dead code removal
//	}
//			if (NEVER) break; //kludge: avoid dead code removal
//		}
//	}
//	TRAMPOLINE(12); //kludge: compensate for page select or address tracking bug
//	TRAMPOLINE(12); //kludge: compensate for page select or address tracking bug
//	TRAMPOLINE(1); //kludge: compensate for page select or address tracking bug
#ifdef PIC16X
	TRAMPOLINE(2); //kludge: compensate for address tracking bug
#endif
}


//;==================================================================================================
//;manifest data and EEPROM initialization
//;==================================================================================================

#ifdef WANT_WS2811
 #define WS2811_INCL  (1<<0xA) | (1<<0xB) //(1<<RENXt_WS2811(SERIES)) | (1<<RENXt_WS2811(PARALLEL))
#else
 #define WS2811_INCL  0
#endif
#ifdef WANT_GECE
 #define GECE_INCL  (1<<5) //(1<<RENXt_GECE)
#else
 #define GECE_INCL  0
#endif
#ifdef WANT_CHPLEX
 #define CHPLEX_INCL  (1<<1) | (1<<2) | (1<<3) | (1<<4) //(1<<RENXt_PWM(0xCC)) | (1<<RENXt_PWM(0xCA)) | (1<<RENXt_CHPLEX(0xCA)) | (1<<RENXt_CHPLEX(0xCC))
#else
 #define CHPLEX_INCL  0
#endif
#define NULL_INCL  (1<<0) //always available
#define IOTYPES  NULL_INCL | WS2811_INCL | GECE_INCL | CHPLEX_INCL


//embed info into ROM:
//sender can read this info and then adapt to controller configuration
//#define MORE_MANIF_ADDR 0xF80 //put up above other code
inline void manifest(void)
{
	init(); //prev init first

	ATADRS(adrsof(RENXt_MANIF));
//	asm data adrsof(MORE_MANIF); //pointer to more info
//	ATADRS(-1);

//	ATADRS(adrsof(MORE_MANIF));
//manifest data:
//sender can use this info to optimize packets for this uController
	asm
	{
//		data "RenXt\0";
//		data A2('R', 'e'), A2('n', 'X'), A2('t', '\0'); //"RenXt"
#define CHAR(ignore, value)  value //kludge: BoostC asm doesn't like char constants, so drop them and use hex const instead
//CAUTION: BoostC doesn't handle comments within asm!
		data A2(CHAR('R', 0x52), CHAR('e', 0x65)), A2(CHAR('n', 0x6E), CHAR('X', 0x58)), A2(CHAR('t', 0x74), CHAR('\0', 0)); //"RenXt"
		data RENXt_VERSION; //version#
//TODO: add date?
		data DEVICE_CODE; //which uController
		data (NODEIO_PINS << 8) | (SERIES_PIN); //#node I/O pins available (lower for parallel smart nodes), which I/O pin used for series nodes
		data IOTYPES; //which node I/O types compiled in
		data DIMMING_CURVE; //#steps (resolution) of dimming curve
		data TOTAL_BANKRAM; //total RAM available for node + palette data; also indicates RAM scale factor
		data MAX_BAUD % 0x1000, MAX_BAUD / 0x1000; //max baud rate (won't fit in 14 bits); little endian
		data CLOCK_FREQ % 0x1000, CLOCK_FREQ / 0x1000; //ext clock freq (if any); won't fit in 14 bits; little endian; also check ExtClockFailed
#warning "fix this"
		data 0x1E00; //IIF(PIC16, 0x1000, 0) + IIF(DEBUG, 0x800, 0) + IIF(DEBUG2, 0x400, 0) + IIF(HIGHBAUD_OK, 0x200, 0) + IIF(AUTO_DETECT_BAUD, 0x100, 0);
//		data IIF(PIC16, 0x1000, 0);// + IIF(DEBUG, 0x800, 0) + IIF(DEBUG2, 0x400, 0) + IIF(HIGHBAUD_OK, 0x200, 0) + IIF(AUTO_DETECT_BAUD, 0x100, 0);
;//#define NODE_TYPE  WS2811
	}
	ATADRS(-1);
}
#undef init
#define init()  manifest() //initialization wedge, for compilers that don't support static initialization


//node config initialization:
//loads node config from EEPROM (overridable) and changes bkg color (nodes are already set to first palette entry)
//called only from main() when starting test/demo code
//non_inline void set_all_WREG(void); //fwd ref
//#define set_all(palinx)  { WREG = palinx; set_all_WREG(); }


//volatile uint8 demo_strlen @adrsof(DEMO_STACKFRAME3) + 0; //UNUSED3)+0;
volatile uint8 demo_pattern @adrsof(DEMO_STACKFRAME3) + 1 ;//UNUSED3)+1;


inline void InitNodeConfig(void)
{
	volatile uint8 count @adrsof(DEMO_STACKFRAME3) + 1 ;//UNUSED3)+1; //use same adrs as demo_pattern to free more memory for caller //LEAF_PROC);
//	ONPAGE(DEMO_PAGE); //put on same page as demo code to reduce page selects

	init(); //prev init first
//set up default values in EEPROM, allow sender to overwrite:
//#warning "EEPROM - put this back in"
#if 1
	ATADRS(EEPROM_ORG);
//sender can overwrite this, but it is reset each time pic is reflashed:
	asm
	{
//#if CLOCK_FREQ >= 32 MHz
// #warning "[INFO] Setting default node config to parallel WS281X, max #nodes"
//		data (RENXt_WS2811(PARALLEL) << 4) | (4 BPP & BPP_MASK); //NodeConfig: default to one (series) string, WS2811
//#else
 #warning "[INFO] Setting default node config to series WS281X, max #nodes"
		data (RENXt_WS2811(SERIES) << 4) | (4 BPP & BPP_MASK); //NodeConfig: default to one (series) string, WS2811
//#endif
//		data (TOTAL_BANKRAM - (3<<4 BPP)) / RAM_SCALE; //NodeBytes: default node data size to max available; determines max #nodes
//		data 50; //string length
		data Nodes2Bytes(NUM_NODES(4 BPP), 4 BPP) / RAM_SCALE; //(TOTAL_BANKRAM - (3<<4 BPP)) / RAM_SCALE; //default node data size to max available; determines max #nodes
		data ADRS_ALL; //initial controller address
//		data (4 BPP); //2-bit bpp; kludge: BoostC doesn't support bit sizes
		data R(BLACK), G(BLACK), B(BLACK); //RGB2PROG(0x123456); //BLACK); //default bkg color
//		data 150; //test/demo string length; LED strip is typically 150 (30/m) or 240 60/m); LED strings are typically 50 (5V) or 128 (12V)
		data 1; //default test pattern
		data 0; //reserved for application data (prop name, etc); RenXt_tool can set this later (via WriteMem opcode)
//NOTE: enter prop names into lower portion of PICKit2 window when programming
	}
	ATADRS(-1);
//#pragma DATA /*_EEPROM*/  0xF000, (RENXt_WS2811(SERIES) << 4) | (1 - 1); //NodeConfig: default to one (series) string, WS2811
//#pragma DATA _EEPROM, TOTAL_BANKRAM / RAM_SCALE - (3<<4 BPP); //NodeBytes: default node data size to max available; determines max #nodes
//#pragma DATA _EEPROM, (4 BPP); //2-bit bpp; kludge: BoostC doesn't support bit sizes

	eeadrL = 0; //start of EEPROM
	EEREAD(TRUE); NodeConfig = eedatL;
	EEREAD_AGAIN(TRUE); NodeBytes = eedatL;
	EEREAD_AGAIN(TRUE); MyAdrs = eedatL;
//	if (!MyAdrs) { PROTOCOL_ERROR(PROTOERR_FIRMBUG); --MyAdrs; }
//	EEREAD_AGAIN(); ++eeadrL; WREG = eedatL; SetBPP_WREG();
#if 0
//GGGG RRRR RRRR, GGGG BBBB BBBB
	EEREAD_AGAIN(TRUE); DimmingCurve(eedatL); palette[0].R8 = WREG;
	swap_WREG(eedatH); //save G upper nibble before moving to next prog word
	EEREAD_AGAIN(TRUE); DimmingCurve(WREG | eedatH); palette[0].G8 = WREG;
	DimmingCurve(eedatL); palette[0].B8 = WREG;
#else
	count = 3;
	fsrL = PALETTE_ADDR(0, 4 BPP); fsrH = PALETTE_ADDR(0, 4 BPP) / 0x100; /*set palette start address*/
	for (;;)
	{
		EEREAD_AGAIN(TRUE); DimmingCurve(eedatL); indf_autopal = WREG;
		if (!--count) break;
	}
#endif
	if ((CLOCK_FREQ >= 32 MHz) && ParallelNodes) //convert series palette entry (3 bytes) to 3 parallel sub-palette entries (4 bytes each) + 1 indirect entry to palette triplet
	{
		is_gece();
 		if (!IsGece) rgb2parallel(0);
	}
//	EEREAD_AGAIN(TRUE); demo_strlen = eedatL;
	EEREAD_AGAIN(TRUE); demo_pattern = eedatL; //demo/test pattern to use
#else
	NodeConfig = (RENXt_WS2811(SERIES) << 4) | (4 BPP & BPP_MASK); //NodeConfig: default to one (series) string, WS2811
	NodeBytes = Nodes2Bytes(NUM_NODES(4 BPP), 4 BPP) / RAM_SCALE; //(TOTAL_BANKRAM - (3<<4 BPP)) / RAM_SCALE; //default node data size to max available; determines max #nodes
	MyAdrs = ADRS_ALL; //initial controller address
	fsrL = PALETTE_ADDR(0, 4 BPP); fsrH = PALETTE_ADDR(0, 4 BPP) / 0x100; /*set palette start address*/
	indf_autopal = R(BLACK); indf_autopal = G(BLACK); indf_autopal = B(BLACK); //RGB2PROG(0x123456); //BLACK); //default bkg color
//	demo_strlen = 150; //test/demo string length; LED strip is typically 150 (30/m) or 240 60/m); LED strings are typically 50 (5V) or 128 (12V)
	demo_pattern = 1; //default test pattern
//	name = 0; //reserved for application data (prop name, etc); RenXt_tool can set this
#endif
	nodepin_init();
	if (LEAST_PAGE != PROTOCOL_PAGE) TRAMPOLINE(1); //kludge: compensate for call across page boundary
	gece_init(); //set initial gece port as early as possible; also init fifo state; need to do this before set-all
//don't do this yet (can't start gece i/o within first 2 sec, and don't need to set all non-gece nodes yet):
//	set_all(0); //incoming serial data ignored during init (echo is off)
}
#undef init
#define init()  InitNodeConfig() //initialization wedge for compilers that don't support static init


//node config initialization:
#if 0
inline void nodecfg_init(void)
{
//	uint8 demo_pattern @adrsof(LEAF_PROC);
	init(); //prev init first

//	StateBits = 0; //initialize all state bits with 1 clrf instr; NOTE: this also sets bpp to 4
//	iochars.bytes[0] = (iochars.bytes[1] = (iochars.bytes[2] = 0)); //BoostC generates extra instr with this
//hard-coded defaults (overridable later):
#if 0
	NodeConfig = (RENXt_NULLIO << 4) | (4 BPP & BPP_MASK); //default to one (series) string, WS2811; //RENXt_NULLIO;
//	if (Ports[PORTOF(ZC_PIN)] & (1 << PINOF(ZC_PIN)) WREG ^= (-RENXt_GECE ^ /*undo prev:*/ RENXt_WS2811) << 4;
//	NodeConfig = WREG;
	NodeBytes = Nodes2Bytes(NUM_NODES(4 BPP), 4 BPP) / RAM_SCALE; //(TOTAL_BANKRAM - (3<<4 BPP)) / RAM_SCALE; //default node data size to max available; determines max #nodes
//	SetBPP(4 BPP); //default to 4 bpp; determines palette size and node packing
	SetPalette(0, BLACK);
//#if (TOTAL_BANKRAM - (3<<4 BPP)) / RAM_SCALE > 255
#if Nodes2Bytes(NUM_NODES(4 BPP), 4 BPP) / RAM_SCALE > 255
 #error "[ERROR] Total banked RAM "TOTAL_BANKRAM" scaled by "RAM_SCALE" too big: "(TOTAL_BANKRAM - (3<<4 BPP)) / RAM_SCALE""
#endif
//	NodeBytesH = MIN(Nodes2Bytes(150, 4 BPP), TOTAL_BANKRAM - 16 * 3) / 0x100; //top bit of node data size
//	SetPalette(0, BLACK);
//	SetNodeCount(count, bpp);
//	protocol_inactive = FALSE; //kludge: don't allow set_all to be interrupted by serial data; protocol() will treat this as recursion and pass thru, but echo is off so serial data will be discarded
	set_all(0); //incoming serial data ignored during init (echo is off)
#endif
//	gece_palette_not_full = 0xFF; //kludge (for gece): mark palette entry 10 so GetBitmap knows whether to skip or wrap; max brightness = 0xCC
//	protocol_inactive = FALSE;
	InitNodeConfig();
}
#undef init
#define init()  nodecfg_init() //initialization wedge for compilers that don't support static init
#endif


//;==================================================================================================
//;Demo/test/autonomous animation patterns
//hard-coded, driven from rom tables.
//;==================================================================================================

//#define Wait4Timer(which, duration, idler, uponserial)  \
//	Wait4TimerThread(which, duration, EMPTY, { idler; SerialCheck(IGNORE_FRAMERRS, { protocol(); uponserial; }}))

#ifdef WANT_GECE
 volatile bit gece_demo @BITADDR(adrsof(statebit_0x20)); //CAUTION: this will be overwritten by various opcodes; check for no other conflicts within demo code (set_all_gece_alternating uses 0x10)
#else
 #define gece_demo  FALSE //compiler should optimize out unused code
#endif
//volatile bit needio @BITADDR(adrsof(statebit_0x10)); //CAUTION: this will be overwritten by various opcodes


#if 0 //DEBUG ONLY
 #define debug(pos, val)  { WREG = val; if (pos == 1) debug1_WREG(); else debug2_WREG(); } //debug##pos_WREG(); } //BoostC broken again
non_inline void debug1_WREG(void)
{
	uint8 debug_value @adrsof(LEAF_PROC);
	ONPAGE(DEMO_PAGE);

	debug_value = WREG;
//	fsrH = NODE_ADDR(0, 4 BPP) / 0x100; fsrL = NODE_ADDR(0, 4 BPP); indf &= 0xF0; indf |= 2;
//	--fsrL; indf &= 0xF0; indf |= 2;
//	--fsrL; indf &= 0xF0; indf |= 2;
//	uint8 bits @adrsof(LEAF_PROC);
//	bitcopy(iochars.bytes[0], bits.0);
//	bitcopy(iochars.bytes[1], bits.1);
//	bitcopy(iochars.bytes[2], bits.2);
//	bitcopy(IsCharAvailable, bits.3);
//	bitcopy(!HasFramingError, bits.4);
	IFRP1(rp1 = TRUE); //status |= 0x40); //RP1 kludge for 16F688 over-optimization
	if (debug_value & 1) SetNode(8, 2, 4 BPP); else SetNode(8, 4, 4 BPP); //2 == red, 1 == green, 4 == BLUE
	if (debug_value & 2) SetNode(9, 2, 4 BPP); else SetNode(9, 4, 4 BPP);
	if (debug_value & 4) SetNode(10, 2, 4 BPP); else SetNode(10, 4, 4 BPP);
	if (debug_value & 8) SetNode(11, 2, 4 BPP); else SetNode(11, 4, 4 BPP);
	if (debug_value & 16) SetNode(12, 2, 4 BPP); else SetNode(12, 4, 4 BPP);
	if (debug_value & 32) SetNode(13, 2, 4 BPP); else SetNode(13, 4, 4 BPP);
	if (debug_value & 64) SetNode(14, 2, 4 BPP); else SetNode(14, 4, 4 BPP);
	if (debug_value & 128) SetNode(15, 2, 4 BPP); else SetNode(15, 4, 4 BPP);
	SetNode(5, 2, 4 BPP);
	IFRP1(rp1 = FALSE); //status &= ~0x40); //RP1 kludge for 16F688 over-optimization
//	send_nodes();
}
non_inline void debug2_WREG(void)
{
	uint8 debug_value @adrsof(LEAF_PROC);
	ONPAGE(DEMO_PAGE);

	debug_value = WREG;
//	fsrH = NODE_ADDR(0, 4 BPP) / 0x100; fsrL = NODE_ADDR(0, 4 BPP); indf &= 0xF0; indf |= 2;
//	--fsrL; indf &= 0xF0; indf |= 2;
//	--fsrL; indf &= 0xF0; indf |= 2;
//	uint8 bits @adrsof(LEAF_PROC);
//	bitcopy(iochars.bytes[0], bits.0);
//	bitcopy(iochars.bytes[1], bits.1);
//	bitcopy(iochars.bytes[2], bits.2);
//	bitcopy(IsCharAvailable, bits.3);
//	bitcopy(!HasFramingError, bits.4);
	IFRP1(rp1 = TRUE); //status |= 0x40); //RP1 kludge for 16F688 over-optimization
	if (debug_value & 1) SetNode(8+8, 2, 4 BPP); else SetNode(8+8, 4, 4 BPP); //2 == red, 1 == green, 4 == BLUE
	if (debug_value & 2) SetNode(9+8, 2, 4 BPP); else SetNode(9+8, 4, 4 BPP);
	if (debug_value & 4) SetNode(10+8, 2, 4 BPP); else SetNode(10+8, 4, 4 BPP);
	if (debug_value & 8) SetNode(11+8, 2, 4 BPP); else SetNode(11+8, 4, 4 BPP);
	if (debug_value & 16) SetNode(12+8, 2, 4 BPP); else SetNode(12+8, 4, 4 BPP);
	if (debug_value & 32) SetNode(13+8, 2, 4 BPP); else SetNode(13+8, 4, 4 BPP);
	if (debug_value & 64) SetNode(14+8, 2, 4 BPP); else SetNode(14+8, 4, 4 BPP);
	if (debug_value & 128) SetNode(15+8, 2, 4 BPP); else SetNode(15+8, 4, 4 BPP);
	SetNode(5, 2, 4 BPP);
	IFRP1(rp1 = FALSE); //status &= ~0x40); //RP1 kludge for 16F688 over-optimization
//	send_nodes();
}
#else
 #define debug(pos, val)
#endif


//exit demo mode if serial data arrived during node I/O:
//any waits that are done in demo/test pattern code are for animation
//they will be interrupted immediately if protocol data arrives
non_inline void StartProtocolMaybe(void)
{
//	ONPAGE(PROTOCOL_PAGE); //CAUTION: must be on same page as protocol() to avoid page select problem with function chaining //DEMO_PAGE); //put on same page as demo code to reduce page selects
	ONPAGE(DEMO_PAGE);

//	if (HasFramingError) break; //goto comm_reset; //NOTE: must check for frame error before reading char
//	SerialErrorReset();
	BusyPassthru(/*RECEIVE_FRAMERRS*/ FIXUP_FRAMERRS, 5); //open serial line can cause frame errors, so don't allow those to interrupt demo sequence
//	BusyPassthru(RECEIVE_FRAMERRS, 5); //open serial line can cause frame errors, so don't allow those to interrupt demo sequence
	if (DEMO_PAGE != IOH_PAGE) TRAMPOLINE(2); //kludge: compensate for page selects
	if (!SentNodes) send_nodes(); //start node I/O after timer is started; okay if node I/O overruns timer, because it will adjust during next cycle (for delays longer than 50 msec)
	if (iobusy) resume_nodes(); //resume I/O while waiting for timer or protocol data
//	SerialCheck(FIXUP_FRAMERRS, nextpkt(WaitForSync)); //fixup + ignore frame errors, wait for Sync if data received
//#if 0 //broken
//look back and see if a char was received when things were too busy to look at it in detail:
	WREG = iochars.bytes[0]; WREG |= iochars.bytes[1]; WREG |= iochars.bytes[2]; //status.Z => no serial I/O
	if (!EqualsZ) { iobusy = FALSE; front_panel(FP_NODEIO); WREG = txreg; nextpkt(EndOfPacket); } //try to preserve current char, and start handling protocol
//CAUTION: might need trampoline here
//	if (!EqualsZ) { protocol(); return; } //if data came in during previous send_nodes, interrupt test/demo and handle the protocol
//no serial data received; continue with demo
//	nop(); //kludge: this is required in order to preserve return instr
//	if (EqualsZ && (!IsCharAvailable || HasFramingError)) return;
#if 0 //broken
	debug(1, iochars.bytes[0]);
	debug(2, txreg);
	send_nodes();
#endif
//NOTE: first Sync char was eaten by prior pass-thru code
//try to "recover" the Sync by checking last char sent
//if that doesn't work, remainder of packet will be discarded and sender must send >= 1 Sync per controller at the very start; this is the price for having a free-running demo/test pattern at startup
//	protocol(2);
#if 0
	protocol_inactive = FALSE; //kludge: make it look like protocol() already received the first char
	WantEcho = FALSE; //don't echo anything until Sync received
	if (txreg == RENARD_SYNC) { WantEcho = TRUE; nextpkt(EndOfPacket); }
	nextpkt(WaitForSync); //if it wasn't Sync, just ignore until next Sync arrives
#endif
//TODO: return from protocol() should rerun main(), not just return
//	TRAMPOLINE(2); //kludge: compensate for address tracking bug
}


//NOTE: 384 WS2811 nodes uses ~ 11.5 msec for send_nodes, 1920 nodes uses 57.6 msec, so delay time should be reduced accordingly to maintain caller's desired times
//#define WS2811_REFR_TIME  (30 usec * NUM_NODES(4 BPP)) //WS2811
////kludge: avoid macro body length problems with BoostC:
//#if WS2811_REFR_TIME == 11520 //384 nodes
// #undef WS2811_REFR_TIME
// #define WS2811_REFR_TIME  11520 //11.52 msec
//#else
//#if WS2811_REFR_TIME == 57600 //1920 nodes
// #undef WS2811_REFR_TIME
// #define WS2811_REFR_TIME  57600 //57.6 msec
////#else
//#endif
//#endif


//wait until complete before sending more to avoid overruns:
//also, restore gece flag
non_inline void wait_for_iocomplete(void)
{
	ONPAGE(DEMO_PAGE); //put on same page as demo code to reduce page selects

//	if (DEMO_PAGE != IOH_PAGE) TRAMPOLINE(1); //kludge: compensate for call across page boundary
//	send_nodes(); //iobusy = FALSE; //force node refresh (delay is for animation purposes, so node data is assumed to be dirty)
//	FinishWaiting4Timer(Timer1, StartProtocol());
	WREG = NodeConfig >> 4; //split up expr to avoid BoostC generating a temp
	if (IsDumb(WREG)) return; //I/O will never complete so pretend it did and just return to caller
	while (iobusy) StartProtocolMaybe(); //gece I/O finishes asynchronously; wait until complete before sending more to avoid overruns
	is_gece();
	bitcopy(IsGece, gece_demo); //restore flag (overwritten during send_nodes, due to memory shortage)
}

//#undef delay_loop_adjust
//#define delay_loop_adjust(ignored)  send_nodes() //kludge: start node I/O after timer starts so overall time will be correct

//pre-expand commonly used wait times:
non_inline void wait_10msec(void)
{
	ONPAGE(DEMO_PAGE); //put on same page as demo code to reduce page selects
	if (DEMO_PAGE != IOH_PAGE) TRAMPOLINE(1); //kludge: compensate for call across page boundary

//	StartTimer(Timer1, 10 msec, ABSOLUTE_TIMER); //start timer before node I/O so overall time will be correct
	SentNodes = FALSE; //kludge: reuse this flag to control start of node I/O
	wait(10 msec, StartProtocolMaybe()); //start node I/O and check protocol while waiting for timer
//	wait(MAX(10 msec - REFR_TIME, 1 msec), DemoExit()); //BoostC macro body length problem again
//	if (10 msec > REFR_TIME) wait(10 msec - REFR_TIME, StartProtocol());
//	else wait(1 msec, StartProtocol()); //wait a small amount so timer loop gets a few laps
	wait_for_iocomplete(); //don't overrun animation when I/O takes too long
}
non_inline void wait_halfsec(void)
{
	ONPAGE(DEMO_PAGE); //put on same page as demo code to reduce page selects
	if (DEMO_PAGE != IOH_PAGE) TRAMPOLINE(1); //kludge: compensate for call across page boundary
//	TRAMPOLINE(2); //kludge: compensate for call across page boundary

//	tmr1_16 = TimerPreset(1 sec/2, 0, Timer1); //DEBUG ONLY
	SentNodes = FALSE; //kludge: reuse this flag to control start of node I/O
	wait(1 sec/2, StartProtocolMaybe()); //start node I/O and check protocol while waiting for timer
//	wait(MAX(10 msec - REFR_TIME, 1 msec), DemoExit()); //BoostC macro body length problem again
//	if (1 sec/2 > REFR_TIME) wait(1 sec/2 - REFR_TIME, StartProtocol());
//	else wait(1 msec, StartProtocol()); //wait a small amount so timer loop gets a few laps
	wait_for_iocomplete(); //don't overrun animation when I/O takes too long
//	TRAMPOLINE(1); //kludge: compensate for page select or address tracking bug
}
non_inline void wait_10thsec(void)
{
	ONPAGE(DEMO_PAGE); //put on same page as demo code to reduce page selects
	if (DEMO_PAGE != IOH_PAGE) TRAMPOLINE(1); //kludge: compensate for call across page boundary
//	TRAMPOLINE(2); //kludge: compensate for call across page boundary

	SentNodes = FALSE; //kludge: reuse this flag to control start of node I/O
	wait(1 sec/10, StartProtocolMaybe()); //start node I/O and check protocol while waiting for timer
//	wait(MAX(1 sec/10 - REFR_TIME, 1 msec), DemoExit()); //BoostC macro body length problem again
//	if (1 sec/10 > REFR_TIME) wait(1 sec/10 - REFR_TIME, StartProtocol());
//	else wait(1 msec, StartProtocol()); //wait a small amount so timer loop gets a few laps
	wait_for_iocomplete(); //don't overrun animation when I/O takes too long
}
//used by demo/test for animation
//timing doesn't need to be too precise
non_inline void wait_20thsec(void)
{
	ONPAGE(DEMO_PAGE); //put on same page as demo code to reduce page selects
	if (DEMO_PAGE != IOH_PAGE) TRAMPOLINE(1); //kludge: compensate for call across page boundary
//	TRAMPOLINE(2); //kludge: compensate for call across page boundary

//	tmr1_16 = divup((1 sec/20)/2, 16384); /*kludge: BoostC broken again*/
//	tmr1_16 = divup((1 sec/2)/2, 16384); /*kludge: BoostC broken again*/
//	tmr0 = TimerPreset(1 sec/20, 0, Timer0); //DEBUG ONLY
//	tmr1_16 = TimerPreset(1 sec/20, 0, Timer1); //DEBUG ONLY
// portc = 1;
	SentNodes = FALSE; //kludge: reuse this flag to control start of node I/O
	wait(1 sec/20, StartProtocolMaybe()); //start node I/O and check protocol while waiting for timer
// portc = 0;
//#if 1 sec/20 >= REFR_TIME + 1 msec
// #warning "1/20 >= "REFR_TIME""
//	wait(1 sec/20 - REFR_TIME, DemoExit()); //TODO: subtract NodeBytes?
//#else
// #warning "1/20 < "REFR_TIME""
//	wait(1 msec, DemoExit()); //wait a small amount so timer loop gets a few laps
//#endif
//	wait(MAX(1 sec/20 - REFR_TIME, 1 msec), DemoExit()); //BoostC macro body length problem again
//	volatile uint8 delay_count @adrsof(PROTOCOL_STACKFRAME3) + 0; //CAUTION: overwritten during send, but okay to use here
//stretch out delay period for shorter strings (animation sequence might be too short otherwise):
//	if (NodeBytes < (0x20 >> RAM_SCALE_BITS)) delay_count = 8;
//	else if (NodeBytes < (0x40 >> RAM_SCALE_BITS)) delay_count = 4;
//	else if (NodeBytes < (0x80 >> RAM_SCALE_BITS)) delay_count = 2;
//	else delay_count = 1;
//	for (;;)
//	{
//		if (1 sec/20 > REFR_TIME) wait(1 sec/20 - REFR_TIME, StartProtocol());
//		else wait(1 msec, StartProtocol()); //wait a small amount so timer loop gets a few laps
//		if (!--delay_count) break;
//	}
	wait_for_iocomplete(); //don't overrun animation when I/O takes too long
//	TRAMPOLINE(2); //kludge: compensate for call across page boundary
}
//stretch out delay period for shorter strings (animation sequence might be too short otherwise):
non_inline void wait_20thsec_ormore(void)
{
	ONPAGE(DEMO_PAGE); //put on same page as demo code to reduce page selects

	if (NodeBytes < (0x20 >> RAM_SCALE_BITS)) REPEAT(4, wait_20thsec()); //8x
	if (NodeBytes < (0x40 >> RAM_SCALE_BITS)) REPEAT(2, wait_20thsec()); //4x
	if (NodeBytes < (0x80 >> RAM_SCALE_BITS)) wait_20thsec(); //2x
	wait_20thsec();
}
#define wait_1sec()  REPEAT(2, wait_halfsec()) //just reuse 1/2 sec rather than defining a new function
#if 0
non_inline void wait_1sec(void)
{
	ONPAGE(DEMO_PAGE); //put on same page as demo code to reduce page selects
	if (DEMO_PAGE != IOH_PAGE) TRAMPOLINE(1); //kludge: compensate for call across page boundary
//	TRAMPOLINE(2); //kludge: compensate for call across page boundary

	SentNodes = FALSE; //kludge: reuse this flag to control start of node I/O
	wait(1 sec, StartProtocolMaybe()); //start node I/O and check protocol while waiting for timer
//	wait(MAX(1 sec - REFR_TIME, 1 msec), DemoExit()); //BoostC macro body length problem again
//	if (1 sec > REFR_TIME) wait(1 sec - REFR_TIME, StartProtocol());
//	else wait(1 msec, StartProtocol()); //wait a small amount so timer loop gets a few laps
	wait_for_iocomplete(); //don't overrun animation when I/O takes too long
}
#endif
//non_inline void wait_2sec(void)
//{
//	ONPAGE(DEMO_PAGE); //put on same page as demo code to reduce page selects
//	TRAMPOLINE(1); //kludge: compensate for call across page boundary
//	iobusy = TRUE; //force node refresh (delay is for animation purposes, so node data is assumed to be dirty)
//	wait(2 sec, EMPTY);
//}
//#undef delay_loop_adjust
//#define delay_loop_adjust(ignored)  EMPTY //overridable by caller; use this instead of another macro param


//#define set_all_demo(color, count, bpp)  \
//{ \
//	SetNodeCount(count, bpp); \
//	SetPalette(0, color); \
//	set_all(0); \
//}


#ifdef WANT_TEST
//palette entry indexes:
#define BLACK_PALINX  0

//NOTE: palette entry#s are arranged as 3 bits for easier R, G, B control
//NOTE: on WS2811 strip, red + green are swapped on older strips (strings are okay)
#define RED_PALINX  1
#define GREEN_PALINX  2
#define BLUE_PALINX  4
#define YELLOW_PALINX  3 //RED + GREEN
#define CYAN_PALINX  6 //GREEN + BLUE
#define MAGENTA_PALINX  5 //RED + BLUE
#define WHITE_PALINX  7 //RED + GREEN + BLUE
#define UNUSED_PALINX  8 //first spare


//reorder colors for enumeration:
//this allows higher contrast at each step (ie, cyan -> white is not as noticeable)
//also puts palette entry# in upper and lower nibble for easier setting of even/odd nodes
#define NextColor(curcolor)  { WREG = curcolor; NextColor_WREG(); }
non_inline void NextColor_WREG(/*uint8& WREG*/ void)
{
	ONPAGE(DEMO_PAGE); //put on same page as demo code to reduce page selects

//	color += 0x11; //put color palette entry# in upper and lower nibble
//	if (Carry) ++color;
	WREG &= 0xF;
	INGOTOC(TRUE); //#warning "CAUTION: pclath<2:0> must be correct here"
	pcl += WREG;
	PROGPAD(0); //jump table base address
	JMPTBL16(BLACK_PALINX) RETLW(RED_PALINX * 0x11 + 0x88); //black -> red
	JMPTBL16(RED_PALINX) RETLW(GREEN_PALINX * 0x11); //red -> green
	JMPTBL16(GREEN_PALINX) RETLW(BLUE_PALINX * 0x11); //green -> blue
	JMPTBL16(YELLOW_PALINX) RETLW(CYAN_PALINX * 0x11); //yellow -> cyan
	JMPTBL16(BLUE_PALINX) RETLW(YELLOW_PALINX * 0x11); //blue -> yellow
	JMPTBL16(MAGENTA_PALINX) RETLW(WHITE_PALINX * 0x11); //magenta -> white
	JMPTBL16(CYAN_PALINX) RETLW(MAGENTA_PALINX * 0x11); //cyan -> magenta
	JMPTBL16(WHITE_PALINX) RETLW(BLACK_PALINX * 0x11); //white -> black
//second pass (skip every other node, for double-spacing test):
	JMPTBL16(BLACK_PALINX + 8) RETLW(RED_PALINX * 0x11); //black -> red
	JMPTBL16(RED_PALINX + 8) RETLW(GREEN_PALINX * 0x11 + 0x88); //red -> green
	JMPTBL16(GREEN_PALINX + 8) RETLW(BLUE_PALINX * 0x11 + 0x88); //green -> blue
	JMPTBL16(YELLOW_PALINX + 8) RETLW(CYAN_PALINX * 0x11 + 0x88); //yellow -> cyan
	JMPTBL16(BLUE_PALINX + 8) RETLW(YELLOW_PALINX * 0x11 + 0x88); //blue -> yellow
	JMPTBL16(MAGENTA_PALINX + 8) RETLW(WHITE_PALINX * 0x11 + 0x88); //magenta -> white
	JMPTBL16(CYAN_PALINX + 8) RETLW(MAGENTA_PALINX * 0x11 + 0x88); //cyan -> magenta
	JMPTBL16(WHITE_PALINX + 8) RETLW(BLACK_PALINX * 0x11 + 0x88); //white -> black
	INGOTOC(FALSE); //check jump table doesn't span pages
//	TRAMPOLINE(2); //kludge: compensate for page select or address tracking bug
#ifdef PIC16X
	TRAMPOLINE(1); //kludge: compensate for address tracking bug
#endif
}


//set primary colors:
//non-inline for eaier debug
non_inline void demo_palette(void)
{
	ONPAGE(DEMO_PAGE); //put on same page as demo code to reduce page selects

//#if TOTAL_ROM > 4K //lots of code space
//#ifdef WANT_GECE
	if (gece_demo) //set gece colors
	{
#define ApplyDimmingCurve_WREG()  //kludge: bypass dimming curve
		SetPalette(RED_PALINX, RGB2IBGR(RED)); //[1] = green if R/G swapped
		SetPalette(GREEN_PALINX, RGB2IBGR(GREEN)); //[2] = red if R/G swapped
		SetPalette(BLUE_PALINX, RGB2IBGR(BLUE)); //[4] = blue
		SetPalette(YELLOW_PALINX, RGB2IBGR(YELLOW)); //[3] = yellow
		SetPalette(CYAN_PALINX, RGB2IBGR(CYAN)); //[5] = magenta if R/G swapped
		SetPalette(MAGENTA_PALINX, RGB2IBGR(MAGENTA)); //[6]= cyan if R/G swapped
		SetPalette(WHITE_PALINX, RGB2IBGR(WHITE)); //[7] = white
#undef ApplyDimmingCurve_WREG
		return;
	}
//#endif //WANT_GECE

	if ((CLOCK_FREQ >= 32 MHz) && ParallelNodes) // && !gece_demo) //set up parallel sub-palette entries (4 bytes each) + 1 indirect entry to triplet for multiple variations of parallel palette entries; actual colors are generated dynamically as needed
	{
//demo/test pattern sets nodes one at a time and only uses primary colors, so we only need 4 non-0 parallel palette entries:
//parallel palette should be:
//  '0: 0, 0, 0, 0
//  '4: 0x88, 0x88, 0x88, 0x88
//  '8: 0xCC, 0xCC, 0xCC, 0xCC
//  '12: 0xEE, 0xEE, 0xEE, 0xEE
//  '16: 0xFF, 0xFF, 0xFF, 0xFF
//		palette_bytes[0] = 1 << 2; palette_bytes[1] = (2<<5) | 3; //set up palette entry indirection to sub-palette entries (1, 2, 3)
//the following parallel palette entries are set up to handle transitions of 1 - 4 strings from one primary color to another
//first entry (4 bytes) is set up for use as BLACK/background triplet
//next 7 entries are used to construct primary colors for 1, 2, 3, or 4 parallel nodes being turned on or off one string at a time
//all on/off combinations of primary colors would take 2^4 = 16 palette entries, but since they are being turned on/off in order only 8 palette entries are needed
//first 4 entries: #strings left on old color
		SetParallelPalette(0, 0, 0, 0, 0); //all nodes off; can also be used as indirect ptr to self
		SetParallelPalette(1, 0, 0, 0, 0xFF); //first, second, third nodes off, last on
		SetParallelPalette(2, 0, 0, 0xFF, 0xFF); //first + second nodes off, others on
		SetParallelPalette(3, 0, 0xFF, 0xFF, 0xFF); //first off, others on
//next 4 entries: 8-#strings set to new color
		SetParallelPalette(4, 0xFF, 0xFF, 0xFF, 0xFF); //all nodes full-on
		SetParallelPalette(5, 0xFF, 0xFF, 0xFF, 0); //first, second, third nodes on, last off
		SetParallelPalette(6, 0xFF, 0xFF, 0, 0); //first + second nodes on, others off
		SetParallelPalette(7, 0xFF, 0, 0, 0); //first node on, others off
//alternate entries for white (max brightness 0xCC):
		SetParallelPalette(8, 0, 0, 0, 0); //all nodes off; can also be used as indirect ptr to self
		SetParallelPalette(9, 0, 0, 0, 0xCC); //first, second, third nodes off, last on
		SetParallelPalette(10, 0, 0, 0xCC, 0xCC); //first + second nodes off, others on
		SetParallelPalette(11, 0, 0xCC, 0xCC, 0xCC); //first off, others on
//next 4 entries: 8-#strings set to new color
		SetParallelPalette(12, 0xCC, 0xCC, 0xCC, 0xCC); //all nodes reduced-on
		SetParallelPalette(13, 0xCC, 0xCC, 0xCC, 0); //first, second, third nodes on, last off
		SetParallelPalette(14, 0xCC, 0xCC, 0, 0); //first + second nodes on, others off
		SetParallelPalette(15, 0xCC, 0, 0, 0); //first node on, others off
		return;
	}

	SetPalette(RED_PALINX, RED); //[1] = green if R/G swapped
	SetPalette(GREEN_PALINX, GREEN); //[2] = red if R/G swapped
	SetPalette(BLUE_PALINX, BLUE); //[4] = blue
	SetPalette(YELLOW_PALINX, YELLOW); //[3] = yellow
	SetPalette(CYAN_PALINX, CYAN); //[5] = magenta if R/G swapped
	SetPalette(MAGENTA_PALINX, MAGENTA); //[6]= cyan if R/G swapped
//use reduced brightness on white:
//bypass dimming curve in order to get the correct max brightness
//	SetPalette(WHITE_PALINX, WHITE); //[7] = white
	palette[WHITE_PALINX].R8 = R(WHITE);
	palette[WHITE_PALINX].G8 = G(WHITE);
	palette[WHITE_PALINX].B8 = B(WHITE);
//	SetNode(0, RED_PALINX, 4 BPP);
//#else //code space is tight, use more concise hard-coded values
//	uint8 count @adrsof(LEAF_PROC);
//
//	count = 3<<4;
//	fsrL = PALETTE_ADDR(0, 4 BPP); fsrH = PALETTE_ADDR(0, 4 BPP) / 0x100; //point to first palette entry
//	WREG = 0; if (gece_demo) WREG = 0xCC; //max GECE brightness
//	while (--count) indf_autopal = WREG;
//	if (gece_demo) //IBGR values for gece
//	{
//		WREG = 0;
//		gece_palette[BLACK_PALINX].I8 = WREG; gece_palette[RED_PALINX].B4G4 = WREG; gece_palette[GREEN_PALINX].R4Z4 = WREG; gece_palette[BLUE_PALINX].R4Z4 = WREG; gece_palette[CYAN_PALINX].R4Z4 = WREG;
//		WREG = 0xF0;
//		gece_palette[RED_PALINX].R4Z4 = WREG; gece_palette[BLUE_PALINX].B4G4 = WREG; gece_palette[YELLOW_PALINX].R4Z4 = WREG; gece_palette[MAGENTA_PALINX].B4G4 = WREG; gece_palette[MAGENTA_PALINX].R4Z4 = WREG; gece_palette[WHITE_PALINX].R4Z4 = WREG;
//		WREG = 0x0F;
//		gece_palette[GREEN_PALINX].B4G4 = WREG; gece_palette[YELLOW_PALINX].B4G4 = WREG;
//		WREG = 0xFF;
//		gece_palette[CYAN_PALINX].B4G4 = WREG; gece_palette[WHITE_PALINX].B4G4 = WREG;
//	}
//	else //RGB values
//	{
//		WREG = 0xFF;
//		palette[RED_PALINX].R8 = WREG;
//		palette[GREEN_PALINX].G8 = WREG;
//		palette[BLUE_PALINX].B8 = WREG;
//		palette[YELLOW_PALINX].R8 = WREG; palette[YELLOW_PALINX].G8 = WREG;
//		palette[CYAN_PALINX].G8 = WREG; palette[CYAN_PALINX].B8 = WREG;
//		palette[MAGENTA_PALINX].R8 = WREG; palette[MAGENTA_PALINX].B8 = WREG;
//		palette[WHITE_PALINX].R8 = WREG; palette[WHITE_PALINX].G8 = WREG; palette[WHITE_PALINX].B8 = WREG;
//	}
//#endif //TOTAL_ROM > 4K
}


//convert #bytes to #nodes:
//series strings: 2 nodes/byte
//parallel strings: 4 nodes/2 bytes
inline void descale_nodebytes(uint2x8& value)
{
//	value.bytes[0] = NodeBytes; value.bytes[1] = 0;
//	value.as16 <<= 1; //bytes => #nodes
	rl_WREG(NodeBytes); value.bytes[0] = WREG; value.bytes[1] = 0; rl_nc(value.bytes[1]); //NOTE: Carry = 0 here
	if (RAM_SCALE > 1) rl_nc(value); //value.as16 <<= 1; //byte-pairs => bytes
	if (RAM_SCALE > 2) rl_nc(value); //value.as16 <<= 1; //quad-bytes => byte-pairs
}

//demo/test pattern:
//turn on one node at a time, then fade/ramp all, cycle thru each primary color
//NOTE: not intended for use with dumb channels
//NOTE: nothing else needs to run except periodically checking serial port for protocol data, so timing/performance here is not critical
#warning "[INFO] Compiled with test/demo pattern (one-by-one then ramp/fade)"
non_inline void dumb_test(void); //fwd ref
non_inline void node_test(void)
{
	ONPAGE(DEMO_PAGE); //keep demo code separate from protocol and I/O so they will fit within first code page with no page selects
//	TRAMPOLINE(1); //kludge: compensate for call across page boundary (add_banked)

	WREG = NodeConfig >> 4; //split up expr to avoid BoostC generating a temp
	if (IsDumb(WREG)) { dumb_test(); return; }
	is_gece();
	bitcopy(IsGece, gece_demo);
//	NodeConfig = (RENXt_WS2811(SERIES) << 4) | (1 - 1); //TODO: make this configurable?
//	set_all_demo(BLACK, /*2 * 150*/ NUM_NODES(4 BPP), 4 BPP); //start with all nodes dark; set default node data size to 2 * 150 (5m * 30/m) for demo
//	SetPalette(0, BLACK);
//	SetNodeCount(count, bpp);
//	set_all(0);
	demo_palette(); //add demo palette entries; all nodes were already initialized to bkg color during init
//	SetPalette(RED_PALINX, WHITE);
//	BusyPassthru(5); /*need to do this >= 2x for sustained 250k baud*/
	set_all(0); //NOTE: this will start GECE I/O; must send to all gece nodes to initialize addresses;;;; incoming serial data ignored during init (echo is off)

//	send_nodes(); //set all nodes to initial color
//	while (iobusy) StartProtocol(); //gece I/O finishes asynchronously; wait until completed in order to avoid overwriting previous data
//	iobusy = FALSE; //gece_init set flag but didn't start I/O yet; restart in clean state
//	if (gece_demo) wait(6 usec, ASYNC_TIMER); //GECE I/O init already took place, but I/O hasn't send yet so just restart timer (avoids reinit); reset set dummy timer to expire soon, but give enough time for initialization
	wait_20thsec(); //set all nodes to initial color and wait for completion

	volatile uint8 color @adrsof(DEMO_STACKFRAME3 /*EEDATL*/) = 0; //GET_FRAME_STACKFRAME3) = 0; //kludge: reuse unused SFR for demo data
	color = 8; //start at end of alternating test so first pass will be every node
#if 0 //timing test
	for (color = 0; color < 64; ++color)
	{
		trisa = 0;
		porta = color;
		wait_halfsec();
	}
#endif
//	NodeBytes = divup(4/2, RAM_SCALE); //easier to debug
	for (;;) //turn on nodes 1-by-1, rotate thru primary colors; every other node during alternate cycles
	{
//		debug(2); //green
		color <<= 4; //save prev color in upper nibble for parallel node delta analysis
		swap_WREG(color); NextColor_WREG(); //cycle thru colors
		if (ParallelNodes && !gece_demo) { WREG &= 0x0F; WREG |= color; } //save new color in lower nibble
		color = WREG; //NOTE: top bit selects alternating spacing
#if 0 //set-all test; use 20th sec + nop for easier debug
		if (ParallelNodes) //build an indirect triplet dynamically for current color (to reduce total palette size)
		{
//			ParallelBkg_WREG(); //set first palette triplet to 4 parallel copies of designated primary color
//			if (color & RED_PALINX) SetParallelPalette(0, 0xFF, 0xFF, 0xFF, 0xFF); //all reds on
//			else SetParallelPalette(0, 0, 0, 0, 0); //all reds off
//			if (color & GREEN_PALINX) SetParallelPalette(1, 0xFF, 0xFF, 0xFF, 0xFF); //all greens on
//			else SetParallelPalette(1, 0, 0, 0, 0); //all greens off
//			if (color & BLUE_PALINX) SetParallelPalette(2, 0xFF, 0xFF, 0xFF, 0xFF); //all blues on
//			else SetParallelPalette(2, 0, 0, 0, 0); //all blues off
//combine generic all-on/off parallel entries to create desired primary color:
			indir_palette[15].as16 = 0; //skip over preset palette entries
			if (color & RED_PALINX) indir_palette[15].as16 |= 4<<10; //turn on red for 4 parallel nodes
			if (color & GREEN_PALINX) indir_palette[15].as16 |= 4<<5; //green for 4 parallel nodes
			if (color & BLUE_PALINX) indir_palette[15].as16 |= 4; //blue parallel
			WREG = 15; //skip over preset palette entries
		}
		else WREG &= 7;
		set_all(WREG);
		wait_20thsec(); //19.2 sec total for 384 nodes; 96 sec for 1920 nodes
		nop1(); //easier debug in IDE
		wait_halfsec();
		wait_halfsec();
		wait_halfsec();
		wait_halfsec();
		if (ALWAYS) continue;
#endif
#if 0 //color blink test
		NodeBytes = 12/2/RAM_SCALE;
		set_all(color);
//		send_nodes();
 		wait_halfsec();
		set_all(0);
//		send_nodes();
		wait_halfsec();
//		if (color < 0x40) continue;
#endif

#if 1 //turn on max# nodes one-by-one; every-other on second pass
		volatile uint2x8 node @adrsof(DEMO_STACKFRAME3)+1; //EEADRL); //GET_FRAME_STACKFRAME3) + 1; //kludge: reuse unused SFR for demo data
//		volatile uint8 nodeL @adrsof(DEMO_STACKFRAME3)+1, nodeH @adrsof(DEMO_STACKFRAME3)+2; //EEADRL); //GET_FRAME_STACKFRAME3) + 1; //kludge: reuse unused SFR for demo data
//		for (node.as16 = 0; node.as16 < NUM_NODES(4 BPP); ++node.as16) //150 nodes * 0.1 sec == 15 sec / color
//ramsc  #n
//  1    NB*2  max 512
//  2    NB*4  max 1K
//  4    NB*8  max 2K
//kludge: preset loop counter and count backwards to avoid the need to store a separate loop limit:
//BoostC generates 2 temps for this		node.as16 = NodeBytes << (RAM_SCALE_BITS + 1);
//		rl_WREG(NodeBytes); node.bytes[0] = WREG; node.bytes[1] = 0; rl_nc(node.bytes[1]);
//		if (RAM_SCALE > 1) { rl_nc(node.bytes[0]); rl_nc(node.bytes[1]); }
//		if (RAM_SCALE > 2) { rl_nc(node.bytes[0]); rl_nc(node.bytes[1]); }
		volatile uint2x8 limit @adrsof(PROTOCOL_STACKFRAME3) + 0; //use as temps only for loop limit checking; overwritten at other times
		descale_nodebytes(limit);
//		fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //point to first node
//		while (--node.as16) //150 nodes * 0.1 sec == 15 sec / color
		for (node.as16 = 0; node.as16 < limit.as16; ++node.as16) //150 nodes * 0.1 sec == 15 sec / color
		{
//	if (node.bytes[0] >= 4) break;
//			if (node < 50) rr_WREG(node);
//			else WREG = node;
//#if RAM_SCALE > 2 //NUM_NODES(4 BPP) > 512
//// #ifndef PIC16X
////  #error "[ERROR] Too many nodes for non-extended demo code "NUM_NODES(4 BPP)""
//// #endif
// 			rr_WREG(node.bytes[1]);
// 			fsrH -= WREG; //jump ahead 512 nodes; assumes linear addressing
//#endif
//			WREG = node.bytes[1] + 0xFF; //pre-load Carry with node# & 256 so right-shift result is correct; works up to 512 nodes
// 			rr_nc_WREG(node.bytes[1]); //pre-load Carry with node# & 256 so right-shift result is correct; works up to 512 nodes
//			rr_nc_WREG(node.bytes[0]);
//			SetNode_WREG(node.bytes[0], color);
//			add_banked256(WREG, IsDecreasing(NODE_DIRECTION), 0); //adjust fsr for bank gaps/wrap; NOTE: gece node adrs might wrap; this is okay since it still points into node space
//			if (gece) gece_node_overflow(<); //adjust fsrL for gece overflow nodes
#if 0 //tx + uart chain + feedback loop test
			WREG = color & 0x0F; WREG += 'A'; txreg = WREG;
#endif
			if (gece_demo) //not enough RAM to hold all nodes on smaller PICs; GECE nodes are persistent anyway, so just send the ones that changed
			{
				volatile uint8 svmask @adrsof(PROTOCOL_STACKFRAME3) + 2, svnode @adrsof(LEAF_PROC); //PROTOCOL_STACKFRAME3) + 1; //these are available since demo/test code is running
				gece_new_fifo_entry(ALLOC); //set fsr to next entry
				indf_autonode = RENXt_NODELIST(0);
				indf_autonode = color & 0x77; //remember which color to set
				if (ParallelNodes) //8 strings; string length on each pin == NodeBytes << RAM_SCALE / 4; compensate for node addresses across I/O pins
				{
					WREG = node.bytes[0]; //which string and I/O pin (0..7)
					pin2mask_WREG();
					indf_autonode = WREG; //mask
					svmask = WREG;
//interleaved addressing: 0, 8, 16, ..., 392, 400->1, 9, 17, ..., 401->2, 10, 18, ..., 402->3, ...., 407
//0b1, 0b2345 6xxx => 6xxx 2345 => 0bxx12 3456
					swap_WREG(node.bytes[1]); WREG &= 0x70; indf = WREG;
					swap_WREG(node.bytes[0]); WREG &= 0x8F; indf |= WREG; rl(indf); //divide by 8: node# 0..239 => gece node adrs 1..30
					++indf;
					node.as16 += 8; //interleave by string instead of by I/O pin
					if ((svmask != 0x01) && (node.as16 >= limit.as16)) node.as16 -= limit.as16; //wrap to next string if not last
					else --node.as16;
				}
				else //string length on one I/O pin == 2 * NodeBytes << RAM_SCALE
				{
					indf_autonode = 0x40; //series string on pin A2
					svmask = WREG;
					indf = node.bytes[0] + 1; //gece address of this node; ignore nextnodeH (node# can't be > 63)
				}
				svnode = indf; //remember node adrs for alternating nodes during second pass
				gece_new_fifo_entry(COMMIT);
//				if (svnode >= 63) break; //reached max node
				if (color & 8) //just set every other node second time around (for alternating spacing test)
				{
					indf_autonode = RENXt_NODELIST(0);
					indf_autonode = BLACK_PALINX; //remember which color to set for later
					indf_autonode = svmask;
					indf = svnode; WREG = -1; if (svnode & 1) WREG = 1; indf += WREG; //^ (1^2); //clear the one before or after
					gece_new_fifo_entry(COMMIT);
				}
//				send_gece_4bpp(); //override send-all: gece are randomly addressable, so just send changed node (not enough RAM to hold all nodes)
			}
			else //series or parallel RGB strings
			{
				fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //point to first node
				if (RAM_SCALE > 1) { rr_WREG(node.bytes[1]); fsrH -= WREG; } //assume linear addressing with > 256 bytes RAM
				rr_nc_WREG(node.bytes[1]); rr_nc_WREG(node.bytes[0]); add_banked256(WREG, IsDecreasing(NODE_DIRECTION), 0);
//one series string on 1 I/O pin; 1 byte/2 nodes
//4 parallel strings on 4 I/O pins; 2 bytes/4 nodes; string length on each pin == NodeBytes << RAM_SCALE / 2; compensate for node addresses across I/O pins
//0, 4, 8, ..., 1, 5, 9, ..., 2, 6, 10, ..., 3, 7, 11, ... => '0:0x80, '2:0x80, '4:0x80, ..., '0:0x40, '1:0x40, '2: 0x40, ..., '0:0x20, '1:0x20, '2:0x20, ..., '0:0x10, '1:0x10, '2:0x10, ...
				if (ParallelNodes) //byte-pairs for each node group (4 parallel nodes)
				{
					volatile uint8 strcount @adrsof(PROTOCOL_STACKFRAME3) + 2, shtemp @adrsof(LEAF_PROC); //PROTOCOL_STACKFRAME3) + 1; //these are available since demo/test code is running
//parallel palette entries are: 0,1,2 = old color (4 parallel nodes, initially black), 3,4,5,6 = 1,2,3,4 parallel nodes on (new color)
//byte pair for parallel nodes is: pRRRRRGG, GGGBBBBB
//for each color transition, byte pair will cycle: 4 off -> 1 on, 3 off -> 2 on, 2 off -> 3 on, 1 off -> 4 on
//4 old color -> 1 new, 3 old -> 2 new, 2 old -> 3 new, 1 old -> 4 new
					fsrL |= 1; //point to first byte in byte pair (descreasing address order)
//build node grp value from old and new colors:
//example stepping from red to green:
//4 parallel red = (4 on, 0 on, 0) => (1 off/3 on, 1 on/3 off, 0) => (2 off/2 on, 2 on/2 off, 0) => (3 off/1 on, 3 on/1 off, 0) => (0, 4 on, 0) = 4 parallel green
					indir_palette[32].as16 = 0; //skip past used palette entries (16 entries, 4 bytes each)
//set past/present nodes to new color:
					WREG = node.bytes[0] & 3; strcount = 7 - WREG; //8-#strings with new color after update; save for reuse
					WREG = color + 1; WREG &= 7; if (EqualsZ) strcount += 8; //doing white; use brightness-limited entries
					Carry = FALSE; //Carry is assumed to be 0 for shifts below, and will remain so
					if (color & RED_PALINX) { rl_nc_WREG(strcount); shtemp = WREG; rl_nc_WREG(shtemp); indir_palette[32].bytes[0] |= WREG; } //.as16 |= 3<<10; //turn on red for 4 parallel nodes
					if (color & GREEN_PALINX) { WREG = strcount << 4; shtemp = WREG; rl_nc_WREG(shtemp); if (Carry) indir_palette[32].bytes[0] |= 1; indir_palette[32].bytes[1] |= WREG; } //.as16 |= 3<<5; //green for 4 parallel nodes
					if (color & BLUE_PALINX) indir_palette[32].bytes[1] |= strcount; //.as16 |= 3; //blue parallel
//set future nodes to old color:
//					strcount -= 4; //#strings with old color before update
					WREG = node.bytes[0] & 3; strcount = 7-4 - WREG; //4-#strings with old color after update
					WREG = color + 1; WREG &= 7; if (EqualsZ) strcount += 8; //doing white; use brightness-limited entries
					Carry = FALSE; //Carry is assumed to be 0 for shifts below, and will remain so
					if (color & (RED_PALINX << 4)) { rl_nc_WREG(strcount); shtemp = WREG; rl_nc_WREG(shtemp); indir_palette[32].bytes[0] |= WREG; } //.as16 |= 3<<10; //turn on red for 4 parallel nodes
					if (color & (GREEN_PALINX << 4)) { WREG = strcount << 4; shtemp = WREG; rl_nc_WREG(shtemp); if (Carry) indir_palette[32].bytes[0] |= 1; indir_palette[32].bytes[1] |= WREG; } //.as16 |= 3<<5; //green for 4 parallel nodes
					if (color & (BLUE_PALINX << 4)) indir_palette[32].bytes[1] |= strcount; //.as16 |= 3; //blue parallel
//					volatile uint2x8 palinx @adrsof(PROTOCOL_STACKFRAME3) + 0; //, palinx2 @adrsof(PROTOCOL_STACKFRAME3) + 1; //these are available since demo/test code is running
//					WREG = node.bytes[0] & 3; WREG += 3; palinx.bytes[0] = WREG; //set parallel pal inx# to #parallel nodes turned on
//					Series2Parallel(palinx); //convert palette inx# to triplet in byte pair
					indf_autonode = indir_palette[32].bytes[0]; indf_autonode = indir_palette[32].bytes[1]; //apply new color to next node in group
					if (color & 8) //just set every other node second time around (for alternating spacing test)
					{
						fsrL ^= 4; //next node in this string is 4 bytes away ;;;; // && (NodeConfig >> 4 == RENXt_GECE(SERIES)))
						add_banked(0, IsDecreasing(NODE_DIRECTION), 0); //not really needed for PIC16X
//						WREG = 0x0F; if (node.bytes[0] & 8) WREG = 0xF0; //odd nodes in lower nibble, even nodes in upper nibble
//						indf &= WREG;
//						fsrL ^= 4;
//						indf &= WREG;
						volatile uint8 node_mask @adrsof(LEAF_PROC);
						WREG = node.bytes[0] & 3; //which parallel string# to change
						pin2mask_WREG(); node_mask = WREG; swap_WREG(node_mask); WREG |= node_mask; //0 => 0x88, 1 => 0x44, 2 => 0x022, 3 => 0x11
						WREG ^= 0xFF; indf_node &= WREG; --fsrL; indf &= WREG; //set node before/after to black
					}
					node.as16 += 4; //interleave by string instead of by I/O pin
					WREG = node.bytes[0] & 3; if ((WREG != 3) && (node.as16 >= limit.as16)) node.as16 -= limit.as16; //wrap to next string if not last
					else --node.as16;
				}
//				else if (color & 8) indf = 0; //just every other node second time around (for alternating spacing test) for one series string
				else if (node.bytes[0] & 1) { if (color & 8) indf = 0; indf &= 0xF0; indf |= color & 7; /*add_banked(1, IsDecreasing(NODE_DIRECTION), 0)*/; } //odd nodes in lower nibble
				else { if (color & 8) indf = 0; indf &= 0x0F; indf |= color & 0x70; } //even nodes in upper nibble
			}
//	debug(iochars.bytes[0]);
//			send_nodes();
#if 0 //comm port test
			WREG = node.bytes[0]; WREG |= 0x20; WREG &= 0x7F; txreg = WREG;
#endif
//			wait_10thsec(); //38.4 sec total for 384 nodes
//	wait_2sec_zc(); wait_2sec_zc();
//#if WANT_GECE //gece special cases: node I/O is slow but nodes are randomly addressable, so only send to one node
			wait_20thsec_ormore(); //19.2 sec total for 384 nodes; 96 sec for 1920 nodes; stretch it out if it's a lot shorter
			descale_nodebytes(limit); //kludge: not enough RAM to store node limit, so recalculate each time
		}
//		if (gece_demo && (RAM_SCALE < 2)) third_node_grp = first_node; //kludge: initialize node group to look fresh; compensates for node space wrap
#endif
//		debug(6); //yellow
		wait_1sec();
#if 1 //ramp/fade all nodes
//NOTE: all nodes have color index at this point
//the loop below manipulates the current palette entry to avoid the need to update all the node color indices
		if (!ParallelNodes || gece_demo) //save current palette entry and then replace it; this avoids the need for another set-all
		{
			GetPalent(color & 7);
			palette[UNUSED_PALINX].R8 = indf_autopal; //save current color
			palette[UNUSED_PALINX].G8 = indf_autopal;
			palette[UNUSED_PALINX].B8 = indf_autopal;
			fsrL -= 3;
//			swap_WREG(NodeConfig); WREG ^= RENXt_GECE(DONT_CARE); WREG &= 0x0E; //WREG = NodeConfig >> 4; //split up expr to avoid BoostC generating a temp
			if (gece_demo) indf = 0; //gece colors: only need to set first byte of IBGR = 0 to turn off node; leave R/G/B alone
			else AddPalette(BLACK); //set current color off; only first byte needs to be 0 for gece
		}
		else
		{
//			WREG = color + 1; WREG &= 7;
			/*if (EqualsZ)*/ SetParallelPalette(16-4, 0, 0, 0, 0); //blank out entry for all nodes on (reduced brightness)
			/*else*/ SetParallelPalette(8-4, 0, 0, 0, 0); //blank out entry for all nodes on (full brightness)
		}
//		send_nodes();
//		wait_halfsec();

		volatile bit gece @BITADDR(adrsof(statebit_0x20));
		volatile uint2x8 brightness @adrsof(DEMO_STACKFRAME3)+1; //EEADRL); //GET_FRAME_STACKFRAME3) + 1; //kludge: reuse unused SFR for demo data
		for (brightness.as16 = 0; brightness.bytes[1] < 512/0x100; ++brightness.as16)
		{
//	if (brightness.as16 < 200) brightness.as16 = 200;
//	if (brightness.as16 > 256 + 30) break;
#if 0 //tx + uart chain + feedback loop test
			WREG = brightness.bytes[0] & 0x0F; WREG += '0'; txreg = WREG;
#endif
			GetPalent(color & 7); //set fsr to current palette entry; do this before setting W to brightness
			WREG = brightness.bytes[0];
			if (brightness.bytes[1] & 1) WREG = 255 - WREG; //ramp then fade; ramp first so start of ramp is obvious after previous one-by-one fill
//			DimmingCurve(brightness)
			ApplyDimmingCurve_WREG(); //need this for gece as well
			volatile uint8 palbyte @adrsof(LEAF_PROC); //cached palette byte
#if 1 //limit max white to 80% which is #CCCCCC; do this for all node types, not just for GECE, in case power supply is not heavy enough (full white is typically too bright anyway)
			palbyte = WREG; //temp save dim-adjusted WREG
//BoostC generates a temp here			if (fsrL == PALETTE_ADDR(7, 4 BPP) & 0xFF) //doing white
			WREG = fsrL;
			WREG ^= PALETTE_ADDR(7, 4 BPP) & 0xFF;
			if (EqualsZ) //doing white
			{
				WREG = (WHITE & 0xFF) - palbyte;
				if (Borrow) palbyte += WREG; //reduce white brightness
			}
			WREG = palbyte; //restore WREG, possibly adjusted
#endif
			if (gece_demo) //gece colors: only need to set first byte of IBGR to change intensity
			{
				indf = WREG;
				WREG = 0xCC - WREG;
				if (Borrow) indf += WREG; //limit max; { indf += WREG; brightness.bytes[0] = (0x200 - 1) - brightness.bytes[0]; ++brightness.bytes[1]; } //0x200 - 0xCC - 1; } //max gece brightness = 0xCC; skip over clipped range
				set_all_gece_alternating(color & 0x77, color & 8); //need new fifo command to refresh gece nodes
//				gece_new_fifo_entry(ALLOC); //set fsr to next entry
//				indf_autonode = RENXt_SETALL(0);
//				indf_autonode = color & 0x77; //remember which color to set for later
//				if (ParallelNodes)
//				{
//					indf_autonode = ~0; //all strings
//					indf = NodeBytes;
//					indf >>= 1; if (Carry) ++indf;
//					indf >>= 1; if (Carry) ++indf;
//				}
//				else
//				{
//					indf_autonode = 0x40; //1 string on pin A2
//					indf = NodeBytes << 1; //#nodes to send
//				}
//				gece_new_fifo_entry(COMMIT);
			}
			else if (ParallelNodes)
			{
//				volatile uint8 palbyte @adrsof(LEAF_PROC); //cached palette byte
				palbyte = WREG;
				fsrL_parapal = PARALLEL_PALETTE_ADDR(4, 4 BPP); fsrH_parapal = PARALLEL_PALETTE_ADDR(4, 4 BPP) / 0x100;
				for (;;)
				{
					WREG = 0; rl_nc(palbyte); if (Carry) WREG |= 0xF0; rl_nc(palbyte); if (Carry) WREG |= 0x0F; //copy msb to 4 parallel nodes 2x
					indf_parapal_auto = WREG;
					if (fsrL_parapal == (PARALLEL_PALETTE_ADDR(5, 4 BPP) & 0xFF)) break;
				}
				CopyParallelPalette(4, 4+8); //also update reduced brightness palette in case current color is white
//				indir_palette[4].as16 = (1 << 10) | (2<<5) | 3; //set up palette entry#0 as indirection to sub-palette entries (1, 2, 3)
			}
			else
			{
//				ApplyDimmingCurve_WREG(); //don't do this for gece
				indf = 0; if (color & /*GREEN*/ RED_PALINX) indf = WREG;
				++fsrL;
				indf = 0; if (color & /*RED*/ GREEN_PALINX) indf = WREG;
				++fsrL;
				indf = 0; if (color & BLUE_PALINX) indf = WREG;
			}
//			send_nodes();
			wait_20thsec(); //12.8 sec fade + ramp each
//	wait_halfsec();
//			if (brightness.bytes[0] & 1) debug(4); //red
//			else debug(1); //blue
		}
//		wait_1sec();
//restore current palette entry for one-by-one test:
		if (!ParallelNodes || gece_demo) //restore current palette entry
		{
			GetPalent(color & 7);
			indf_autopal = palette[UNUSED_PALINX].R8;
			indf_autopal = palette[UNUSED_PALINX].G8;
			indf_autopal = palette[UNUSED_PALINX].B8;
			if (gece_demo) set_all_gece_alternating(color & 7, color & 8); //need new fifo command to restore gece nodes (update palette alone won't refresh them)
		}
		else
		{
//			WREG = color + 1; WREG &= 7;
			/*if (EqualsZ)*/ SetParallelPalette(16-4, 0xCC, 0xCC, 0xCC, 0xCC); //all nodes on (reduced brightness)
			/*else*/ SetParallelPalette(8-4, 0xFF, 0xFF, 0xFF, 0xFF); //all nodes on (full brightness)
		}
		wait_1sec(); //wait a while before starting fade (gece just turned all back on)
//		indf = 0; if (color & /*GREEN*/ RED_PALINX) --indf;
//		++fsrL;
//		indf = 0; if (color & /*RED*/ GREEN_PALINX) --indf;
//		++fsrL;
//		indf = 0; if (color & BLUE_PALINX) --indf;
#endif
		if (NEVER) break; //avoid "never returns" warning
	}
}


//demo/test pattern:
//turn on one channel at a time, then fade/ramp all
//NOTE: use only with dumb channels
non_inline void dumb_test(void)
{
	ONPAGE(DEMO_PAGE); //keep demo code separate from protocol and I/O so they will fit within first code page with no page selects

//#if 0 //ran out of code space
	for (;;)
	{
//one at a time on/off:
		volatile uint8 row @adrsof(DEMO_STACKFRAME3); //EEADRL); //GET_FRAME_STACKFRAME3) = 0; //kludge: reuse unused SFR for demo data
		for (row = 0x80;; row >>= 1) //all rows (shared/chplex channels) + null row (dedicated pwm channels)
		{
#if 0 //tx + uart chain + feedback loop test
			WREG = '@';
			if (row.0) WREG += 1;
			if (row.1) WREG += 2;
			if (row.2) WREG += 3;
			if (row.3) WREG += 4;
			if (row.4) WREG += 5;
			if (row.5) WREG += 6;
			if (row.6) WREG += 7;
			if (row.7) WREG += 8;
			txreg = WREG;
#endif
			volatile uint8 col @adrsof(DEMO_STACKFRAME3)+1; //EEDATL);
			for (col = 0x80; col; col >>= 1)
			{
				if (col == row) continue; //skip missing chplex row/col channels
#if 0 //tx + uart chain + feedback loop test
				if (col.0) WREG = 0;
				if (col.1) WREG = 1;
				if (col.2) WREG = 2;
				if (col.3) WREG = 3;
				if (col.4) WREG = 4;
				if (col.5) WREG = 5;
				if (col.6) WREG = 6;
				if (col.7) WREG = 7;
				WREG += '0'; txreg = WREG;
#endif
//set frame buffer with 1 channel on full:
				fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100;
				indf_autonode = 255; //full bright (actually, could go one more to 256)
				indf_autonode = row;
				indf_autonode = col;
				indf = 0; //eof marker
//				iobusy = TRUE; //mark dirty
//				send_nodes();
				wait_halfsec(); //31.5 sec total for 64 channels (8*7 chplex + 1*8 dedicated)
			}
			if (!row) break; //exit after one last time thru loop for row 0
		}
//all on ramp/fade:
		for (row = 0x80;; row >>= 1) //all rows (shared/chplex channels) + null row (dedicated pwm channels)
		{
#if 0 //tx + uart chain + feedback loop test
			txreg = '.';
#endif
			volatile uint2x8 brightness @adrsof(DEMO_STACKFRAME3)+1; //EEADRL); //GET_FRAME_STACKFRAME3) + 1; //kludge: reuse unused SFR for demo data
			for (brightness.as16 = 0; brightness.bytes[1] < 512/0x100; ++brightness.as16)
			{
//set frame buffer with all channels on varying brightness:
				fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100;
				WREG = brightness.bytes[0];
				if (brightness.bytes[1]) WREG = 255 - WREG; //ramp then fade; ramp first so start of ramp is obvious after previous one-by-one
				ApplyDimmingCurve_WREG();
				indf_autonode = WREG; //brightness
				indf_autonode = row;
				indf_autonode = ~row; //all channels for this row
				IFRP1(rp1 = TRUE); //status |= 0x40); //RP1 kludge for 16F688 over-optimization; allows access to Bank 3
				indf_autonode = 0 - first_node; //complemented brightness from above to give the correct brightness duty cycle
				IFRP1(rp1 = FALSE); //status &= ~0x40); //RP1 kludge for 16F688 over-optimization; allows access to Bank 3
				WREG = 0; indf_autonode = WREG; //no rows
				indf_autonode = WREG; //no cols
				indf = 0; //eof marker
//				iobusy = TRUE; //mark dirty
//				send_nodes();
				wait_20thsec(); //12.8 sec fade + ramp each
			}
			if (!row) break; //exit after one last time thru loop for row 0
		}
//all off:
		fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100;
		indf_autonode = 255; //full bright
		WREG = 0; indf_autonode = WREG; //no rows
		indf_autonode = WREG; //no cols
		indf = 0; //eof marker
//		iobusy = TRUE; //mark dirty
//		send_nodes();
		wait_halfsec();
		if (NEVER) break; //avoid "never returns" warning
	}
//#endif
//	nop1();
}
#endif //def WANT_TEST


#if 0 //com port test
#warning "[INFO] Demo pattern = com port test"
#define node_test  comm_test
non_inline void node_test(void)
{
	ONPAGE(DEMO_PAGE); //keep demo code separate from protocol and I/O so they will fit within first code page with no page selects
//	TRAMPOLINE(1); //kludge: compensate for call across page boundary (add_banked)

	NodeConfig = 0;
	trisa = ~4;
	for (;;)
	{
		porta = 4;
		txreg = '1';
		wait_halfsec();

		porta = 0;
		txreg = '0';
		wait_halfsec();
	}
}
#endif


#include "RenXt_eyeball_demo.h"
#include "RenXt_icicle_demo.h"
//#include "RenXt_TaeKwonDo_demo.h"

//#define debug(n)  porta = n
//{\
//	porta = 0;
//	wait_halfsec();
//	porta = n;
//	wait_halfsec();
//	porta = 0;
//	wait_halfsec();
//	porta = n;
//	wait_halfsec();
//}


//;==================================================================================================
//;Main
//hard-coded, driven from rom tables.
//;==================================================================================================

//wrap previous initialization into a function (for easier debug, no functional purpose):
non_inline void real_init(void)
{
	ONPAGE(LEAST_PAGE); //only called from main so put it on same code page
//	TRAMPOLINE(1); //kludge: compensate for call across page boundary (set_all)
	init(); //prev init first
}
#undef init
#define init()  real_init() //initialization wedge, for compilers that don't support static initialization


#if 0 //hard-code Memorial: R, W, B
#warning "[INFO] Demo pattern = Memorial day pattern"
#define node_test  Memorial_node_test //override
non_inline void node_test(void)
{
	ONPAGE(DEMO_PAGE); //keep demo code separate from protocol and I/O so they will fit within first code page with no page selects
//	TRAMPOLINE(1); //kludge: compensate for call across page boundary (add_banked)

#define ON4  4
#define OFF4  0
#define SETCOLOR(r, g, b)  \
{ \
	indf_autonode = ((r << 10) | (g<<5) | b) >> 8; \
	indf_autonode = ((r << 10) | (g<<5) | b); \
}
	is_gece();
	bitcopy(IsGece, gece_demo);
	demo_palette(); //add demo palette entries; all nodes were already initialized to bkg color during init; 0 = off, 4 = on
	rgb2parallel(0);
	set_all(0);
	wait_20thsec(); //set all nodes to initial color and wait for completion

	volatile uint8 node @adrsof(DEMO_STACKFRAME3) = 0;
	fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //point to first node
	for (node = 0; node < 17; ++node)
	{
		SETCOLOR(OFF4, ON4, OFF4); //red
		add_banked(0, IsDecreasing(NODE_DIRECTION), 0); //adjust fsr for bank gaps/wrap; only need to check every 16 bytes
	}
	for (node = 0; node < 17; ++node)
	{
		SETCOLOR(ON4, ON4, ON4); //white
		add_banked(0, IsDecreasing(NODE_DIRECTION), 0); //adjust fsr for bank gaps/wrap; only need to check every 16 bytes
	}
	for (node = 0; node < 17; ++node)
	{
		SETCOLOR(OFF4, OFF4, ON4); //blue
		add_banked(0, IsDecreasing(NODE_DIRECTION), 0); //adjust fsr for bank gaps/wrap; only need to check every 16 bytes
	}
	for (node = 0; node < 14; ++node)
	{
		SETCOLOR(ON4, ON4, ON4); //white
		add_banked(0, IsDecreasing(NODE_DIRECTION), 0); //adjust fsr for bank gaps/wrap; only need to check every 16 bytes
	}
	for (node = 0; node < 14; ++node)
	{
		SETCOLOR(OFF4, ON4, OFF4); //red
		add_banked(0, IsDecreasing(NODE_DIRECTION), 0); //adjust fsr for bank gaps/wrap; only need to check every 16 bytes
	}
	for (;;)
	{
		wait_halfsec(); //256 * 150 msec ~= 38 sec
		if (NEVER) return;
	}
}
#endif


#if 0 //hard-code Halloween: orange/purple
#define node_test  Halloween_node_test //override
non_inline void node_test(void)
{
	ONPAGE(DEMO_PAGE); //keep demo code separate from protocol and I/O so they will fit within first code page with no page selects
//	TRAMPOLINE(1); //kludge: compensate for call across page boundary (add_banked)

#if DEVICE == PIC16F1825 //Renard-PX1 uses A0, A1, and A5 for status LEDs, so leave them at their correct values; NOTE: LEDs are connected directly to VCC, so status bits are inverted (provide ground)
#warning "[INFO] Demo pattern = Halloween pattern (parallel)"
#define ON4  4
#define OFF4  0
#define SETCOLOR(r, g, b)  \
{ \
	indf_autonode = ((r << 10) | (g<<5) | b) >> 8; \
	indf_autonode = ((r << 10) | (g<<5) | b); \
}
	NodeConfig = (RENXt_WS2811(PARALLEL) << 4) | (4 BPP & BPP_MASK);

	is_gece();
	bitcopy(IsGece, gece_demo);
#if 0 //red drip
	SetParallelPalette(1, 0, 0, 0, 0);
	SetParallelPalette(5, 0xFF, 0xFF, 0xFF, 0xFF);
	volatile uint8 node2 @adrsof(DEMO_STACKFRAME3) = 0;
	volatile uint8 ofs2 @adrsof(DEMO_STACKFRAME3) + 1;
	for (;;)
	for (ofs2 = 0; ofs2 < 55; ++ofs2)
	{
		fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //point to first node
//all off:
		for (node2 = 0; node2 < 80; ++node2)
		{
			if (node2 > ofs2) SETCOLOR(1, 1, 1);
			else if (ofs2 <= 8) SETCOLOR(5, 5, 5);
			else
			{
				WREG = ofs2 - 8;
				if (node2 > WREG) SETCOLOR(5, 5, 5);
				else SETCOLOR(1, 1, 1);
			}
			add_banked(0, IsDecreasing(NODE_DIRECTION), 0); //adjust fsr for bank gaps/wrap; only need to check every 16 bytes
		}
//		wait_halfsec();
		for (node2 = ofs2 >> 4; node2 < 6; ++node2)
			wait_20thsec();
		if (NEVER) return;
	}
#endif

//	demo_palette(); //add demo palette entries; all nodes were already initialized to bkg color during init; 0 = off, 4 = on
//purple:
	SetParallelPalette(1, 0, 0, 0, 0);
	SetParallelPalette(2, 0x40, 0x40, 0x40, 0x40);
	SetParallelPalette(3, 0x60, 0x60, 0x60, 0x60);
//orange:
	SetParallelPalette(4, 0x40, 0x40, 0x40, 0x40);
	SetParallelPalette(5, 0xFF, 0xFF, 0xFF, 0xFF);
	SetParallelPalette(6, 0x10, 0x10, 0x10, 0x10);

//	rgb2parallel(0);
//	set_all(0);
//	wait_20thsec(); //set all nodes to initial color and wait for completion

	volatile uint8 node @adrsof(DEMO_STACKFRAME3) = 0;
	volatile uint8 ofs @adrsof(DEMO_STACKFRAME3) + 1;
//	for (node = 0; node < 50-13; ++node)
//	{
//		SETCOLOR(1, 2, 3); //purple
//		add_banked(0, IsDecreasing(NODE_DIRECTION), 0); //adjust fsr for bank gaps/wrap; only need to check every 16 bytes
//	}
//	for (node = 0; node < 30+13; ++node)
//	{
//		SETCOLOR(4, 5, 6); //orange
//		add_banked(0, IsDecreasing(NODE_DIRECTION), 0); //adjust fsr for bank gaps/wrap; only need to check every 16 bytes
//	}
	front_panel(~FP_COMMERR);
	front_panel(~FP_RXIO);
	for (;; ++ofs)
	{
		fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //point to first node
		for (node = ofs; node < 80+16; ++node)
		{
			WREG = node & 7;
			if (EqualsZ) SETCOLOR(0, 0, 0); //avoid blending
			else if (node & 8) SETCOLOR(1, 2, 3); //purple
			else SETCOLOR(4, 5, 6); //orange
			add_banked(0, IsDecreasing(NODE_DIRECTION), 0); //adjust fsr for bank gaps/wrap; only need to check every 16 bytes
		}
		if (ofs & 8) front_panel(FP_OTHER);
		else front_panel(~FP_OTHER);
//		wait_halfsec();
		wait_halfsec();
		if (NEVER) return;
	}
#else
#warning "[INFO] Demo pattern = Halloween pattern (series)"
//	is_gece();
//	bitcopy(IsGece, gece_demo);
//	demo_palette(); //add demo palette entries; all nodes were already initialized to bkg color during init; 0 = off, 4 = on
//#define ApplyDimmingCurve_WREG()
//	SetPalette(1, 0x004060); //purple; R/G swapped
////	SetPalette(2, 0x404000); //orange; R/G swapped
//	SetPalette(3, 0x40FF10); //orange; R/G swapped
//#undef ApplyDimmingCurve_WREG
//	rgb2parallel(0);
//	NodeConfig = RENXt_PWM(COMMON_CATHODE) << 4; //(RENXt_WS2811(PARALLEL) << 4) | (4 BPP & BPP_MASK);
//	NodeConfig = 0; //no I/O
	volatile uint8 delay @adrsof(DEMO_STACKFRAME3) + 1;

#if 0
	swap_WREG(indf); WREG &= 0x0F; //port A row bit
//hard-coded for 4 of 6 pins on Port A, Port C
	portbuf.bytes[0] = WREG; //set row output pin high, columns low; these will be inverted below if they are common cathode
//		if (CommonCathode) compl(portbuf.bytes[0]); //= ~portbuf.bytes[0]; //rows are low, columns are high; BoostC doesn't seem to know about the COM instr
	WREG ^= TRISA_INIT(CHPLEX); //set row pin to Output ("0"), columns to Input (Hi-Z) = "1", special output pins remain as-is (SEROUT), and A4 is hard-wired to Input
	trisbuf.bytes[0] = WREG;
	if (ActiveHigh) portbuf.bytes[0] = WREG; //also update row + col pins with inverted values; //compl(portbuf.bytes[0]); //= ~portbuf.bytes[0]; //rows are low, columns are high; BoostC doesn't seem to know about the COM instr
	WREG = indf_autonode; WREG &= 0x0F; //port C row bit
	portbuf.bytes[1] = WREG; //set row output pin high, columns low
//		if (CommonCathode) compl(portbuf.bytes[1]); //= ~portbuf.bytes[1]; //rows are low, columns are high; BoostC doesn't seem to know about the COM instr
	WREG ^= TRISBC_INIT(CHPLEX); //set row pin to Output ("0"), columns to Input, special output pins remain as-is
	trisbuf.bytes[1] = WREG;
	if (ActiveHigh) portbuf.bytes[1] = WREG; //also update row + col pins with inverted values; //compl(portbuf.bytes[1]); //= ~portbuf.bytes[1]; //rows are low, columns are high; BoostC doesn't seem to know about the COM instr
//get column outputs:
	swap_WREG(indf); WREG &= 0x0F; //port A column bits
	/*if (indf & 0x80) WREG |= 0x10; / /if (indf & 0x80)*/ WREG += 8; //A3 is input-only; shift it to A4
//		if (!CommonCathode) WREG = ~WREG; //cols are low instead of high
	trisbuf.bytes[0] ^= WREG; //set column pins also to Output (should have been set to Input when row was set above); A4 will be incorrect, but it's hard-wired to Input so it doesn't matter
	WREG = indf_autonode; WREG &= 0x0F; //port C column bits
//		if (!CommonCathode) WREG = ~WREG; //cols are low instead of high
	trisbuf.bytes[1] ^= WREG; //set column pins also to Output (should have been set to Input when row was set above)
#endif
#if 0 //low-level
	trisa = TRISA_INIT(PWM); //set row pin to Output ("0"), columns to Input (Hi-Z) = "1", special output pins remain as-is (SEROUT), and A4 is hard-wired to Input
	trisbc = TRISBC_INIT(PWM); //set row pin to Output ("0"), columns to Input (Hi-Z) = "1", special output pins remain as-is (SEROUT), and A4 is hard-wired to Input
	volatile uint8 delay @adrsof(DEMO_STACKFRAME3) + 1;
	for (;; ++delay)
	{
		if (delay <= 0x40) { porta = (SWAP8(0x06 | 0x09) & 0x0F) + 8; portbc = (0x06 | 0x09) & 0x0F; }
		else /*if (delay <= 0x80)*/ { porta = (SWAP8(0x09) & 0x0F) + 8; portbc = (0x09) & 0x0F; }
//		else { porta = 0; portbc = 0; }
		wait(40 usec, EMPTY);
	}
	for (;;)
	{
		porta = (SWAP8(0x80) & 0x0F) + 8; //NOT inverted
		portbc = 0x80 & 0x0F; //NOT inverted
	
//		fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //point to first node
//		indf_autonode = 255; //brightness
//		indf_autonode = 0; //row 0: non-chplex (pwm)
//		indf_autonode = 0x88; //column index
//		indf_autonode = 0; //eof
//		trisa = TRISA_INIT(PWM); //set row pin to Output ("0"), columns to Input (Hi-Z) = "1", special output pins remain as-is (SEROUT), and A4 is hard-wired to Input
//		trisbc = TRISBC_INIT(PWM); //set row pin to Output ("0"), columns to Input (Hi-Z) = "1", special output pins remain as-is (SEROUT), and A4 is hard-wired to Input
		porta = (SWAP8(0x80) & 0x0F) + 8; //NOT inverted
		portbc = 0x80 & 0x0F; //NOT inverted
//		trisa &= (SWAP8(~0x88) & 0x0F) + 8;
//		trisbc &= ~0x88 & 0x0F;
		wait_halfsec();
		wait_halfsec();

//		fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //point to first node
//		indf_autonode = 255; //brightness
//		indf_autonode = 0; //row 0: non-chplex (pwm)
//		indf_autonode = 0x44; //column index
//		indf_autonode = 0; //eof
//		trisa = TRISA_INIT(PWM); //set row pin to Output ("0"), columns to Input (Hi-Z) = "1", special output pins remain as-is (SEROUT), and A4 is hard-wired to Input
//		trisbc = TRISBC_INIT(PWM); //set row pin to Output ("0"), columns to Input (Hi-Z) = "1", special output pins remain as-is (SEROUT), and A4 is hard-wired to Input
		porta = (SWAP8(0x40) & 0x0F) + 8; //inverted
		portbc = 0x40 & 0x0F; //inverted
//		trisa &= (SWAP8(~0x44) & 0x0F) + 8;
//		trisbc &= ~0x44 & 0x0F;
		wait_halfsec();
		wait_halfsec();

//		fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //point to first node
//		indf_autonode = 255; //brightness
//		indf_autonode = 0; //row 0: non-chplex (pwm)
//		indf_autonode = 0x22; //column index
//		indf_autonode = 0; //eof
//		trisa = TRISA_INIT(PWM); //set row pin to Output ("0"), columns to Input (Hi-Z) = "1", special output pins remain as-is (SEROUT), and A4 is hard-wired to Input
//		trisbc = TRISBC_INIT(PWM); //set row pin to Output ("0"), columns to Input (Hi-Z) = "1", special output pins remain as-is (SEROUT), and A4 is hard-wired to Input
		porta = (SWAP8(0x20) & 0x0F) + 8; //inverted
		portbc = 0x20 & 0x0F; //inverted
//		trisa &= (SWAP8(~0x22) & 0x0F) + 8;
//		trisbc &= ~0x22 & 0x0F;
		wait_halfsec();
		wait_halfsec();

//		fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //point to first node
//		indf_autonode = 255; //brightness
//		indf_autonode = 0; //row 0: non-chplex (pwm)
//		indf_autonode = 0x11; //column index
//		indf_autonode = 0; //eof
//		trisa = TRISA_INIT(PWM); //set row pin to Output ("0"), columns to Input (Hi-Z) = "1", special output pins remain as-is (SEROUT), and A4 is hard-wired to Input
//		trisbc = TRISBC_INIT(PWM); //set row pin to Output ("0"), columns to Input (Hi-Z) = "1", special output pins remain as-is (SEROUT), and A4 is hard-wired to Input
		porta = (SWAP8(0x10) & 0x0F) + 8; //inverted
		portbc = 0x10 & 0x0F; //inverted
//		trisa &= (SWAP8(~0x11) & 0x0F) + 8;
//		trisbc &= ~0x11 & 0x0F;
		wait_halfsec();
		wait_halfsec();

		porta = (SWAP8(0x08) & 0x0F) + 8; //inverted
		portbc = 0x08 & 0x0F; //inverted
		wait_halfsec();
		wait_halfsec();

		porta = (SWAP8(0x04) & 0x0F) + 8; //inverted
		portbc = 0x04 & 0x0F; //inverted
		wait_halfsec();
		wait_halfsec();

		porta = (SWAP8(0x02) & 0x0F) + 8; //inverted
		portbc = 0x02 & 0x0F; //inverted
		wait_halfsec();
		wait_halfsec();

		porta = (SWAP8(0x01) & 0x0F) + 8; //inverted
		portbc = 0x01 & 0x0F; //inverted
		wait_halfsec();
		wait_halfsec();

//		fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //point to first node
//		indf_autonode = 0; //eof
//		trisa = TRISA_INIT(PWM); //set row pin to Output ("0"), columns to Input (Hi-Z) = "1", special output pins remain as-is (SEROUT), and A4 is hard-wired to Input
//		trisbc = TRISBC_INIT(PWM); //set row pin to Output ("0"), columns to Input (Hi-Z) = "1", special output pins remain as-is (SEROUT), and A4 is hard-wired to Input
		porta = (SWAP8(0) & 0x0F) + 8; //inverted
		portbc = 0 & 0x0F; //inverted
//		trisa &= (SWAP8(~0) & 0x0F) + 8;
//		trisbc &= ~0 & 0x0F;
		wait_halfsec();
//		wait_halfsec();
		if (NEVER) return;
	}
#endif
#if 1 //high-level
	for (;;)
	{
		fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //point to first node
		indf_autonode = 0x40; //brightness
		indf_autonode = 0; //row 0: non-chplex (pwm)
		indf_autonode = 0x06; //column index red
		indf_autonode = 0xc0; //brightness
		indf_autonode = 0; //row 0: non-chplex (pwm)
		indf_autonode = 0x06 | 0x09; //column index red+blue
		indf_autonode = 0; //eof
		for (delay = 0; delay < 5*2; ++delay) wait_halfsec();

		fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //point to first node
		indf_autonode = 0xff; //brightness
		indf_autonode = 0; //row 0: non-chplex (pwm)
		indf_autonode = 0x30; //column index white
		indf_autonode = 0; //eof
		wait_20thsec();

		fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //point to first node
		indf_autonode = 0x40; //brightness
		indf_autonode = 0; //row 0: non-chplex (pwm)
		indf_autonode = 0x06; //column index red
		indf_autonode = 0xc0; //brightness
		indf_autonode = 0; //row 0: non-chplex (pwm)
		indf_autonode = 0x06 | 0x09; //column index red+blue
		indf_autonode = 0; //eof
		for (delay = 0; delay < 20*2; ++delay) wait_halfsec();

		fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //point to first node
		indf_autonode = 0xff; //brightness
		indf_autonode = 0; //row 0: non-chplex (pwm)
		indf_autonode = 0x30; //column index white
		indf_autonode = 0; //eof
		wait_20thsec();

		fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //point to first node
		indf_autonode = 0xff; //brightness
		indf_autonode = 0;
		indf_autonode = 0; //row 0: non-chplex (pwm)
		indf_autonode = 0; //eof
		for (delay = 0; delay < 3; ++delay) wait_20thsec();

		fsrL = NODE_ADDR(0, 4 BPP); fsrH = NODE_ADDR(0, 4 BPP) / 0x100; //point to first node
		indf_autonode = 0xff; //brightness
		indf_autonode = 0; //row 0: non-chplex (pwm)
		indf_autonode = 0x30; //column index white
		indf_autonode = 0; //eof
		wait_20thsec();
		if (NEVER) return;
	}
#endif	
#endif
	for (;;)
	{
		wait_halfsec(); //256 * 150 msec ~= 38 sec
		if (NEVER) return;
	}
}
#endif

#if 0 //hard-code Easter 0,40,ff -> 0,80,80
#warning "[INFO] Demo pattern = Easter pattern"
#define node_test  Easter_node_test //override
non_inline void node_test(void)
{
	ONPAGE(DEMO_PAGE); //keep demo code separate from protocol and I/O so they will fit within first code page with no page selects
//	TRAMPOLINE(1); //kludge: compensate for call across page boundary (add_banked)

	is_gece();
	bitcopy(IsGece, gece_demo);
	demo_palette(); //add demo palette entries; all nodes were already initialized to bkg color during init
	rgb2parallel(0);
	set_all(0);
	wait_20thsec(); //set all nodes to initial color and wait for completion

	volatile uint8 color @adrsof(DEMO_STACKFRAME3) = 0;
	for (;;)
	{
//0,40,ff -> 0,80,80
		WREG = color; if (color & 0x80) WREG = 0 - WREG; //0..0x80..0
		palette[0].R8 = 0; //green
		palette[0].G8 = WREG; //red
		palette[0].B8 = WREG; //blue
		palette[0].G8 >>= 1; palette[0].G8 += 0x40; //0x40..0x80..0x40
		palette[0].B8 = ~ palette[0].B8; //0xff..0x7f..0xff
		rgb2parallel(0);
//		set_all(0);
		REPEAT(3, wait_20thsec()); //256 * 150 msec ~= 38 sec
		++color;
		if (NEVER) return;
	}
}
#endif


//NOTE: open RS485 rx line causes serial data to be received (with frame errors); we want to ignore these
//serial data recevied at wrong baud rate also causes frame errors; we want to auto-detect the correct baud rate in this case
//since frame errors are ambiguous, look at when they occur to decide how to handle them:
//- within first 2 sec, assume RS485 rx line is open and ignore them
//- after first 2 sec, try to auto-detect the correct baud rate
void main(void)
{
//	/*volatile*/ uint8 demo_pattern @adrsof(LEAF_PROC); //set from eeprom during init
	ONPAGE(LEAST_PAGE); //keep demo code separate from protocol and I/O so they will fit within first code page with no page selects
//	TRAMPOLINE(1); //kludge: compensate for call across page boundary (protocol)

	init(); //set up control regs, init memory, etc; no protocol is handled during this time
//	protocol_inactive = FALSE; //kludge: force serial data to be ignored (echo is off, protocol() sees recursion so won't process it)
	wait_2sec_zc(); //give GECE nodes time to stabilize; ZC monitoring also needs >= 1 sec to get a stable count; serial input is ignored during this time so delay period won't be interrupted
	protocol_inactive = TRUE; //kludge: force serial data to be ignored (echo is off, protocol() sees recursion so won't process it)
//NOTE: can't send to GECE until 2 sec after powerup
	if (LEAST_PAGE != PROTOCOL_PAGE) TRAMPOLINE(1); //kludge: compensate for call across page boundary
//	gece_init(); //set initial gece port + fifo state, assign addresses (must wait until after 2 sec stabilizatio period)
//	set_all(0); //NOTE: this will start GECE I/O; must send to all nodes to initialize addresses;;;; incoming serial data ignored during init (echo is off)

	for (;;) //loop to allow resume of demo/test if protocol() returns
	{
		stats_init(); //clear I/O stats after all other init (junk chars might have come in)

//	send_nodes();
//inline void Flush_serin(void)
//		if (ignore_frerrs && HasFramingError) SerialErrorReset(); /*NoSerialWaiting = TRUE*/ /*ignore frame errors so open RS485 line doesn't interrupt demo sequence*/ \
//	WREG = rcreg;
//	ioerrs = 0;
//	zero24(iochars.as24);
//	debug(iochars.bytes[0]);
//	wait_2sec_zc(); wait_2sec_zc();
	

//	if (!IsCharAvailable && !HasFramingError && !IsNonZero24(iochars.as24)) ]
//	if (!ioerrs) ProtocolListen = TRUE; //comm port is clean; start listening for data packets (otherwise ignore it so open RS485 doesn't interrupt test/demo)
//	zc_stable = TRUE;
//	iobusy = TRUE; //end of init period; allow normal operation

//	porta = 0x23;
//	for (;;)
//	{
//		bitcopy(porta.3, porta.0);
//		wait_20thsec();
//		if (NEVER) break;
//	}
//	InitNodeConfig(); //get default node type + count, bkg
//	EEREAD_AGAIN(); ///*++eeadrL*/; demo_pattern = eedatL; //eeadrL was left pointing to demo_pattern during init
//#warning "DEMO DEMO DEMO DEMO DEMO"
//	/*if (!porta.3)*/ eedatL = 2; //high (open) => test (default from EEPROM), low (grounded) => demo/custom
// NodeConfig = 0xa0;
// WREG = 0;
	 	switch (demo_pattern)
		{
#ifdef WANT_TEST
			case 1:
				node_test();
				break;
#endif
#ifdef WANT_EYEBALL_DEMO
			case 2:
				demo(); //uses up prog space; comment this out if not needed
				break;
#endif //WANT_DEMO
#ifdef WANT_ICICLE_DEMO
		case 3:
				icicle_demo();
				break;
#endif
//TODO: put other demo/stand-alone patterns here
		}

#if 0 //DEBUG ONLY
#warning "[INFO] 3-node hard-coded test"
//	fsrH = NODE_ADDR(0, 4 BPP) / 0x100; fsrL = NODE_ADDR(0, 4 BPP); indf &= 0xF0; indf |= 4;
//	--fsrL; indf &= 0xF0; indf |= 4;
//	--fsrL; indf &= 0xF0; indf |= 4;
		IFRP1(rp1 = TRUE); //status |= 0x40); //RP1 kludge for 16F688 over-optimization
		SetNode(4, 4, 4 BPP);
		SetNode(5, 4, 4 BPP);
		SetNode(6, 4, 4 BPP);
		IFRP1(rp1 = FALSE); //status &= &0x40); //RP1 kludge for 16F688 over-optimization
		send_nodes();
#endif
//	for (; ALWAYS;)
//		wait_2sec(); //if test/demo returns, wait for protocol bytes to arrive; resuse wait loop from above since duration doesn't matter
		protocol(); //nothing to do but wait for protocol data to arrive
		if (NEVER) break; //avoid "never returns" compiler warning
	}
}
//eof
