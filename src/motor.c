/*
 * Six-step (trapezoidal) open-loop BLDC drive — Infineon TLE9879-2QXA40
 *
 * Motor: 6 slots / 4 magnet poles  => 2 pole pairs
 *        R_phase-to-phase = 0.50 ohm (measured pole to pole)
 *
 * Because 6 slots / 4 poles gives 2 pole pairs:
 *   1 electrical revolution = 6 commutation steps
 *   1 mechanical revolution = 2 electrical revolutions = 12 commutation steps
 *
 * This is OPEN LOOP: the rotor is forced to follow the commutation table
 * with no position feedback. It is deliberately current-limited by duty
 * cap (see CURRENT SAFETY below) because 0.50 ohm pole-to-pole draws
 * enormous current if driven at high duty near standstill.
 *
 * Build:  make
 * Flash:  make flash
 *
 * ---------------------------------------------------------------------------
 * CURRENT SAFETY — READ BEFORE POWERING THE BRIDGE
 * ---------------------------------------------------------------------------
 * In six-step drive exactly two phases conduct, in series across the DC bus,
 * so the resistance the bus sees is the pole-to-pole value: 0.50 ohm.
 *
 * At standstill (or low speed) there is no meaningful back-EMF, so current is
 * limited only by resistance and duty:
 *
 *     I_avg  ~=  (V_bus * duty) / R_pp
 *
 * With V_bus = 12 V and R_pp = 0.50 ohm, 100% duty => 24 A. That will destroy
 * the bridge, the supply, or the motor. The duty cap below is derived from
 * MOTOR_I_MAX_MA so the stall current stays bounded:
 *
 *     duty_max = (I_max * R_pp) / V_bus
 *
 * At 12 V / 0.50 ohm / 3.0 A limit that is 12.5% duty. Raise MOTOR_I_MAX_MA
 * only after measuring actual current with a meter or shunt. Once the motor is
 * spinning, back-EMF opposes the bus and real current falls well below this
 * estimate, so this cap is conservative at speed and correct at standstill.
 *
 * This file drives the gate driver directly. It does NOT close a current loop.
 * Add CSA/ADC2 overcurrent shutdown before running anything but a bench test.
 * ---------------------------------------------------------------------------
 */

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef signed int         int32_t;

/* ===========================================================================
 * Motor and supply parameters — EDIT THESE TO MATCH YOUR SETUP
 * ========================================================================= */

/* Measured winding resistance, pole to pole, in milliohms. 0.50 ohm = 500 mOhm */
#define MOTOR_R_PP_MOHM      500u

/* DC bus voltage in millivolts. Measure it; do not assume the label value. */
#define MOTOR_VBUS_MV        12000u

/* Hard current ceiling in milliamps. Start LOW. This sets the duty cap. */
#define MOTOR_I_MAX_MA       3000u

/* 6 slots / 4 poles => 2 pole pairs */
#define MOTOR_POLE_PAIRS     2u

/* Commutation steps per electrical revolution (six-step is always 6) */
#define STEPS_PER_EREV       6u

/* Open-loop ramp: align, then accelerate to the target speed */
#define ALIGN_MS             300u     /* hold step 0 to park the rotor      */
#define RAMP_START_ERPM      120u     /* first electrical speed after align */
#define RAMP_END_ERPM        1800u    /* final open-loop electrical speed   */
#define RAMP_MS              1500u    /* time spent accelerating            */

/* ===========================================================================
 * Clock / PWM configuration
 * ========================================================================= */

#define F_CPU_HZ             40000000UL   /* 40 MHz core after SystemInit */
#define PWM_FREQ_HZ          20000UL      /* 20 kHz — above audible range */

/* CCU6 T12 counts up to period then reloads; period reg is (ticks - 1) */
#define PWM_PERIOD_TICKS     ((uint16_t)(F_CPU_HZ / PWM_FREQ_HZ))  /* 2000 */

/* Dead time in T12 ticks. 40 MHz => 25 ns/tick. 40 ticks = 1.0 us.
 * Must exceed the gate driver's turn-off delay or the bridge shoot-throughs. */
#define DEAD_TIME_TICKS      40u

/* ===========================================================================
 * Derived duty cap — see CURRENT SAFETY above
 * ========================================================================= */

/* duty_max_permille = 1000 * I_max * R_pp / V_bus, all in consistent units.
 * I_max[mA] * R_pp[mOhm] = microvolts; divide by V_bus[mV]*1000 for a ratio. */
/* Kept cast-free so it is usable in #if preprocessor checks below. */
#define DUTY_MAX_PERMILLE \
  (((MOTOR_I_MAX_MA * MOTOR_R_PP_MOHM) / (MOTOR_VBUS_MV)))

#define DUTY_MAX_TICKS \
  ((uint16_t)(((uint32_t)PWM_PERIOD_TICKS * (uint32_t)DUTY_MAX_PERMILLE) / 1000u))

/* Fail the build if the parameters would let the bridge cook itself. */
#if (DUTY_MAX_PERMILLE == 0)
#error "Duty cap computed as 0 — check MOTOR_I_MAX_MA / R_PP / VBUS."
#endif
#if (DUTY_MAX_PERMILLE > 500)
#error "Duty cap >50% with 0.5 ohm pole-to-pole. Lower MOTOR_I_MAX_MA."
#endif

/* ===========================================================================
 * Register map — TLE987x. Values from the TLE987x user manual / SVD.
 * ========================================================================= */

/* --- SCU / power management ------------------------------------------- */
#define SCUPM_BASE           0x50006000UL
#define SCUPM_WDT1_TRIG      (*(volatile uint32_t *)(SCUPM_BASE + 0x34UL))
#define WDT1_TRIG_VALUE      0x3FU
#define WDT1_SOW_ONE         (1u << 6)

/* --- Port ------------------------------------------------------------- */
#define PORT_BASE            0x48028000UL
#define PORT_P0_DATA         (*(volatile uint8_t  *)(PORT_BASE + 0x00UL))
#define PORT_P0_DIR          (*(volatile uint8_t  *)(PORT_BASE + 0x04UL))

/* --- CCU6 timer / capture-compare unit --------------------------------- */
#define CCU6_BASE            0x40010000UL
#define CCU6_CC60SR          (*(volatile uint16_t *)(CCU6_BASE + 0x00UL))
#define CCU6_CC61SR          (*(volatile uint16_t *)(CCU6_BASE + 0x04UL))
#define CCU6_CC62SR          (*(volatile uint16_t *)(CCU6_BASE + 0x08UL))
#define CCU6_T12PR           (*(volatile uint16_t *)(CCU6_BASE + 0x24UL))
#define CCU6_T12DTC          (*(volatile uint16_t *)(CCU6_BASE + 0x2CUL))
#define CCU6_TCTR0           (*(volatile uint16_t *)(CCU6_BASE + 0x30UL))
#define CCU6_TCTR2           (*(volatile uint16_t *)(CCU6_BASE + 0x34UL))
#define CCU6_TCTR4           (*(volatile uint16_t *)(CCU6_BASE + 0x38UL))
#define CCU6_MODCTR          (*(volatile uint16_t *)(CCU6_BASE + 0x40UL))
#define CCU6_TRPCTR          (*(volatile uint16_t *)(CCU6_BASE + 0x44UL))
#define CCU6_PSLR            (*(volatile uint16_t *)(CCU6_BASE + 0x48UL))
#define CCU6_MCMOUTS         (*(volatile uint16_t *)(CCU6_BASE + 0x50UL))
#define CCU6_MCMCTR          (*(volatile uint16_t *)(CCU6_BASE + 0x54UL))
#define CCU6_ISS             (*(volatile uint16_t *)(CCU6_BASE + 0x74UL))

/* TCTR4 bits */
#define TCTR4_T12RR          (1u << 1)   /* T12 run reset      */
#define TCTR4_T12RS          (1u << 2)   /* T12 run set        */
#define TCTR4_T12RES         (1u << 1)
#define TCTR4_DTRES          (1u << 3)   /* dead-time reset    */
#define TCTR4_T12STR         (1u << 6)   /* shadow transfer    */
#define TCTR4_T12STD         (1u << 5)

/* MODCTR: enable modulation on the six bridge outputs CC60..CC62 + inverted */
#define MODCTR_T12MODEN_ALL  0x003Fu

/* MCMOUTS shadow-transfer request */
#define MCMOUTS_STRMCM       (1u << 15)

/* --- BDRV: 3-phase MOSFET gate driver ---------------------------------- */
#define BDRV_BASE            0x50005000UL
#define BDRV_CFG             (*(volatile uint32_t *)(BDRV_BASE + 0x00UL))
#define BDRV_IST             (*(volatile uint32_t *)(BDRV_BASE + 0x04UL))
#define BDRV_ISCLR           (*(volatile uint32_t *)(BDRV_BASE + 0x0CUL))
#define BDRV_TRIM            (*(volatile uint32_t *)(BDRV_BASE + 0x10UL))

/* BDRV_CFG: per-driver enable bits (2 bits each: off / on) */
#define BDRV_HS1_EN          (1u << 0)
#define BDRV_LS1_EN          (1u << 2)
#define BDRV_HS2_EN          (1u << 4)
#define BDRV_LS2_EN          (1u << 6)
#define BDRV_HS3_EN          (1u << 8)
#define BDRV_LS3_EN          (1u << 10)
#define BDRV_ALL_EN          (BDRV_HS1_EN | BDRV_LS1_EN | \
                              BDRV_HS2_EN | BDRV_LS2_EN | \
                              BDRV_HS3_EN | BDRV_LS3_EN)

/* Charge pump enable lives in the SCUPM power-mode control */
#define SCUPM_BDRV_CTRL      (*(volatile uint32_t *)(SCUPM_BASE + 0x40UL))
#define BDRV_CP_EN           (1u << 0)

/* Drain-source / overcurrent status bits we must see clear before enabling */
#define BDRV_IST_FAULT_MASK  0x00003FFFUL

/* ===========================================================================
 * Commutation table — six-step, 120 degree conduction
 * ===========================================================================
 * Each step energizes one high side and one low side on a DIFFERENT phase.
 * The third phase floats (both switches off) so back-EMF can be sensed there
 * if you later add sensorless detection.
 *
 * MCMOUTS layout (CCU6 multi-channel mode), bits 5..0 = CC62H CC62L CC61H
 * CC61L CC60H CC60L, where phase 1 = CC60, phase 2 = CC61, phase 3 = CC62.
 *
 * The high-side bit is PWM-modulated (via MODCTR); the low-side bit is held
 * on for the whole step. That is "PWM on high, ON on low" complementary-free
 * six-step, which avoids shoot-through by construction: the two conducting
 * switches are never on the same half-bridge.
 */

#define OUT_P1L (1u << 0)
#define OUT_P1H (1u << 1)
#define OUT_P2L (1u << 2)
#define OUT_P2H (1u << 3)
#define OUT_P3L (1u << 4)
#define OUT_P3H (1u << 5)

/* Step: phase pair driven.  0: A->B  1: A->C  2: B->C  3: B->A  4: C->A  5: C->B */
static const uint16_t commutation_fwd[STEPS_PER_EREV] = {
  (uint16_t)(OUT_P1H | OUT_P2L),   /* 0: current A -> B */
  (uint16_t)(OUT_P1H | OUT_P3L),   /* 1: current A -> C */
  (uint16_t)(OUT_P2H | OUT_P3L),   /* 2: current B -> C */
  (uint16_t)(OUT_P2H | OUT_P1L),   /* 3: current B -> A */
  (uint16_t)(OUT_P3H | OUT_P1L),   /* 4: current C -> A */
  (uint16_t)(OUT_P3H | OUT_P2L),   /* 5: current C -> B */
};

/* ===========================================================================
 * State
 * ========================================================================= */

static volatile uint32_t g_systick_ms;   /* incremented by SysTick_Handler */
static uint8_t           g_step;         /* 0..5, current commutation step */

/* ===========================================================================
 * Watchdog — WDT1 resets the SoC if it is not serviced
 * ========================================================================= */

static void wdt1_service(void)
{
  SCUPM_WDT1_TRIG = WDT1_SOW_ONE;
  SCUPM_WDT1_TRIG = WDT1_TRIG_VALUE;
}

/* ===========================================================================
 * SysTick — 1 ms time base
 * ========================================================================= */

#define SYST_CSR   (*(volatile uint32_t *)0xE000E010UL)
#define SYST_RVR   (*(volatile uint32_t *)0xE000E014UL)
#define SYST_CVR   (*(volatile uint32_t *)0xE000E018UL)

void SysTick_Handler(void);   /* overrides the weak startup symbol */

void SysTick_Handler(void)
{
  g_systick_ms++;
}

static void systick_init(void)
{
  SYST_RVR = (F_CPU_HZ / 1000UL) - 1UL;
  SYST_CVR = 0;
  SYST_CSR = (1u << 2) | (1u << 1) | (1u << 0);  /* core clk, IRQ, enable */
}

static uint32_t millis(void)
{
  return g_systick_ms;
}

/* Busy-wait that keeps the watchdog fed. */
static void delay_ms(uint32_t ms)
{
  const uint32_t start = millis();
  while ((millis() - start) < ms) {
    wdt1_service();
  }
}

/* ===========================================================================
 * CCU6 PWM init — T12 in centre-aligned mode with hardware dead time
 * ========================================================================= */

static void ccu6_init(void)
{
  /* Stop the timer while reconfiguring */
  CCU6_TCTR4 = TCTR4_T12RR;

  /* T12 period. Centre-aligned halves the effective frequency, so the
   * period register is set for the full up-down cycle. */
  CCU6_T12PR = (uint16_t)(PWM_PERIOD_TICKS - 1u);

  /* Start with 0% duty on all three channels — bridge makes no current. */
  CCU6_CC60SR = 0;
  CCU6_CC61SR = 0;
  CCU6_CC62SR = 0;

  /* Hardware dead time on all three channels. This is the shoot-through
   * guard: it delays each turn-on, it does not delay turn-off. */
  CCU6_T12DTC = (uint16_t)((DEAD_TIME_TICKS & 0xFFu) | (0x07u << 8));

  /* Centre-aligned mode, T12 counts up then down */
  CCU6_TCTR0 = (uint16_t)(0x0000u);

  /* Passive level: outputs idle low so the bridge is off when duty is 0. */
  CCU6_PSLR = 0x0000u;

  /* Multi-channel mode: MCMOUTS drives which switches may conduct,
   * MODCTR gates the PWM onto them. */
  CCU6_MCMCTR = 0x0000u;
  CCU6_MODCTR = MODCTR_T12MODEN_ALL;

  /* No external trap source wired on the eval kit — clear trap control.
   * If your board routes an overcurrent comparator to CTRAP, enable it here
   * so hardware kills the bridge without software in the loop. */
  CCU6_TRPCTR = 0x0000u;

  /* Latch shadow registers, then run T12 */
  CCU6_TCTR4 = (uint16_t)(TCTR4_T12STR | TCTR4_DTRES);
  CCU6_TCTR4 = TCTR4_T12RS;
}

/* Apply duty (in T12 ticks) to all three compare channels.
 * Only the channels selected by MCMOUTS actually reach the gates. */
static void ccu6_set_duty(uint16_t ticks)
{
  if (ticks > DUTY_MAX_TICKS) {
    ticks = DUTY_MAX_TICKS;   /* hard clamp — never exceed the current cap */
  }

  CCU6_CC60SR = ticks;
  CCU6_CC61SR = ticks;
  CCU6_CC62SR = ticks;

  CCU6_TCTR4 = TCTR4_T12STR;  /* shadow transfer on next period boundary */
}

/* Select which switches conduct for this commutation step. */
static void ccu6_set_outputs(uint16_t pattern)
{
  CCU6_MCMOUTS = (uint16_t)(pattern | MCMOUTS_STRMCM);
}

/* Turn every bridge switch off without stopping the timer. */
static void ccu6_all_off(void)
{
  CCU6_CC60SR = 0;
  CCU6_CC61SR = 0;
  CCU6_CC62SR = 0;
  CCU6_TCTR4  = TCTR4_T12STR;
  ccu6_set_outputs(0);
}

/* ===========================================================================
 * BDRV — gate driver. Enabled LAST, after PWM is known-safe at 0% duty.
 * ========================================================================= */

static int bdrv_init(void)
{
  /* Charge pump first — high-side gates need it before they can turn on. */
  SCUPM_BDRV_CTRL |= BDRV_CP_EN;
  delay_ms(5);   /* let the pump reach voltage */

  /* Clear any latched fault from a previous run. */
  BDRV_ISCLR = BDRV_IST_FAULT_MASK;

  /* Refuse to enable if faults are still asserted (short, UV, overtemp). */
  if ((BDRV_IST & BDRV_IST_FAULT_MASK) != 0UL) {
    return 0;
  }

  /* Enable all six drivers. PWM is still at 0% duty, so no current flows. */
  BDRV_CFG |= BDRV_ALL_EN;

  return 1;
}

static void bdrv_disable(void)
{
  BDRV_CFG &= ~(uint32_t)BDRV_ALL_EN;
  SCUPM_BDRV_CTRL &= ~(uint32_t)BDRV_CP_EN;
}

static int bdrv_faulted(void)
{
  return (BDRV_IST & BDRV_IST_FAULT_MASK) != 0UL;
}

/* ===========================================================================
 * Commutation timing
 * ===========================================================================
 * Electrical RPM -> microseconds per commutation step.
 *
 *   erev/s          = erpm / 60
 *   steps/s         = erev/s * 6
 *   us per step     = 1e6 / steps/s = 1e7 / erpm      (since 1e6*60/6 = 1e7)
 *
 * Mechanical speed is erpm / POLE_PAIRS, i.e. half the electrical speed for
 * this 6-slot / 4-pole motor.
 */

static uint32_t erpm_to_step_us(uint32_t erpm)
{
  if (erpm == 0u) {
    return 100000u;
  }
  return 10000000UL / erpm;
}

/* Microsecond-ish busy delay derived from the core clock.
 * Coarse by design: open-loop commutation tolerates a few percent jitter. */
static void delay_us(uint32_t us)
{
  /* ~3 core cycles per loop iteration at -Os on Cortex-M3 */
  volatile uint32_t n = (us * (F_CPU_HZ / 1000000UL)) / 3UL;
  while (n--) {
    __asm volatile("nop");
  }
}

/* Advance to the next commutation step in the running direction. */
static void commutate_next(void)
{
  g_step++;
  if (g_step >= STEPS_PER_EREV) {
    g_step = 0u;
  }
  ccu6_set_outputs(commutation_fwd[g_step]);
}

/* ===========================================================================
 * Startup sequence: align -> ramp -> run
 * ========================================================================= */

/* Park the rotor at a known electrical angle so the first commutation
 * produces torque in the intended direction instead of stalling or kicking
 * backwards. Uses reduced duty — the rotor is not moving, so this is the
 * worst case for current. */
static void motor_align(void)
{
  g_step = 0u;
  ccu6_set_outputs(commutation_fwd[g_step]);

  /* Half the cap during align: rotor is stationary, no back-EMF at all. */
  ccu6_set_duty((uint16_t)(DUTY_MAX_TICKS / 2u));
  delay_ms(ALIGN_MS);
}

/* Open-loop acceleration. Steps the commutation faster and faster over
 * RAMP_MS. If the rotor cannot keep up it will slip and stall — lower
 * RAMP_END_ERPM or lengthen RAMP_MS if that happens. */
static void motor_ramp(void)
{
  const uint32_t t_start = millis();
  uint32_t elapsed = 0u;

  ccu6_set_duty(DUTY_MAX_TICKS);

  while (elapsed < RAMP_MS) {
    uint32_t erpm;

    /* Linear interpolation from RAMP_START_ERPM to RAMP_END_ERPM */
    erpm = RAMP_START_ERPM +
           (((RAMP_END_ERPM - RAMP_START_ERPM) * elapsed) / RAMP_MS);

    commutate_next();
    delay_us(erpm_to_step_us(erpm));

    wdt1_service();

    if (bdrv_faulted()) {
      return;   /* caller re-checks and shuts down */
    }

    elapsed = millis() - t_start;
  }
}

/* ===========================================================================
 * main
 * ===========================================================================
 * Init order follows the project rule: clocks -> GPIO -> peripherals ->
 * interrupts -> motor enable LAST.
 */

int main(void)
{
  uint32_t step_us;

  wdt1_service();

  /* Time base before anything that delays */
  systick_init();

  /* PWM configured and running at 0% duty — safe, no bridge current */
  ccu6_init();

  /* Gate driver last, and only if it reports no faults */
  if (!bdrv_init()) {
    /* Bridge refused to come up. Stay dark and keep the watchdog happy
     * rather than forcing current into a faulted stage. */
    ccu6_all_off();
    for (;;) {
      wdt1_service();
    }
  }

  /* Rotor to a known angle, then spin it up open loop */
  motor_align();
  motor_ramp();

  /* Steady-state run at the final open-loop speed */
  step_us = erpm_to_step_us(RAMP_END_ERPM);
  ccu6_set_duty(DUTY_MAX_TICKS);

  for (;;) {
    commutate_next();
    delay_us(step_us);

    wdt1_service();

    if (bdrv_faulted()) {
      /* Overcurrent / short / undervoltage: kill the bridge and latch off.
       * Deliberately not auto-retrying — a fault at 0.5 ohm means something
       * is wrong that retrying will make worse. */
      ccu6_all_off();
      bdrv_disable();
      for (;;) {
        wdt1_service();
      }
    }
  }
}
