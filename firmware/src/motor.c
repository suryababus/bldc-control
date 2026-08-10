/*
 * Hall-sensor six-step BLDC drive — Infineon TLE9879-2QXA40
 * Board: "BLDC Controller" (Altium, 14-04-2026), IC5 = TLE9879-2QXA40
 *
 * Motor: 6 slots / 4 magnet poles => 2 pole pairs
 *        R phase-to-phase = 0.50 ohm, supply 12 V
 *
 * ---------------------------------------------------------------------------
 * ALL REGISTER ADDRESSES BELOW ARE VERIFIED against the Infineon CMSIS header
 * tle987x.h v3.0.7 (offsetof() on the real struct definitions).
 *
 * The previous version of this file used guessed addresses and could not work:
 *   CCU6_BASE  was 0x40010000, actually 0x4000C000
 *   BDRV_BASE  was 0x50005000, actually 0x40034000
 *   CC60SR     was +0x00,      actually +0x14  (16-bit regs are padded)
 *   TCTR4      was +0x38,      actually +0x04
 *   MCMOUTS    was +0x50,      actually +0x08  (STRMCM is bit 7, not 15)
 *   MODCTR     was +0x40,      actually +0x5C  (MCMEN bit 7 was never set)
 * Every CCU6/BDRV write landed in unmapped space, so the bridge never switched.
 * ---------------------------------------------------------------------------
 *
 * HARDWARE (from BLDC_Driver_14_Apr_2026.pdf):
 *   Hall sensors: 3x TLE4946-2K, open-drain + 4.7k pull-up to VDDEXT
 *     IC2_Q -> pin 24 -> P0.3   (via R15 220R)
 *     IC3_Q -> pin 25 -> P0.2   (via R18 220R)
 *     IC4_Q -> pin 27 -> P1.4   (via R21 220R)
 *   NOTE: P0.2 is a HALL INPUT on this board. The hello-world in main.c
 *         drives P0.2 as an LED — that is an EvalKit pinout and is WRONG
 *         here; it fights the Hall sensor's open-drain output.
 *   Bridge: 6x NVMFS5C628NL on dedicated GH1..GH3 / GL1..GL3 pins (not GPIO)
 *   Shunt:  2 mOhm on SL / OP1 / OP2 (current sense, not used in this file)
 *   Angle sensor: TLE5009 iGMR on P2.0/P2.2/P2.4/P2.5 (not used in this file)
 *
 * ---------------------------------------------------------------------------
 * CURRENT SAFETY
 * ---------------------------------------------------------------------------
 * Two phases conduct in series across the bus, so the bus sees 0.50 ohm.
 * At standstill there is no back-EMF, so I = V*duty/R:
 *     100% duty at 12 V => 24 A. Bridge/motor destroying.
 * The duty cap is derived from MOTOR_I_MAX_MA:
 *     duty_max = I_max * R_pp / V_bus = 3 A * 0.5 / 12 = 12.5%
 * Raise MOTOR_I_MAX_MA only with a meter on the supply.
 *
 * With Hall feedback the motor commutates only when the rotor actually moves,
 * so a stalled rotor holds one step and sits at the duty-capped current
 * instead of running away.
 * ---------------------------------------------------------------------------
 *
 * Build:  make            (set SRCS to src/motor.c in the Makefile)
 * Flash:  make flash
 */

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;

/* ===========================================================================
 * Motor / supply parameters — EDIT TO MATCH YOUR SETUP
 * ========================================================================= */

#define MOTOR_R_PP_MOHM      500u      /* 0.50 ohm pole-to-pole, in mOhm    */
#define MOTOR_VBUS_MV        12000u    /* 12 V supply                       */
#define MOTOR_I_MAX_MA       3000u     /* current ceiling -> sets duty cap  */
#define MOTOR_POLE_PAIRS     2u        /* 6 slots / 4 poles                 */

/* Set to 1 to reverse rotation direction */
#define MOTOR_REVERSE        0

/* Startup duty while the rotor is still stationary (permille of period).
 * Hall-based startup does not need a blind ramp: we read the Hall state,
 * energize the matching step, and the rotor pulls itself into motion. */
#define KICKSTART_MS         200u

/* ===========================================================================
 * Clock / PWM
 * ========================================================================= */

#define F_CPU_HZ             40000000UL
#define PWM_FREQ_HZ          20000UL
#define PWM_PERIOD_TICKS     ((uint16_t)(F_CPU_HZ / PWM_FREQ_HZ))   /* 2000 */

/* Dead time in T12 ticks (25 ns each at 40 MHz). 40 ticks = 1.0 us. */
#define DEAD_TIME_TICKS      40u

/* Duty cap — cast-free so it works inside #if */
#define DUTY_MAX_PERMILLE    (((MOTOR_I_MAX_MA * MOTOR_R_PP_MOHM) / MOTOR_VBUS_MV))
#define DUTY_MAX_TICKS \
  ((uint16_t)(((uint32_t)PWM_PERIOD_TICKS * (uint32_t)DUTY_MAX_PERMILLE) / 1000u))

#if (DUTY_MAX_PERMILLE == 0)
#error "Duty cap computed as 0 - check MOTOR_I_MAX_MA / R_PP / VBUS."
#endif
#if (DUTY_MAX_PERMILLE > 500)
#error "Duty cap >50% with 0.5 ohm pole-to-pole. Lower MOTOR_I_MAX_MA."
#endif

/* ===========================================================================
 * Registers — VERIFIED against tle987x.h v3.0.7
 * ========================================================================= */

/* --- SCUPM ------------------------------------------------------------- */
#define SCUPM_BASE           0x50006000UL
#define SCUPM_WDT1_TRIG      (*(volatile uint32_t *)(SCUPM_BASE + 0x34UL))
#define WDT1_TRIG_VALUE      0x3FU
#define WDT1_SOW_ONE         (1u << 6)

/* --- PORT -------------------------------------------------------------- */
#define PORT_BASE            0x48028000UL
#define PORT_P0_DATA         (*(volatile uint8_t  *)(PORT_BASE + 0x00UL))
#define PORT_P0_DIR          (*(volatile uint8_t  *)(PORT_BASE + 0x04UL))
#define PORT_P0_PUDSEL       (*(volatile uint8_t  *)(PORT_BASE + 0x18UL))
#define PORT_P0_PUDEN        (*(volatile uint8_t  *)(PORT_BASE + 0x1CUL))
#define PORT_P0_ALTSEL0      (*(volatile uint8_t  *)(PORT_BASE + 0x30UL))
#define PORT_P0_ALTSEL1      (*(volatile uint8_t  *)(PORT_BASE + 0x34UL))
#define PORT_P1_DATA         (*(volatile uint8_t  *)(PORT_BASE + 0x08UL))
#define PORT_P1_DIR          (*(volatile uint8_t  *)(PORT_BASE + 0x0CUL))
#define PORT_P1_PUDSEL       (*(volatile uint8_t  *)(PORT_BASE + 0x20UL))
#define PORT_P1_PUDEN        (*(volatile uint8_t  *)(PORT_BASE + 0x24UL))
#define PORT_P1_ALTSEL0      (*(volatile uint8_t  *)(PORT_BASE + 0x38UL))
#define PORT_P1_ALTSEL1      (*(volatile uint8_t  *)(PORT_BASE + 0x3CUL))

/* Hall inputs per the board schematic */
#define HALL_A_MASK          (1u << 3)   /* P0.3 <- IC2_Q */
#define HALL_B_MASK          (1u << 2)   /* P0.2 <- IC3_Q */
#define HALL_C_MASK          (1u << 4)   /* P1.4 <- IC4_Q */

/* --- CCU6 -------------------------------------------------------------- */
#define CCU6_BASE            0x4000C000UL
#define CCU6_TCTR4           (*(volatile uint16_t *)(CCU6_BASE + 0x04UL))
#define CCU6_MCMOUTS         (*(volatile uint16_t *)(CCU6_BASE + 0x08UL))
#define CCU6_CC60SR          (*(volatile uint16_t *)(CCU6_BASE + 0x14UL))
#define CCU6_CC61SR          (*(volatile uint16_t *)(CCU6_BASE + 0x18UL))
#define CCU6_CC62SR          (*(volatile uint16_t *)(CCU6_BASE + 0x1CUL))
#define CCU6_T12PR           (*(volatile uint16_t *)(CCU6_BASE + 0x24UL))
#define CCU6_T12DTC          (*(volatile uint16_t *)(CCU6_BASE + 0x2CUL))
#define CCU6_TCTR0           (*(volatile uint16_t *)(CCU6_BASE + 0x30UL))
#define CCU6_ISS             (*(volatile uint16_t *)(CCU6_BASE + 0x4CUL))
#define CCU6_PSLR            (*(volatile uint16_t *)(CCU6_BASE + 0x50UL))
#define CCU6_MCMCTR          (*(volatile uint16_t *)(CCU6_BASE + 0x54UL))
#define CCU6_TCTR2           (*(volatile uint16_t *)(CCU6_BASE + 0x58UL))
#define CCU6_MODCTR          (*(volatile uint16_t *)(CCU6_BASE + 0x5CUL))
#define CCU6_TRPCTR          (*(volatile uint16_t *)(CCU6_BASE + 0x60UL))

/* TCTR4 bits (verified) */
#define TCTR4_T12RR          (1u << 0)
#define TCTR4_T12RS          (1u << 1)
#define TCTR4_T12RES         (1u << 2)
#define TCTR4_DTRES          (1u << 3)
#define TCTR4_T12STR         (1u << 6)

/* TCTR0 bits */
#define TCTR0_CTM            (1u << 7)   /* centre-aligned (count up/down) */

/* MODCTR bits */
#define MODCTR_T12MODEN_ALL  (0x3Fu << 0)
#define MODCTR_MCMEN         (1u << 7)   /* multi-channel mode ENABLE      */

/* MCMOUTS bits */
#define MCMOUTS_STRMCM       (1u << 7)   /* shadow transfer request        */

/* --- BDRV -------------------------------------------------------------- */
#define BDRV_BASE            0x40034000UL
#define BDRV_CTRL1           (*(volatile uint32_t *)(BDRV_BASE + 0x00UL))
#define BDRV_CTRL2           (*(volatile uint32_t *)(BDRV_BASE + 0x04UL))
#define BDRV_CP_CTRL_STS     (*(volatile uint32_t *)(BDRV_BASE + 0x20UL))
#define BDRV_CP_CLK_CTRL     (*(volatile uint32_t *)(BDRV_BASE + 0x24UL))

/* CTRL1: LS1 [2:0], LS2 [10:8], HS1 [18:16], HS2 [26:24] */
#define LS1_EN               (1u << 0)
#define LS1_PWM              (1u << 1)
#define LS1_ON               (1u << 2)
#define LS1_DS_STS           (1u << 4)
#define LS2_EN               (1u << 8)
#define LS2_PWM              (1u << 9)
#define LS2_ON               (1u << 10)
#define LS2_DS_STS           (1u << 12)
#define HS1_EN               (1u << 16)
#define HS1_PWM              (1u << 17)
#define HS1_ON               (1u << 18)
#define HS1_DS_STS           (1u << 20)
#define HS2_EN               (1u << 24)
#define HS2_PWM              (1u << 25)
#define HS2_ON               (1u << 26)
#define HS2_DS_STS           (1u << 28)

/* CTRL2: LS3 [2:0], HS3 [10:8] */
#define LS3_EN               (1u << 0)
#define LS3_PWM              (1u << 1)
#define LS3_ON               (1u << 2)
#define LS3_DS_STS           (1u << 4)
#define HS3_EN               (1u << 8)
#define HS3_PWM              (1u << 9)
#define HS3_ON               (1u << 10)
#define HS3_DS_STS           (1u << 12)

#define CTRL1_ALL_EN         (LS1_EN | LS2_EN | HS1_EN | HS2_EN)
#define CTRL2_ALL_EN         (LS3_EN | HS3_EN)

/* Drain-source fault status across both control registers */
#define CTRL1_FAULT_MASK     (LS1_DS_STS | LS2_DS_STS | HS1_DS_STS | HS2_DS_STS)
#define CTRL2_FAULT_MASK     (LS3_DS_STS | HS3_DS_STS)

/* Charge pump */
#define CPCLK_EN             (1u << 15)

/* ===========================================================================
 * Commutation
 * ===========================================================================
 * Six-step: one high side PWM-modulated, one low side static ON, third phase
 * floating. The two conducting switches are always on DIFFERENT half-bridges,
 * so shoot-through is impossible by construction; dead time covers the
 * high/low overlap within one bridge during transitions.
 *
 * Bridge phase 1/2/3 = R/Y/B phase on the schematic.
 */

typedef struct {
  uint32_t ctrl1;   /* bits to OR into BDRV_CTRL1 */
  uint32_t ctrl2;   /* bits to OR into BDRV_CTRL2 */
} step_drive_t;

/* Step order for forward rotation: A+B-, A+C-, B+C-, B+A-, C+A-, C+B- */
static const step_drive_t step_table[6] = {
  /* 0: phase1 high (PWM), phase2 low  */
  { HS1_EN | HS1_PWM | LS2_EN | LS2_ON,                     LS3_EN            },
  /* 1: phase1 high (PWM), phase3 low  */
  { HS1_EN | HS1_PWM | LS2_EN,                              LS3_EN | LS3_ON   },
  /* 2: phase2 high (PWM), phase3 low  */
  { HS2_EN | HS2_PWM | LS1_EN,                              LS3_EN | LS3_ON   },
  /* 3: phase2 high (PWM), phase1 low  */
  { HS2_EN | HS2_PWM | LS1_EN | LS1_ON,                     LS3_EN            },
  /* 4: phase3 high (PWM), phase1 low  */
  { LS1_EN | LS1_ON,                                        HS3_EN | HS3_PWM  },
  /* 5: phase3 high (PWM), phase2 low  */
  { LS2_EN | LS2_ON,                                        HS3_EN | HS3_PWM  },
};

/*
 * Hall code -> commutation step.
 *
 * Hall code = (C<<2)|(B<<1)|A, i.e. 1..6 for the six valid 120-degree states.
 * 0 and 7 are illegal (all sensors low / all high) and mean a broken sensor,
 * missing pull-up, or an unpowered VDDEXT rail.
 *
 * This is the standard 120-degree mapping. If the motor buzzes, jerks, or
 * spins the wrong way, this table is the thing to permute — see
 * hall_selftest() below, which prints the observed sequence.
 */
static const signed char hall_to_step[8] = {
  -1,   /* 000 illegal */
   0,   /* 001 */
   2,   /* 010 */
   1,   /* 011 */
   4,   /* 100 */
   5,   /* 101 */
   3,   /* 110 */
  -1,   /* 111 illegal */
};

/* ===========================================================================
 * State
 * ========================================================================= */

static volatile uint32_t g_systick_ms;
static volatile uint8_t  g_last_hall;
static volatile uint32_t g_commutations;   /* watch in the debugger */

/* ===========================================================================
 * Watchdog
 * ========================================================================= */

static void wdt1_service(void)
{
  SCUPM_WDT1_TRIG = WDT1_SOW_ONE;
  SCUPM_WDT1_TRIG = WDT1_TRIG_VALUE;
}

/* ===========================================================================
 * SysTick 1 ms
 * ========================================================================= */

#define SYST_CSR   (*(volatile uint32_t *)0xE000E010UL)
#define SYST_RVR   (*(volatile uint32_t *)0xE000E014UL)
#define SYST_CVR   (*(volatile uint32_t *)0xE000E018UL)

void SysTick_Handler(void);

void SysTick_Handler(void)
{
  g_systick_ms++;
}

static void systick_init(void)
{
  SYST_RVR = (F_CPU_HZ / 1000UL) - 1UL;
  SYST_CVR = 0;
  SYST_CSR = (1u << 2) | (1u << 1) | (1u << 0);
}

static uint32_t millis(void)
{
  return g_systick_ms;
}

static void delay_ms(uint32_t ms)
{
  const uint32_t start = millis();
  while ((millis() - start) < ms) {
    wdt1_service();
  }
}

/* ===========================================================================
 * Hall inputs
 * ========================================================================= */

static void hall_init(void)
{
  /* GPIO function (not alternate), input direction.
   * The TLE4946-2K outputs are open-drain with external 4.7k pull-ups to
   * VDDEXT, so no internal pull is required — but enabling the internal
   * pull-up costs nothing and survives a missing/blown external resistor. */
  PORT_P0_ALTSEL0 &= (uint8_t)~(HALL_A_MASK | HALL_B_MASK);
  PORT_P0_ALTSEL1 &= (uint8_t)~(HALL_A_MASK | HALL_B_MASK);
  PORT_P0_DIR     &= (uint8_t)~(HALL_A_MASK | HALL_B_MASK);
  PORT_P0_PUDSEL  |= (uint8_t)(HALL_A_MASK | HALL_B_MASK);   /* select pull-up */
  PORT_P0_PUDEN   |= (uint8_t)(HALL_A_MASK | HALL_B_MASK);

  PORT_P1_ALTSEL0 &= (uint8_t)~HALL_C_MASK;
  PORT_P1_ALTSEL1 &= (uint8_t)~HALL_C_MASK;
  PORT_P1_DIR     &= (uint8_t)~HALL_C_MASK;
  PORT_P1_PUDSEL  |= (uint8_t)HALL_C_MASK;
  PORT_P1_PUDEN   |= (uint8_t)HALL_C_MASK;
}

/* Returns 0..7; 0 and 7 indicate a sensor/wiring fault. */
static uint8_t hall_read(void)
{
  const uint8_t p0 = PORT_P0_DATA;
  const uint8_t p1 = PORT_P1_DATA;
  uint8_t code = 0u;

  if ((p0 & HALL_A_MASK) != 0u) { code |= 1u; }
  if ((p0 & HALL_B_MASK) != 0u) { code |= 2u; }
  if ((p1 & HALL_C_MASK) != 0u) { code |= 4u; }

  return code;
}

/* Debounce by requiring two identical reads. Hall edges are clean but the
 * bridge switching injects noise onto the sensor lines. */
static uint8_t hall_read_stable(void)
{
  uint8_t a, b;
  do {
    a = hall_read();
    b = hall_read();
  } while (a != b);
  return a;
}

/* ===========================================================================
 * CCU6 PWM
 * ========================================================================= */

static void ccu6_init(void)
{
  /* Halt T12 while configuring */
  CCU6_TCTR4 = TCTR4_T12RR;

  /* Period. Centre-aligned counts up then down. */
  CCU6_T12PR = (uint16_t)(PWM_PERIOD_TICKS - 1u);

  /* 0% duty on every channel — bridge passes no current yet */
  CCU6_CC60SR = 0;
  CCU6_CC61SR = 0;
  CCU6_CC62SR = 0;

  /* Dead time, enabled on all three channels */
  CCU6_T12DTC = (uint16_t)((DEAD_TIME_TICKS & 0xFFu) | (0x07u << 8));

  /* Centre-aligned mode, T12 clocked from f_sys with no prescaler */
  CCU6_TCTR0 = (uint16_t)TCTR0_CTM;

  /* Passive level low: outputs idle off */
  CCU6_PSLR = 0x0000u;

  /* Multi-channel mode off — this design gates the bridge through BDRV
   * CTRL1/CTRL2 EN/PWM/ON bits rather than CCU6 MCM, which keeps the
   * commutation logic in one place and avoids the MCMOUTS/MODCTR
   * interaction entirely. */
  CCU6_MCMCTR = 0x0000u;
  CCU6_MODCTR = MODCTR_T12MODEN_ALL;

  /* No trap source wired on this board. If you later route the shunt
   * comparator to CTRAP, enable it here for hardware overcurrent shutdown. */
  CCU6_TRPCTR = 0x0000u;

  /* Load shadows, reset dead-time, then start T12 */
  CCU6_TCTR4 = (uint16_t)(TCTR4_T12STR | TCTR4_DTRES | TCTR4_T12RES);
  CCU6_TCTR4 = TCTR4_T12RS;
}

static void ccu6_set_duty(uint16_t ticks)
{
  if (ticks > DUTY_MAX_TICKS) {
    ticks = DUTY_MAX_TICKS;      /* hard clamp — the current limit */
  }
  CCU6_CC60SR = ticks;
  CCU6_CC61SR = ticks;
  CCU6_CC62SR = ticks;
  CCU6_TCTR4  = TCTR4_T12STR;
}

/* ===========================================================================
 * BDRV
 * ========================================================================= */

static void bdrv_all_off(void)
{
  BDRV_CTRL1 = 0u;
  BDRV_CTRL2 = 0u;
}

static int bdrv_init(void)
{
  /* Everything off before the pump comes up */
  bdrv_all_off();

  /* Charge pump — high-side gates cannot turn on without it */
  BDRV_CP_CLK_CTRL |= CPCLK_EN;
  delay_ms(10);

  /* Refuse to run if any drain-source fault is latched */
  if (((BDRV_CTRL1 & CTRL1_FAULT_MASK) != 0u) ||
      ((BDRV_CTRL2 & CTRL2_FAULT_MASK) != 0u)) {
    return 0;
  }

  return 1;
}

static int bdrv_faulted(void)
{
  return (((BDRV_CTRL1 & CTRL1_FAULT_MASK) != 0u) ||
          ((BDRV_CTRL2 & CTRL2_FAULT_MASK) != 0u));
}

/* Apply one commutation step to the bridge. Writes whole registers so the
 * previous step's switches are released in the same store — no read-modify-
 * write window where two high sides could be enabled at once. */
static void bdrv_apply_step(uint8_t step)
{
  const step_drive_t *d = &step_table[step];

  /* Order matters: clear first, then set, so we never transiently enable
   * a high and low side on the same leg. */
  BDRV_CTRL1 = 0u;
  BDRV_CTRL2 = 0u;
  BDRV_CTRL2 = d->ctrl2;
  BDRV_CTRL1 = d->ctrl1;
}

/* ===========================================================================
 * Commutation from Hall state
 * ========================================================================= */

static void commutate_from_hall(uint8_t hall)
{
  signed char step = hall_to_step[hall & 0x07u];

  if (step < 0) {
    return;                      /* illegal code — caller handles it */
  }

#if MOTOR_REVERSE
  /* Reverse = advance three steps (180 electrical degrees) the other way */
  step = (signed char)((step + 3) % 6);
#endif

  bdrv_apply_step((uint8_t)step);
  g_commutations++;
}

/* ===========================================================================
 * main
 * ========================================================================= */

int main(void)
{
  uint32_t last_move_ms;

  wdt1_service();

  systick_init();
  hall_init();
  ccu6_init();          /* PWM running at 0% duty — safe */

  if (!bdrv_init()) {
    bdrv_all_off();
    for (;;) { wdt1_service(); }
  }

  /* Verify the Hall sensors before energizing anything. An illegal code here
   * means VDDEXT is dead, a sensor is unplugged, or a pull-up is missing —
   * all of which would otherwise cause a stuck, current-hogging step. */
  {
    uint8_t h = hall_read_stable();
    if ((h == 0u) || (h == 7u)) {
      bdrv_all_off();
      for (;;) { wdt1_service(); }   /* latch: fix the sensors first */
    }
    g_last_hall = h;
  }

  /* Energize the step matching the current rotor position, then raise duty.
   * With Hall feedback there is no blind ramp — the rotor is already where
   * the sensors say it is, so the first step produces correct torque. */
  commutate_from_hall(g_last_hall);
  ccu6_set_duty(DUTY_MAX_TICKS);
  delay_ms(KICKSTART_MS);

  last_move_ms = millis();

  for (;;) {
    const uint8_t hall = hall_read_stable();

    wdt1_service();

    if ((hall == 0u) || (hall == 7u)) {
      /* Sensor failed while running — stop immediately. */
      ccu6_set_duty(0);
      bdrv_all_off();
      for (;;) { wdt1_service(); }
    }

    if (hall != g_last_hall) {
      g_last_hall = hall;
      commutate_from_hall(hall);
      last_move_ms = millis();
    }

    /* Stall detection: no Hall transition for 1 s while energized means the
     * rotor is jammed. At 0.5 ohm a stalled winding is the worst thermal
     * case, so cut drive rather than cook it. */
    if ((millis() - last_move_ms) > 1000u) {
      ccu6_set_duty(0);
      bdrv_all_off();
      for (;;) { wdt1_service(); }
    }

    if (bdrv_faulted()) {
      ccu6_set_duty(0);
      bdrv_all_off();
      for (;;) { wdt1_service(); }
    }
  }
}
