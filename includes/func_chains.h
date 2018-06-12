////////////////////////////////////////////////////////////////////////////////
////
/// Finalize function chains by wrapping inline code into discrete functions
//

#ifndef _FUNCCHAINS_H
#define _FUNCCHAINS_H


//inline function chains:
//1 chain used for each peripheral event handler + 1 for init
//this is a work-around for compilers that don't support static init
//#define init()
//#define on_tmr0()
//#define on_tmr1()
//#define on_rx()
//#define on_zc_rise()
//#define on_zc_fall()
//TODO: maybe move *_check() into individual .h files?


#ifdef init
//wrap with non-inline function for easier debug:
 non_inline void init_wrapper(void)
 {
	ONPAGE(LEAST_PAGE); //put code where it will fit with no page selects
	init(); //prev init
 }
 #undef init
 #define init()  init_wrapper() //function chain in lieu of static init
#else //dummy def to avoid conditional logic in caller
 #define init()  //nop
#endif //def init


#ifdef debug
//wrap with non-inline function for easier debug:
 non_inline void debug_wrapper(void)
 {
	ONPAGE(LEAST_PAGE); //put code where it will fit with no page selects
	debug(); //prev debug
 }
 #undef debug
 #define debug()  /*if (NEVER)*/ debug_wrapper() //function chain in lieu of expr eval in compiler #warning/#error/#pragma messages
#else //dummy def to avoid conditional logic in caller
 #define debug()  //nop
#endif //def init


#ifdef on_tmr_dim
//wrap with non-inline function for easier debug:
 non_inline void on_tmr_dim_wrapper(void)
 {
	ONPAGE(LEAST_PAGE); //put code where it will fit with no page selects
    on_tmr_dim_check(); //check if event should be triggered
	on_tmr_dim(); //chain prev evt handlers
 }
 #undef on_tmr_dim
 #define on_tmr_dim()  on_tmr_dim_wrapper() //function chain
#endif //def on_tmr_dim


#ifdef on_tmr_50msec
//wrap with non-inline function for easier debug:
 non_inline void on_tmr_50msec_wrapper(void)
 {
	ONPAGE(LEAST_PAGE); //put code where it will fit with no page selects
    on_tmr_50msec_check(); //check if event should be triggered
	on_tmr_50msec(); //chain prev evt handlers
 }
 #undef on_tmr_50msec
 #define on_tmr_50msec()  on_tmr_50msec_wrapper() //function chain
#endif //def on_tmr_50msec


#ifdef on_tmr_1sec
//wrap with non-inline function for easier debug:
 non_inline void on_tmr_1sec_wrapper(void)
 {
	ONPAGE(LEAST_PAGE); //put code where it will fit with no page selects
    on_tmr_1sec_check(); //check if event should be triggered
	on_tmr_1sec(); //chain prev evt handlers
 }
 #undef on_tmr_1sec
 #define on_tmr_1sec()  on_tmr_1sec_wrapper() //function chain
#endif //def on_tmr_1sec


#ifdef on_rx
//wrap with non-inline function for easier debug:
 non_inline void on_rx_wrapper(void)
 {
	ONPAGE(LEAST_PAGE); //put code where it will fit with no page selects
    on_rx_check(); //check if event should be triggered
	on_rx(); //chain prev evt handlers
 }
 #undef on_rx
 #define on_rx()  on_rx_wrapper() //function chain
#endif //def on_rx


#ifdef on_zc_rise
//wrap with non-inline function for easier debug:
 non_inline void on_zc_rise_wrapper(void)
 {
	ONPAGE(LEAST_PAGE); //put code where it will fit with no page selects
    on_zc_check_rise(); //check if event should be triggered
	on_zc_rise(); //chain prev evt handlers
 }
 #undef on_zc_rise
 #define on_zc_rise()  on_zc_rise_wrapper() //function chain
#endif //def on_zc_rise


#ifdef on_zc_fall
//wrap with non-inline function for easier debug:
 non_inline void on_zc_fall_wrapper(void)
 {
	ONPAGE(LEAST_PAGE); //put code where it will fit with no page selects
    on_zc_check_fall(); //check if event should be triggered
	on_zc_fall(); //chain prev evt handlers
 }
 #undef on_zc_fall
 #define on_zc_fall()  on_zc_fall_wrapper() //function chain
#endif //def on_zc_fall


#endif //ndef _FUNCCHAINS_H
//eof