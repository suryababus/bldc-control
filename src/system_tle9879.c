typedef unsigned int uint32_t;

/* SCB VTOR — point vectors at program flash base */
#define SCB_VTOR (*(volatile uint32_t *)0xE000ED08UL)
#define PROG_FLASH_START 0x11000000UL

/*
 * BootROM NAC/NAD word (IROM2 @ 0x1101EFFC).
 * Value from Infineon BSL defaults used by TLE987x examples.
 */
const uint32_t g_nac_nad __attribute__((section(".nac_nad"), used)) = 0xFE01BA45UL;

void SystemInit(void)
{
  SCB_VTOR = PROG_FLASH_START;
}
