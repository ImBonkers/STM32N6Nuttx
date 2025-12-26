# STM32N6 NuttX Port - Status Report

## Overview

This document tracks the progress of porting NuttX RTOS to the STM32N6 Nucleo-N657X0-Q board (Cortex-M55 @ 800MHz).

## Hardware Details

- **Board**: NUCLEO-N657X0-Q
- **MCU**: STM32N657X0 (Cortex-M55, ARMv8.1-M with TrustZone)
- **Memory**: No internal flash - code runs from external XSPI flash or SRAM
- **SRAM1 (AXI)**: 0x24000000
- **SRAM2**: 0x24100000 - 0x24140000 (256KB) - used for code execution
- **External Flash**: MX25UM51245G at 0x70000000 (memory-mapped at 0x34180000)

## Boot Modes

| Mode | BOOT0 (JP1) | BOOT1 (JP2) | Description |
|------|-------------|-------------|-------------|
| Normal Boot | 0 (pos 1) | 0 (pos 1) | Boot from external flash |
| DEV Boot | 0 (pos 1) | 1 (pos 2) | Development mode - for SWD programming |
| **Serial Boot** | **1 (pos 2)** | **0 (pos 1)** | **USB DFU mode - load to RAM via USB (RECOMMENDED)** |

### Serial Boot Mode (Recommended for Development)
- Set BOOT0=1 (JP1 pos 2), BOOT1=0 (JP2 pos 1)
- Connect both USB ports:
  - **ST-Link USB (CN10)**: Power and serial console (ttyACM0)
  - **USB CN8**: DFU firmware loading
- Firmware is loaded directly to RAM and executed
- **Not persistent** - lost on power cycle (re-flash to run again)
- No jumper changes needed between flash and run cycles

### External Flash Boot (Persistent)
- Program with BOOT0=0, BOOT1=1
- Run with BOOT0=0, BOOT1=0
- Requires power cycle and jumper change after programming

## Firmware Signing Requirement

**CRITICAL**: The STM32N6 ROM bootloader requires **signed firmware**.

Use ST's signing tool:
```bash
STM32_SigningTool_CLI -bin firmware.bin -nk -of 0x80000000 -t fsbl -o firmware.signed.bin
```

The signing tool is included with STM32CubeProgrammer at:
`/home/samuel/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_SigningTool_CLI`

---

## Current Status: FSBL Boots from External Flash, NuttX Loading In Progress

### Latest Progress (December 26, 2024)

**FSBL successfully boots from external flash and turns on blue LED.**

#### Critical Discovery: `-align` Flag Required for Signing

The STM32_SigningTool_CLI requires the `-align` flag when using header v2.3 for STM32N6:

```bash
STM32_SigningTool_CLI -bin firmware.bin -nk -of 0x80000000 -t fsbl \
  -ep <entry_point> -la 0x34180400 -o firmware_signed.bin -hv 2.3 -align
```

**The `-align` flag adds padding bytes to align the payload to the 0x400 offset, which the boot ROM expects. Without this flag, the boot ROM fails to load the FSBL.**

#### Working FSBL Build & Flash Commands

```bash
# Build FSBL
cd /home/samuel/Documents/GitHub/STM32N6Nuttx/SampleSTM32Project/Makefile/FSBL
make clean && make -j4

# Get entry point (add 1 for thumb bit)
arm-none-eabi-nm build/SampleProject_FSBL.elf | grep Reset_Handler
# Example: 341826b4 -> use 0x341826b5

# Sign with -align flag (CRITICAL!)
rm -f build/SampleProject_FSBL_signed.bin
/home/samuel/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_SigningTool_CLI \
  -bin build/SampleProject_FSBL.bin -nk -of 0x80000000 -t fsbl \
  -ep 0x341826b5 -la 0x34180400 \
  -o build/SampleProject_FSBL_signed.bin -hv 2.3 -align

# Flash to external flash (in DEV_MODE: BOOT1=HIGH)
/home/samuel/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI \
  -c port=SWD mode=HOTPLUG ap=1 \
  -el /home/samuel/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/ExternalLoader/MX25UM51245G_STM32N6570-NUCLEO.stldr \
  -w build/SampleProject_FSBL_signed.bin 0x70000000

# Flash NuttX binary
/home/samuel/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI \
  -c port=SWD mode=HOTPLUG ap=1 \
  -el /home/samuel/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/ExternalLoader/MX25UM51245G_STM32N6570-NUCLEO.stldr \
  -w /home/samuel/Documents/GitHub/STM32N6Nuttx/nuttx/nuttx.bin 0x70020000

# Switch to FLASH_MODE (BOOT1=LOW, BOOT0=LOW) to run
```

#### Memory Map for External Flash Boot

| Address | Content | Description |
|---------|---------|-------------|
| 0x70000000 | FSBL (signed) | First Stage Boot Loader with header |
| 0x70020000 | NuttX.bin | NuttX OS binary (raw, unsigned) |

#### FSBL Execution Flow

1. Boot ROM loads signed FSBL from 0x70000000 to AXISRAM2 (0x34180400)
2. FSBL initializes clocks and GPIO
3. FSBL turns on **blue LED** (confirms FSBL is running)
4. FSBL copies NuttX from 0x70020000 to 0x34000000 (256KB)
5. FSBL validates NuttX vector table (SP and Reset_Handler in valid range)
6. FSBL turns on **green LED** (confirms copy complete)
7. FSBL jumps to NuttX Reset_Handler
8. If validation fails: **red LED** = bad SP, **red+green LED** = bad reset handler

#### Current LED Status

| LED | Status | Meaning |
|-----|--------|---------|
| Blue | ON | FSBL started successfully |
| Green | ? | NuttX copy complete, about to jump |
| Red | OFF | No validation errors |

#### NuttX Memory Configuration

NuttX is linked to run from AXISRAM2 at 0x34000000:
- Initial SP: 0x340c2ae4
- Reset Handler: 0x34000489
- Text start: 0x34000000

---

## Previous Status: Zephyr Working, NuttX In Progress

### Zephyr Test Results (CONFIRMED WORKING)

Successfully built and ran Zephyr on the board:

```bash
# Setup
source zephyr-venv/bin/activate
export ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb
export GNUARMEMB_TOOLCHAIN_PATH=/usr
export PATH="/home/samuel/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin:$PATH"

# Build for serial boot mode
west build -p always -b nucleo_n657x0_q//sb zephyr/samples/hello_world

# Flash via USB DFU
west flash --runner stm32cubeprogrammer
```

**Output confirmed:**
```
*** Booting Zephyr OS build fde41e87929a ***
Hello World! nucleo_n657x0_q/stm32n657xx/sb
```

**Blinky sample also works** - LED blinks correctly, confirming GPIO and VddIO initialization.

---

## Key Initialization Sequence (from Zephyr)

The STM32N6 requires specific initialization before GPIO and peripherals work.

### Critical Discovery: VddIO Power Domains Must Be Enabled

**This was the root cause of GPIO not working in earlier attempts.**

From `zephyr/soc/st/stm32/stm32n6x/soc.c`:

### 1. SystemInit (called from reset handler)
```c
// Set vector table
SCB->VTOR = vector_table_address;

// Clear SAU regions (TrustZone security)
for (int i = 0; i < 8; i++) {
    SAU->RNR = i;
    SAU->RBAR = 0;
    SAU->RLAR = 0;
}

// Enable VddIO power domains (CRITICAL!)
PWR->SVMCR1 |= PWR_SVMCR1_VDDIO4SV;
PWR->SVMCR2 |= PWR_SVMCR2_VDDIO5SV;
PWR->SVMCR3 |= PWR_SVMCR3_VDDIO2SV | PWR_SVMCR3_VDDIO3SV;

// Enable FPU
SCB->CPACR |= ((3UL << 20U) | (3UL << 22U));
```

### 2. soc_early_init_hook (early kernel init)
```c
// Enable instruction and data caches
sys_cache_instr_enable();
sys_cache_data_enable();

// SystemCoreClock = 64MHz (HSI default after reset)
SystemCoreClock = 64000000;

// Enable PWR peripheral clock
LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_PWR);

// Set voltage scaling for best performance
LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE0);

// Enable VddIO supplies
LL_PWR_EnableVddIO2();
LL_PWR_EnableVddIO3();
LL_PWR_EnableVddIO4();
LL_PWR_EnableVddIO5();

// Set VddIO2 and VddIO3 to 1.8V
LL_PWR_SetVddIO2VoltageRange(LL_PWR_VDDIO_VOLTAGE_RANGE_1V8);
LL_PWR_SetVddIO3VoltageRange(LL_PWR_VDDIO_VOLTAGE_RANGE_1V8);
```

---

## Register Addresses

### PWR Registers (Non-Secure)
| Register | Address | Description |
|----------|---------|-------------|
| PWR_BASE_NS | 0x46024800 | PWR base address |
| PWR_SVMCR1 | PWR_BASE + 0x34 | VDDIO4 enable |
| PWR_SVMCR2 | PWR_BASE + 0x38 | VDDIO5 enable |
| PWR_SVMCR3 | PWR_BASE + 0x3C | VDDIO2, VDDIO3 enable |

### Key Bits
| Bit | Register | Description |
|-----|----------|-------------|
| bit 8 | PWR_SVMCR1 | VDDIO4SV - Enable VDDIO4 |
| bit 8 | PWR_SVMCR2 | VDDIO5SV - Enable VDDIO5 |
| bit 8 | PWR_SVMCR3 | VDDIO2SV - Enable VDDIO2 |
| bit 9 | PWR_SVMCR3 | VDDIO3SV - Enable VDDIO3 |

### RCC Registers (Non-Secure)
| Register | Address | Description |
|----------|---------|-------------|
| RCC_BASE_NS | 0x44020000 | RCC base address |
| RCC_AHB4ENR | RCC_BASE + 0x130 | GPIO and PWR clock enable |
| RCC_AHB4ENR_PWREN | bit 4 | PWR clock enable |

### GPIO (USART1 on PE5/PE6)
| Register | Address | Description |
|----------|---------|-------------|
| GPIOE_BASE | 0x42024000 | GPIOE base address |
| USART1_TX | PE5 | Alternate Function 7 |
| USART1_RX | PE6 | Alternate Function 7 |

### USART1 (Non-Secure)
| Register | Address | Description |
|----------|---------|-------------|
| USART1_BASE_NS | 0x42400000 | USART1 base address |

---

## Serial Console
- **USART1** on PE5 (TX) and PE6 (RX)
- Connected to ST-Link Virtual COM Port
- Settings: **115200 8N1**
- Device: `/dev/ttyACM0`

---

## Previous Issues and Solutions

### 1. GPIO Writes Had No Effect
**Symptom**: Writing to GPIO registers had no effect
**Cause**: VddIO power domains not enabled
**Solution**: Enable VDDIO2/3/4/5 via PWR_SVMCRx registers BEFORE accessing GPIO

### 2. OpenOCD Connection Failed
**Symptom**: PARTNO 0x0 unrecognized
**Solution**: Use STM32CubeProgrammer instead, with correct AP selection (ap=1)

### 3. Debug Execution Blocked
**Symptom**: Code loads but PC never advances, DFSR shows DWTTRAP
**Cause**: Security context from ROM bootloader blocks debug execution
**Solution**: Use signed firmware loaded via ROM bootloader (serial boot or external flash)

### 4. No Serial Output with hello_world
**Symptom**: picocom shows nothing
**Cause**: hello_world prints once at boot, output happens before terminal connects
**Solution**: Capture output during flash, or use blinky for visual confirmation

### 5. MSPLIM/PSPLIM Causes Stack Overflow
**Symptom**: Setting MSP causes immediate STKOF fault
**Cause**: Boot ROM sets MSPLIM to restrict stack range
**Solution**: Clear MSPLIM/PSPLIM early in startup code:
```c
asm volatile (
    "mov r0, #0\n\t"
    "msr msplim, r0\n\t"
    "msr psplim, r0\n\t"
);
```

---

## NuttX Porting Requirements

Based on Zephyr analysis, NuttX needs:

1. **Signed firmware** - Integrate STM32_SigningTool_CLI into build
2. **Early VddIO enable** - Before any GPIO access
3. **PWR clock enable** - RCC_AHB4ENR_PWREN before PWR register access
4. **USART1 driver** - PE5/PE6, AF7, 115200 baud
5. **Clear MSPLIM/PSPLIM** - First instructions in reset handler
6. **Set VTOR** - Point to NuttX vector table

---

## File Locations

| File | Description |
|------|-------------|
| `zephyr/soc/st/stm32/stm32n6x/soc.c` | Zephyr STM32N6 SOC init (reference) |
| `zephyr/boards/st/nucleo_n657x0_q/` | Zephyr board files |
| `STM32CubeN6/Drivers/CMSIS/Device/ST/STM32N6xx/Source/Templates/system_stm32n6xx_fsbl.c` | ST HAL SystemInit |
| `minimal_test/main.c` | Minimal test code |
| `nuttx/` | NuttX source |

---

## Tools Required

- **STM32CubeProgrammer v2.18.0 or later** - For signing and flashing
- **arm-none-eabi-gcc** - Cross compiler
- **Python 3 with west** - For Zephyr builds (reference)

---

## Quick Reference Commands

### Flash Zephyr (Serial Boot Mode)
```bash
source zephyr-venv/bin/activate
export ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb
export GNUARMEMB_TOOLCHAIN_PATH=/usr
export PATH="/home/samuel/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin:$PATH"

west build -p always -b nucleo_n657x0_q//sb zephyr/samples/basic/blinky
west flash --runner stm32cubeprogrammer
```

### Monitor Serial Output
```bash
picocom -b 115200 /dev/ttyACM0
```

### Check USB DFU Device
```bash
lsusb | grep -i stm
# Should show: STMicroelectronics STM Device in DFU Mode
```

---

*Last updated: December 2024*
