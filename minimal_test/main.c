/* Ultra-minimal STM32N6 test with SAU/MPU disable */

#include <stdint.h>

/* Stack - placed at end of SRAM2 data region */
#define STACK_TOP       0x24144000

/* Diagnostic memory locations */
#define TEST_ADDR       (*(volatile uint32_t *)0x24140000)
#define CFSR_COPY       (*(volatile uint32_t *)0x24140004)
#define HFSR_COPY       (*(volatile uint32_t *)0x24140008)

/* Fault registers */
#define CFSR            (*(volatile uint32_t *)0xE000ED28)
#define HFSR            (*(volatile uint32_t *)0xE000ED2C)

/* MPU and SAU control */
#define MPU_CTRL        (*(volatile uint32_t *)0xE000ED94)
#define SAU_CTRL        (*(volatile uint32_t *)0xE000EDD0)

#define MAGIC_VALUE     0xDEADBEEF

/* RCC and peripheral addresses for STM32N6 */
#define RCC_AHB4ENR     (*(volatile uint32_t *)0x46028278)  /* AHB4 Enable Register */
#define RCC_APB2ENR     (*(volatile uint32_t *)0x4602826C)  /* APB2 Enable Register */
#define RCC_CCIPR13     (*(volatile uint32_t *)0x46028174)  /* USART1 kernel clock */
#define RCC_AHB4ENR_GPIOEEN  (1 << 4)  /* GPIOE enable bit */
#define RCC_AHB4ENR_PWREN    (1 << 18) /* PWR enable bit */
#define RCC_APB2ENR_USART1EN (1 << 4)  /* USART1 enable bit */

/* PWR registers for VddIO enable (non-secure addresses) */
#define PWR_BASE        0x46024800
#define PWR_SVMCR1      (*(volatile uint32_t *)(PWR_BASE + 0x34))  /* VDDIO4 */
#define PWR_SVMCR2      (*(volatile uint32_t *)(PWR_BASE + 0x38))  /* VDDIO5 */
#define PWR_SVMCR3      (*(volatile uint32_t *)(PWR_BASE + 0x3C))  /* VDDIO2, VDDIO3 */
#define PWR_SVMCR1_VDDIO4SV  (1 << 8)
#define PWR_SVMCR2_VDDIO5SV  (1 << 8)
#define PWR_SVMCR3_VDDIO2SV  (1 << 8)
#define PWR_SVMCR3_VDDIO3SV  (1 << 9)

/* GPIOE registers (for USART1 TX=PE5, RX=PE6) */
#define GPIOE_BASE      0x46021000
#define GPIOE_MODER     (*(volatile uint32_t *)(GPIOE_BASE + 0x00))
#define GPIOE_AFRL      (*(volatile uint32_t *)(GPIOE_BASE + 0x20))
#define GPIOE_AFRH      (*(volatile uint32_t *)(GPIOE_BASE + 0x24))

/* USART1 registers */
#define USART1_BASE     0x42001000
#define USART1_CR1      (*(volatile uint32_t *)(USART1_BASE + 0x00))
#define USART1_ISR      (*(volatile uint32_t *)(USART1_BASE + 0x1C))
#define USART1_TDR      (*(volatile uint32_t *)(USART1_BASE + 0x28))

/* Test results */
#define STAGE_MARKER    (*(volatile uint32_t *)0x2414000C)  /* Current stage */
#define USART_ISR_COPY  (*(volatile uint32_t *)0x24140010)  /* Copy of USART ISR */
#define GPIOE_MODER_COPY (*(volatile uint32_t *)0x24140014) /* Copy of GPIOE_MODER */
#define PWR_SVMCR3_COPY  (*(volatile uint32_t *)0x24140018) /* Copy of PWR_SVMCR3 */

/* Main function - test peripheral access */
void main(void)
{
    /* Stage 1: Basic execution */
    STAGE_MARKER = 0x11111111;
    TEST_ADDR = 0x11111111;

    /* Stage 2: Enable PWR clock and VddIO power domains */
    STAGE_MARKER = 0x22222222;
    RCC_AHB4ENR |= RCC_AHB4ENR_PWREN;  /* Enable PWR peripheral clock */
    __asm__ volatile ("dsb");
    __asm__ volatile ("isb");

    /* Enable VddIO2, VddIO3, VddIO4, VddIO5 - required for GPIO access! */
    PWR_SVMCR3 |= PWR_SVMCR3_VDDIO2SV | PWR_SVMCR3_VDDIO3SV;
    PWR_SVMCR1 |= PWR_SVMCR1_VDDIO4SV;
    PWR_SVMCR2 |= PWR_SVMCR2_VDDIO5SV;
    __asm__ volatile ("dsb");
    PWR_SVMCR3_COPY = PWR_SVMCR3;  /* Copy for debugging */
    TEST_ADDR = 0x22222222;

    /* Stage 2.5: Enable GPIOE clock */
    STAGE_MARKER = 0x22555555;
    RCC_AHB4ENR |= RCC_AHB4ENR_GPIOEEN;
    __asm__ volatile ("dsb");
    TEST_ADDR = 0x22555555;

    /* Stage 3: Configure USART1 kernel clock to use HSI (64 MHz) */
    STAGE_MARKER = 0x33333333;
    RCC_CCIPR13 = 3;  /* Select HSI as USART1 clock source */
    __asm__ volatile ("dsb");

    /* Enable USART1 APB clock */
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
    __asm__ volatile ("dsb");
    __asm__ volatile ("isb");

    /* Small delay for clock to stabilize */
    for (volatile int i = 0; i < 1000; i++);
    TEST_ADDR = 0x33333333;

    /* Stage 4: Try to read USART1 ISR register */
    STAGE_MARKER = 0x44444444;
    USART_ISR_COPY = USART1_ISR;
    TEST_ADDR = 0x44444444;

    /* Stage 5: Configure GPIO for USART1 (PE5=TX, PE6=RX, AF7) */
    STAGE_MARKER = 0x55555555;
    {
        uint32_t moder = GPIOE_MODER;
        moder &= ~((3 << (5*2)) | (3 << (6*2)));  /* Clear PE5, PE6 mode bits */
        moder |= (2 << (5*2)) | (2 << (6*2));     /* Set to Alternate Function mode */
        GPIOE_MODER = moder;

        uint32_t afrl = GPIOE_AFRL;
        afrl &= ~((0xF << (5*4)) | (0xF << (6*4)));  /* Clear AF bits for PE5, PE6 */
        afrl |= (7 << (5*4)) | (7 << (6*4));         /* Set AF7 for USART1 */
        GPIOE_AFRL = afrl;
        __asm__ volatile ("dsb");
    }
    GPIOE_MODER_COPY = GPIOE_MODER;  /* Copy for debugging - should show AF bits set */
    TEST_ADDR = 0x55555555;

    /* Stage 6: Configure USART1 for 115200 baud (assuming 64MHz clock) */
    STAGE_MARKER = 0x66666666;
    {
        /* Disable USART first */
        USART1_CR1 = 0;

        /* Set baud rate: BRR = fck / baud = 64000000 / 115200 = 555 = 0x22B */
        (*(volatile uint32_t *)(USART1_BASE + 0x0C)) = 555;  /* BRR register */

        /* Enable USART, TX, and RX */
        USART1_CR1 = (1 << 0) | (1 << 3) | (1 << 2);  /* UE, TE, RE */
    }
    TEST_ADDR = 0x66666666;

    /* Stage 7: Send test message */
    STAGE_MARKER = 0x77777777;
    {
        const char *msg = "Hello from STM32N6!\r\n";
        while (*msg) {
            /* Wait for TXE (TX empty) */
            while (!(USART1_ISR & (1 << 7)));
            /* Send character */
            USART1_TDR = *msg++;
        }
        /* Wait for TC (transmission complete) */
        while (!(USART1_ISR & (1 << 6)));
    }
    TEST_ADDR = MAGIC_VALUE;

    /* Simple infinite loop - keep outputting */
    while (1) {
        for (volatile int i = 0; i < 1000000; i++);  /* Delay */
        while (!(USART1_ISR & (1 << 7)));
        USART1_TDR = '.';
    }
}

/* Reset handler - written in assembly to avoid any issues */
__attribute__((naked))
void reset_handler(void)
{
    __asm__ volatile (
        /* Clear MSPLIM and PSPLIM */
        "MOV r0, #0\n"
        "MSR msplim, r0\n"
        "MSR psplim, r0\n"

        /* Disable SAU (SAU_CTRL = 0) */
        "LDR r1, =0xE000EDD0\n"
        "STR r0, [r1]\n"

        /* Disable MPU (MPU_CTRL = 0) */
        "LDR r1, =0xE000ED94\n"
        "STR r0, [r1]\n"

        /* Data and instruction sync barriers */
        "DSB\n"
        "ISB\n"

        /* Set stack pointer */
        "LDR r0, =0x24144000\n"
        "MSR msp, r0\n"

        /* Jump to main */
        "LDR r0, =main\n"
        "BX r0\n"
        ".ltorg\n"
    );
}

/* HardFault handler */
__attribute__((naked))
void hardfault_handler(void)
{
    __asm__ volatile (
        "LDR r0, =0x24140000\n"
        "LDR r1, =0x22222222\n"
        "STR r1, [r0]\n"
        "LDR r1, =0xE000ED28\n"  /* CFSR */
        "LDR r2, [r1]\n"
        "STR r2, [r0, #4]\n"
        "LDR r1, =0xE000ED2C\n"  /* HFSR */
        "LDR r2, [r1]\n"
        "STR r2, [r0, #8]\n"
        "1: B 1b\n"
        ".ltorg\n"
    );
}

/* Default handler */
__attribute__((naked))
void default_handler(void)
{
    __asm__ volatile (
        "LDR r0, =0x24140000\n"
        "LDR r1, =0xFFFFFFFF\n"
        "STR r1, [r0]\n"
        "1: B 1b\n"
        ".ltorg\n"
    );
}

/* Vector table */
__attribute__((section(".vectors")))
const void *vector_table[] = {
    (void *)STACK_TOP,          /* Initial stack pointer */
    reset_handler,              /* Reset handler */
    default_handler,            /* NMI */
    hardfault_handler,          /* HardFault */
    default_handler,            /* MemManage */
    default_handler,            /* BusFault */
    default_handler,            /* UsageFault */
    default_handler,            /* SecureFault */
    0, 0, 0,                    /* Reserved */
    default_handler,            /* SVCall */
    default_handler,            /* Debug */
    0,                          /* Reserved */
    default_handler,            /* PendSV */
    default_handler,            /* SysTick */
};
