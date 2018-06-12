dev:
//encode port A and port B/C into a 16-bit value:
//port A in upper byte, port B/C in lower byte
#define ABC2bits16(Abits, BCbits)  ((Abits) << 8) | ((Bbits) & 0xff))
#define Abits(bits16)  ((bits16) >> 8)
#define BCbits(bits16)  ((bits16) & 0xff)

#define pin2bits16(pin)  ABC2bits(IIFNZ(isPORTA(pin), 1 << PINOF(pin)), IIFNZ(isPORTBC(pin), 1 << PINOF(pin)))
#define COL_PINS  ((PIN2BIT(RA5) | PIN2BIT(RA4) | PIN2BIT(RA2)) << 8)
#define ROW_PINS  (PIN2BIT(RC0) | PIN2BIT(RC0) | PIN2BIT(RC0) | PIN2BIT(RC0))

//cols (3) on Port A, rows (4) on Port B/C:
#define COL6  RA5
#define COL7  RA4
#define COL8  RA2
#define ROW0  RC0
#define ROW1  RC1
#define ROW2  RC2
#define ROW3  RC3
#define COLROW(c, r)  ABC2bits(r, c)
#define COL_PINS  (pin2bits16(COL6) | pin2bits16(COL7) | pin2bits16(COL8))
#define ROW_PINS  (pin2bits16(ROW0) | pin2bits16(ROW1) | pin2bits16(ROW2) | pin2bits16(ROW3))

#define KEY_1  COLROW(COL6, ROW0)
#define KEY_2  COLROW(COL7, ROW0)
#define KEY_3  COLROW(COL8, ROW0)
#define KEY_4  COLROW(COL6, ROW1)
#define KEY_5  COLROW(COL7, ROW1)
#define KEY_6  COLROW(COL8, ROW1)
#define KEY_7  COLROW(COL6, ROW2)
#define KEY_8  COLROW(COL7, ROW2)
#define KEY_9  COLROW(COL8, ROW2)
#define KEY_STAR  COLROW(COL6, ROW3)
#define KEY_0  COLROW(COL7, ROW3)
#define KEY_HASH  COLROW(COL8, ROW3)


//#define TRIS(bits16)
//{
//    TRISA = Abits(bits16);
//    TRISBC = BCbits(bits16);
//}

void port_init()
{
//doesn't matter: set rows for output and cols for input to scan faster (4 rows vs 3 cols)
//leave non-output pins as hi-Z:
    TRISA = ~(Abits(ROW_PINS) | Abits(SEROUT)); //0b111111; //Abits(COL_PINS) | Abits(pin2bits16(SERIN);
    TRISBC = ~(BCbits(ROW_PINS) | BCbits(SEROUT)); //0b100000; //BCbits(COL_PINS) | BCbits(pin2bits16(SERIN);
    PORTA = PORTBC = 0;
    WPUA = WPUBC = ~0;
}

//check for key pressed:
//ZERO = key-pressed flag, WREG = key#
void keypress_WREG()
{
//    CARRY = TRUE;
    ZERO = FALSE;
//    for (;;) //scan until key received
//    {
//    PORTA = Abits(ROW0);
        PORTBC = BCbits(ROW0);
        if (COL6) RETLW(1);
        if (COL7) RETLW(2);
        if (COL8) RETLW(3);

        PORTBC = BCbits(ROW1);
        if (COL6) RETLW(4);
        if (COL7) RETLW(5);
        if (COL8) RETLW(6);

        PORTBC = BCbits(ROW2);
        if (COL6) RETLW(7);
        if (COL7) RETLW(8);
        if (COL8) RETLW(9);

        PORTBC = BCbits(ROW3);
        if (COL6) RETLW(11);
        if (COL7) RETLW(10); //avoid confusion with null/0
        if (COL8) RETLW(12);
//    }
//    CARRY = FALSE;
//    RETLW(0);
    ZERO = TRUE;
}


//inline void on_tmr1_debounce()
//{
//    on_tmr1();
//}

//bkg event handler:
//void yield()
//{
//    on_tmr1(); //only need Timer 1 for debounce
//}


void wait_2msec()
{
//    uint16_t now = tmr1_16;
    T1ENABLED = FALSE;
    TMR1_16 = t1_preset(2 msec);
    T1ENABLED = TRUE;
    while (!T1IF) ; //wait for Timer 1 to wrap
}


void serial_init()
{
    SPBRG = baud(9600); 8N1
}


//wait for space in serial outbuf:
//assumes no errors/timeouts:
void serout_avail()
{
    while (!RCIF) ;
}
#define putchar(which)  { serout_avail(); TXREG = which; }

//sounds:
//CAUTION: ordered list
#define ECHO_0  "0.mp3"
#define ECHO_1  "1.mp3"
#define ECHO_2  "2.mp3"
#define ECHO_3  "3.mp3"
#define ECHO_4  "4.mp3"
#define ECHO_5  "5.mp3"
#define ECHO_6  "6.mp3"
#define ECHO_7  "7.mp3"
#define ECHO_8  "8.mp3"
#define ECHO_9  "9.mp3"
#define ECHO_STAR  "star.mp3"
#define ECHO_HASH  "hash.mp3"
#define WRONG  "wrong.mp3"
#define CORRECT  "correct.mp3"

//start playback of a sound file:
//0 to cancel currently playing sound
void playback(uint8_t which_WREG)
{
    if (ZERO)
    {
        putchar(CANCEL_PLAYBACK);
        putchar(delim);
    }
    else
    {
        putchar(PLAYBACK);
        putchar(WREG);
        putchar(delim);
    }
}


//get key:
//also plays touch tone and waits for key up (debounce)
//returns key in WREG
void getkey_WREG()
{
    for (;;) //yield())
    {
        keypress_WREG();
        if (ZERO) continue; //no key pressed
        wait_2msec();
        if (!ZERO) break; //key pressed + debounced
    }
    uint8_t svkey = WREG;
    playback(WREG);
    for (;;) //yield())
    {
        keypress_WREG();
        if (!ZERO) continue; //key still pressed
        wait_2msec();
        if (ZERO) break; //key released + debounced
    }
    playback(0);
    WREG = svkey;
}


//off-hook == power-up
//on-hook == power down, so no need to exit loop
#define getkey(which)  { getkey_WREG(); if (WREG != which) correct = FALSE; }
void main()
{
 //   for (;;)
 //   {
    bit_t correct = TRUE; //assume correct until proven otherwise
//check for secret code:
//wait for 4 keys before revealing result
    getkey(3);
    getkey(5);
    getkey(5);
    getkey(4);

//play correct/incorrect response:
//NOTE: ring tone or busy signal is part of response
    WREG = WRONG;
    if (correct) WREG += 1;
    playback(WREG);
//    }
    sleep(); //don't do more until next power-up
//TODO: "if you'd like to make a call, please hang up and dial again ...... obnoxious beep, beep, beep, ..."
}
