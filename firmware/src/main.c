/*
 * Hello World for Infineon TLE9879-2QXA40
 *
 * EvalKit: toggles P0.2 (LED2 when JP7 for P0.2 is fitted).
 * Also keeps a "Hello, World!" string in RAM for the debugger watch window.
 *
 * Build:  make
 * Flash:  make flash   (Segger J-Link SWD)
 */

typedef unsigned char uint8_t;
typedef unsigned int uint32_t;

/* PORT @ 0x48028000 — P0_DATA @ +0x00, P0_DIR @ +0x04 */
#define PORT_P0_DATA (*(volatile uint8_t *)0x48028000UL)
#define PORT_P0_DIR  (*(volatile uint8_t *)0x48028004UL)
#define PORT_P0_ALTSEL0 (*(volatile uint8_t *)0x48028030UL)
#define PORT_P0_ALTSEL1 (*(volatile uint8_t *)0x48028034UL)

/* SCUPM WDT1_TRIG @ 0x50006034 — must be serviced or the SoC resets */
#define SCUPM_WDT1_TRIG (*(volatile uint32_t *)0x50006034UL)
#define WDT1_TRIG_VALUE 0x3FU
#define WDT1_SOW_ONE    (1u << 6)

#define LED_PIN_MASK (1u << 2) /* P0.2 */

/* Visible in debugger; classic hello-world payload */
volatile const char g_hello[] = "Hello, World!";

static void delay_loops(volatile uint32_t n)
{
  while (n--) {
    __asm volatile("nop");
  }
}

static void wdt1_service(void)
{
  /* Short open window, then trigger — works outside the normal window */
  SCUPM_WDT1_TRIG = WDT1_SOW_ONE;
  SCUPM_WDT1_TRIG = WDT1_TRIG_VALUE;
}

static void led_init(void)
{
  /* GPIO mode (clear altsel), direction output */
  PORT_P0_ALTSEL0 &= (uint8_t)~LED_PIN_MASK;
  PORT_P0_ALTSEL1 &= (uint8_t)~LED_PIN_MASK;
  PORT_P0_DIR |= LED_PIN_MASK;
  PORT_P0_DATA &= (uint8_t)~LED_PIN_MASK;
}

static void led_toggle(void)
{
  PORT_P0_DATA ^= LED_PIN_MASK;
}

int main(void)
{
  led_init();
  wdt1_service();

  /* Touch the string so the linker keeps it and the debugger can find it */
  (void)g_hello[0];

  for (;;) {
    led_toggle();
    /* ~few hundred ms @ ~40 MHz depending on wait states */
    delay_loops(400000u);
    wdt1_service();
  }
}
