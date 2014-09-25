/**
 * Low-Power IR Remote
 * Matt Sarnoff (msarnoff.org)
 * Version 1.0 (September 23, 2014)
 *
 * Currently only supports the 12-bit Sony protocol.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/power.h>
#include <avr/sleep.h>
#include <avr/pgmspace.h>
#include <util/delay.h>


/* Pin definitions */
#define PORTB_BUTTON_PIN_MASK 0b00011110
#define PORTB_OUTPUT_PIN_MASK 0b00000001

/* IR protocol constants */
#define IR_CARRIER_FREQ       40000
#define HEADER_BIT_DURATION   2400  /* microseconds */
#define ZERO_BIT_ON_DURATION  600   /* microseconds */
#define ONE_BIT_ON_DURATION   1200  /* microseconds */
#define BIT_OFF_DURATION      600   /* microseconds */
#define POST_DELAY_DURATION   45    /* milliseconds */

/* Other constants */
#define DEBOUNCE_DURATION     10    /* milliseconds */


/**
 * Structure defining a command.
 */
typedef struct {
  /* Indicates the button combination that transmits the given command.
   * A 1 bit denotes a pressed button.
   * Multiple 1 bits denote simultaneously held buttons.
   */
  uint8_t pinmask;

  /* 12-bit code.
   * Bits 0-6 specify the command.
   * Bits 7-11 specify the address.
   * Bits 12-15 are unused and should be 0.
   */
  uint16_t code;
} command_def;


/**
 * Button-to-command map.
 */
static const PROGMEM command_def command_defs[] = {
  { 0b00000010, 21|(1<<7) },  /* TV power */
  { 0b00000100, 18|(1<<7) },  /* TV volume up */
  { 0b00001000, 19|(1<<7) },  /* TV volume down */
  { 0b00001100, 20|(1<<7) },  /* TV mute (both volume buttons pressed) */
  { 0b00010000, 37|(1<<7) },  /* TV input select */
};
#define NUM_COMMANDS  sizeof(command_defs)/sizeof(command_def)


/**
 * Enters PWR_DOWN sleep.
 * Execution continues when a pin-change interrupt is received.
 * Cannot be used with any other external interrupts, as the GIMSK register
 * and global interrupt flag are cleared on return.
 */
void power_down_and_wait_for_pin_change(void)
{
  /* Disable output pins */
  PORTB &= ~PORTB_OUTPUT_PIN_MASK;
  DDRB = 0;

  /* Prepare for shutdown */
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  GIMSK = _BV(PCIE); /* enable pin change interrupt */

  /* Sleep now. Processing stops here until a pin change occurs */
  sei();
  sleep_cpu();  /* will sleep before any pending interrupt(s) */
}


/**
 * Pin-change interrupt (called on wakeup)
 */
ISR(PCINT0_vect, ISR_NAKED)
{
  /* Disable further pin-change interrupts to counteract switch bounce */
  GIMSK = 0;  /* we don't use INT0 so this reduces to 1 instruction */
  asm("ret"); /* don't set I flag */
}


/**
 * Enters IDLE sleep for (approximately) the given number of ticks.
 * Uses Timer1. Cannot be used if any other timer interrupts are used, as the
 * TIMSK register and global interrupt flag are cleared on return.
 */
void idle_sleep(uint8_t ticks, uint8_t prescaler)
{
  set_sleep_mode(SLEEP_MODE_IDLE);  
  power_timer1_enable(); 
  OCR1A = ticks;
  TIMSK = _BV(OCIE1A); /* interrupt when count = OCR1A */
  TCCR1 = prescaler;
  sei();
  sleep_cpu();
  power_timer1_disable(); /* interrupt handler leaves I flag clear */
}


/**
 * Timer1 interrupt (called after delay expires)
 */
ISR(TIMER1_COMPA_vect, ISR_NAKED)
{
  TIMSK = 0;  /* disable timer interrupts */
  TCCR1 = 0;  /* stop timer */
  TCNT1 = 0;  /* reset count to 0 */
  asm("ret"); /* don't set I flag */
}


/**
 * Enters IDLE sleep for the approximately the given number of microseconds.
 * Uses a 1/16 prescaler; at a 1 MHz CPU clock, the maximum sleep time is
 * 4080 us, and the minimum sleep time is 16 us. (not including the overhead
 * of the function call, setup, wakeup, interrupt handling, etc.)
 */
#define idle_sleep_us(us) idle_sleep(\
    (F_CPU*(unsigned long)(us))/(16*1000000),\
    _BV(CS12)|_BV(CS10))


/**
 * Enters IDLE sleep for the given number of milliseconds.
 * Uses a 1/4096 prescaler; at a 1 MHz CPU clock, the maximum sleep time is
 * 1045 ms, and the minimum sleep time is 5 ms. (not including the overhead
 * of the function call, setup, wakeup, interrupt handling, etc.)
 */
#define idle_sleep_ms(ms) idle_sleep(\
    (F_CPU*(unsigned long)(ms))/(4096*1000L),\
    _BV(CS13)|_BV(CS12)|_BV(CS10))


/**
 * Transmits a code using the 12-bit Sony protocol.
 * The code is sent from least-significant-bit to most-significant-bit,
 * with bit 0 (lsb of the command) sent first, and concluding with bit 11
 * (msb of the address.)
 * The transmission is followed by a 45ms delay.
 */
void transmit_sony_12bit_code(uint16_t code)
{
  /* Gating the square wave using TCCR0A provides the cleanest transitions */
  register const uint8_t ir_enabled = _BV(COM0A0) | _BV(WGM01);
  power_timer0_enable();

  /* Send the header bit */
  TCCR0A = ir_enabled;
  DDRB = PORTB_OUTPUT_PIN_MASK;
  idle_sleep_us(HEADER_BIT_DURATION);
  TCCR0A = 0;
  idle_sleep_us(BIT_OFF_DURATION);

  /* Output the data bits */
  uint8_t i;
  for (i = 0; i < 12; i++) {
    TCCR0A = ir_enabled;
    if (code & 1) {
      idle_sleep_us(ONE_BIT_ON_DURATION);
    } else {
      idle_sleep_us(ZERO_BIT_ON_DURATION);
    }
    TCCR0A = 0;
    idle_sleep_us(BIT_OFF_DURATION);
    code >>= 1;
  }

  /* Post-transmission delay */
  DDRB = 0;
  power_timer0_disable();
  idle_sleep_ms(POST_DELAY_DURATION);
}


/**
 * Determines the appropriate code for the given button state, and
 * transmits it.
 * If there is no command for the given button state, this function sleeps
 * for the debounce duration and returns.
 */
void transmit_code_for_buttons(uint8_t buttons)
{
  const command_def *cmd;
  uint8_t i;
  for (i = 0, cmd = command_defs; i < NUM_COMMANDS; i++, cmd++) {
    if (buttons == pgm_read_byte_near(&(cmd->pinmask))) {
      uint16_t code = pgm_read_word_near(&(cmd->code));
      transmit_sony_12bit_code(code);
      return;
    }
  }
  /* Not a valid command? Just debounce and return. */
  idle_sleep_ms(DEBOUNCE_DURATION);
}


/**
 * Main function.
 */
int main()
{
  /* Configure Timer0 for square wave output on PB0 */
  TCCR0A = _BV(COM0A0) | _BV(WGM01);
  OCR0A = F_CPU/(2*IR_CARRIER_FREQ);
  TCCR0B = _BV(CS00);

  /* Don't need any peripherals on startup */
  ACSR |= _BV(ACD);
  power_all_disable();

  /* All pins start as inputs; enable pullups on button inputs */
  DDRB = 0;
  PORTB = PORTB_BUTTON_PIN_MASK;
  PCMSK = PORTB_BUTTON_PIN_MASK;

  /* Allow the CPU to be put to sleep */
  sleep_enable();
  
  /* Main loop */
  uint8_t last_input = 0;
  uint8_t input = 0;
  while (1) {
    /* Shut down and wait for a keypress */
    power_down_and_wait_for_pin_change();

    /* Initial debounce */
    idle_sleep_ms(DEBOUNCE_DURATION);

    /* Read buttons */
    input = PINB;
    last_input = input; /* we've already debounced */

    /* Processing loop */
    while (1) {
      /* If the input lines are stable, execute the appropriate action */
      if (input == last_input) {
        uint8_t buttons = (~input) & PORTB_BUTTON_PIN_MASK;
        if (!buttons) {
          /* If nothing is pressed, go back to sleep. */
          break;
        } else {
          /* Otherwise, lookup and transmit the appropriate code */
          transmit_code_for_buttons(buttons);
        }
      }
      /* If the input lines aren't stable, wait */
      else {
        last_input = input;
        idle_sleep_ms(DEBOUNCE_DURATION);
      }

      /* Read the input lines again and loop */
      input = PINB;
    }
  }
}


