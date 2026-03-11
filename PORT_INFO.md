# Porting NuttX to STM32N6 (NUCLEO-N657X0-Q)

## Overview

This document covers the porting of Apache NuttX RTOS to the STMicroelectronics STM32N6 microcontroller, targeting the NUCLEO-N657X0-Q development board. The port does **not** require Helium (MVE) or vector extensions — we are treating the Cortex-M55 as a scalar ARMv8.1-M core only.

---

## Target Hardware

### STM32N6 MCU (STM32N657X0H3Q)

- **Core**: Arm Cortex-M55, ARMv8.1-M Mainline architecture
- **Max frequency**: 800 MHz (overdrive mode), 600 MHz nominal
- **RAM**: 4.2 MB contiguous embedded SRAM (AXI-SRAM), **no internal flash**
- **External memory interfaces**: Hexa-SPI, Octo-SPI (XSPI1, XSPI2), FMC (PSRAM, SDRAM, NOR, NAND)
- **TrustZone**: Optional, ARMv8-M TrustZone support
- **Caches**: Optional I-cache and D-cache (up to 64 KB each)
- **TCM**: ITCM (up to 16 MB), DTCM (up to 16 MB)
- **Bus interfaces**: AMBA5 AXI5 + AHB5 + APB
- **NPU**: ST Neural-ART Accelerator (present on N6x7 variants, absent on N6x5)
- **GPU**: NeoChrom 2.5D, Chrom-ART (DMA2D)
- **Video**: H.264 encoder (720p/1080p @ 30fps), JPEG codec
- **Camera**: Parallel + 2-lane MIPI CSI-2, dedicated ISP (up to 5MP @ 30fps)
- **Peripherals**: Up to 165 GPIOs, 2× USB 2.0 FS OTG (one with UCPD), Gigabit Ethernet with TSN, 4× I2C Fm+, 2× I3C, CAN-FD, SAI, etc.
- **Security**: Target SESIP level 3 / PSA level 3
- **Package**: VFBGA264

### NUCLEO-N657X0-Q Board

- **MCU**: STM32N657X0H3Q
- **Debugger**: Integrated STLINK-V3EC (Virtual COM port + debug)
- **USB connectors**: CN10 (ST-LINK), CN8 (user USB)
- **External flash**: Octo-SPI NOR flash connected via XSPI2 (Macronix)
- **Expansion**: Arduino-compatible + ST Morpho headers
- **Power**: External or internal SMPS for Vcore

---

## Architecture Context

### ARMv8.1-M vs ARMv8-M (what changed from Cortex-M33)

The Cortex-M55 implements ARMv8.1-M Mainline. The Cortex-M33 implements ARMv8-M Mainline. Key differences relevant to this port (excluding Helium/MVE which we skip):

| Feature | Cortex-M33 (ARMv8-M) | Cortex-M55 (ARMv8.1-M) |
|---|---|---|
| ISA | ARMv8-M Mainline | ARMv8.1-M Mainline |
| Helium (MVE) | No | Yes (optional, **we disable**) |
| FPU | SP only (optional) | HP + SP + DP (optional) |
| Low-Overhead Branch (LOB) | No | Yes |
| PACBTI | No | Yes (optional, ignore for now) |
| I-Cache / D-Cache | No | Yes (optional, up to 64 KB each) |
| TCM | No | Yes (ITCM + DTCM) |
| Bus interface | AHB5 + APB | AXI5 + AHB5 + APB |
| Pipeline | 3-stage | 4-stage (5 with FPU/Helium) |
| TrustZone | Yes (optional) | Yes (optional) |
| Custom Instructions (CDE) | Yes (optional) | Yes (optional) |

### Compiler flags

When compiling without Helium:

```bash
# GCC
-mcpu=cortex-m55+nomve -mthumb -mfloat-abi=hard -mfpu=fpv5-d16

# Or architecture-level:
-march=armv8.1-m.main+nofp+nodsp  # if also disabling FPU/DSP
```

The `+nomve` flag tells the compiler not to generate any Helium/MVE instructions. Without this flag, GCC defaults to assuming Helium is available when targeting Cortex-M55.

---

## NuttX Current State

### Existing support to build on

- **ARMv8-M Mainline**: NuttX has working support under `arch/arm/src/armv8-m/` used by Cortex-M33 chips
- **STM32H5**: The most recent Cortex-M33 STM32 port (`arch/arm/src/stm32h5/`), adapted from STM32 and STM32H7 ports. Good structural template.
- **STM32H7**: Cortex-M7 port with cache management code (useful reference for I-cache/D-cache handling, though ARMv8-M cache management differs from ARMv7-M)

### What does NOT exist yet

- No ARMv8.1-M architecture support (`armv81m` directory)
- No STM32N6 chip support
- No NUCLEO-N657X0-Q board support

---

## Boot Process (Critical — STM32N6 is flashless)

The STM32N6 has **no internal flash**. The boot process is fundamentally different from typical STM32 MCUs.

### Boot ROM

The boot ROM is hardcoded in the Cortex-M55 ROM and is the first code to execute on power-on or reset. It:

1. Initializes the system
2. Checks reset source
3. Reads BOOT0/BOOT1 pin states (latched at reset)
4. Selects boot device (external flash, SD card, eMMC, USB, UART)
5. Performs secure boot authentication (if configured)
6. Loads the FSBL into internal SRAM

### DEV Boot Mode (use this for development)

- Activated by setting **BOOT1 switch to position 2-3** on the Nucleo board
- Allows the ST-LINK debugger to load code directly into SRAM and execute it
- No FSBL or external flash required
- **This is how we bring up NuttX initially**

### FSBL + Application (for standalone boot, later)

For production/standalone operation:

1. Boot ROM copies FSBL from external flash into internal SRAM
2. FSBL initializes clocks, configures XSPI, enables memory-mapped mode
3. FSBL either:
   - **Load-and-Run**: Copies application from external flash to SRAM, jumps to it
   - **XIP (Execute-in-Place)**: Jumps directly to application in memory-mapped external flash

ST provides FSBL templates in the STM32CubeN6 package under:
```
STM32Cube_FW_N6_V1.0.0/Projects/STM32N6570-DK/Templates/Template_FSBL_LRUN
STM32CubeN6/Projects/NUCLEO-N657X0-Q/Examples/
```

**Do not worry about standalone boot until NSH is running in DEV mode.**

---

## Memory Map

Key memory regions (verify against RM0486 reference manual):

| Region | Address Range | Description |
|---|---|---|
| AXI-SRAM | 0x3400_0000 – ~0x3442_0000 | 4.2 MB contiguous SRAM |
| XSPI1 memory-mapped | 0x7000_0000 | External memory via XSPI1 |
| XSPI2 memory-mapped | 0x9000_0000 | External memory via XSPI2 (NOR flash on Nucleo) |
| Peripheral base | 0x4000_0000+ | APB/AHB peripherals |
| PPB (Private Peripheral Bus) | 0xE000_0000 | NVIC, SysTick, SCB, MPU, etc. |

**Note**: The boot ROM reserves some SRAM at the beginning. Do not place the NuttX vector table at address 0x34000000. Check the reference manual for exact reserved regions. A safe starting offset is typically 0x400 or more past the SRAM base.

### Initial linker script (DEV boot mode, all-in-SRAM)

```ld
MEMORY
{
    sram (rwx) : ORIGIN = 0x34000400, LENGTH = 4M
}
```

Adjust `ORIGIN` and `LENGTH` after confirming the exact SRAM layout from RM0486.

---

## Porting Structure

### Directory layout to create

```
arch/arm/
├── include/
│   ├── armv81m/          # New: ARMv8.1-M architecture headers
│   └── stm32n6/          # New: STM32N6 chip headers
├── src/
│   ├── armv81m/          # New: ARMv8.1-M architecture code
│   └── stm32n6/          # New: STM32N6 chip-level drivers

boards/arm/stm32n6/
└── nucleo-n657x0-q/     # New: Board support
    ├── include/
    │   └── board.h
    ├── scripts/
    │   └── flash.ld      # Linker script
    ├── src/
    │   ├── Makefile
    │   ├── stm32_boot.c
    │   ├── stm32_bringup.c
    │   └── ...
    ├── configs/
    │   └── nsh/           # NSH defconfig
    └── Kconfig
```

### Layer 1: Architecture (armv81m)

Start by copying `arch/arm/src/armv8-m/` to `arch/arm/src/armv81m/`. The scalar behavior is nearly identical. Changes needed:

1. **Cache management**: Add I-cache and D-cache enable/disable/invalidate/clean functions. The ARMv8.1-M cache uses SCB registers (`SCB->CCR` bits IC and DC). The Cortex-M55 supports hardware-assisted cache invalidation at reset — caches can be enabled by simply setting DC/IC bits in CCR without explicit software invalidation.

2. **LOB (Low-Overhead Branch) enable**: Set the LOB bit in the CCR register early in boot. This enables the loop/branch info cache for zero-overhead loops:
   ```c
   SCB->CCR |= SCB_CCR_LOB_Msk;
   __ISB();
   ```

3. **FPU context**: If using FPU (without MVE), the lazy stacking and context switch code from ARMv8-M should work. The FP registers are the same S0-S31 / D0-D15 when MVE is disabled. No vector register (Q0-Q7) context save needed since we skip Helium.

4. **NVIC, SysTick, MPU, SAU**: These are the same as ARMv8-M. The NVIC supports up to 480 interrupts on M33/M55. No changes needed.

5. **Exception entry/exit**: Same as ARMv8-M. The EXC_RETURN values and stack frame layout are identical when Helium is disabled.

### Layer 2: Chip (stm32n6)

Create `arch/arm/src/stm32n6/` using `arch/arm/src/stm32h5/` as a structural template. The STM32H5 is also Cortex-M33 / ARMv8-M and is the closest existing NuttX port.

**Minimum peripherals for NSH console:**

1. **RCC (Reset and Clock Control)**
   - Start at a low, safe clock speed (64 MHz CPU)
   - Do NOT attempt overdrive mode (800 MHz) initially
   - Overdrive requires: power regulator voltage scale 0, EXT_SMPS_MODE (PB12) configuration
   - Configure PLL4 for XSPI2 clock if needed later
   - Reference: STM32CubeN6 HAL `system_stm32n6xx.c` and CubeMX-generated clock configs

2. **GPIO**
   - Pin mux for UART TX/RX to the VCP (check board schematics / Zephyr DTS for exact pins)
   - Pin for user LED (green LED on PO1, active HIGH) — useful for debug

3. **USART / LPUART**
   - Whichever UART instance connects to the STLINK-V3EC VCP
   - Check the Nucleo schematics or Zephyr board DTS to identify the instance and pins
   - Standard 115200 8N1

4. **SysTick timer**
   - Standard Cortex-M SysTick, no changes from ARMv8-M

**Additional peripherals (add incrementally after NSH works):**

- DMA
- SPI / I2C / I3C
- XSPI (for external flash access)
- USB
- Ethernet (Gigabit with TSN)
- CAN-FD
- Timers / PWM
- ADC / DAC

### Layer 3: Board (nucleo-n657x0-q)

Standard NuttX board support:

- `board.h`: Clock frequencies, LED definitions, button definitions, UART pin configs
- `stm32_boot.c`: Early board initialization (called from `__start`)
- `stm32_bringup.c`: Late initialization (mount filesystems, register drivers, etc.)
- `defconfig` (nsh): Minimal configuration to get NSH shell running

---

## Key References

### ST Documentation

- **RM0486**: STM32N647/657xx Reference Manual (register-level detail for all peripherals)
- **UM3417**: STM32N6 Nucleo-144 board user manual (board schematics, jumper settings, VCP wiring)
- **UM3234**: How to proceed with boot ROM on STM32N6 MCUs
- **DS14740**: STM32N657X0 Datasheet
- **STM32CubeN6**: HAL drivers, examples, FSBL templates — get from ST website or GitHub (`STMicroelectronics/STM32CubeN6`)

### Zephyr Reference (already supports this board)

The Zephyr RTOS has a working port for `nucleo_n657x0_q/stm32n657xx`. This is extremely useful for:

- **Clock tree configuration** (exact PLL/prescaler values)
- **UART pin assignments** for the VCP
- **Linker script addresses** (confirmed SRAM regions)
- **Device tree** (pin mux, peripheral instances)
- **Boot flow** (how Zephyr handles DEV mode vs FSBL)

Zephyr docs: https://docs.zephyrproject.org/latest/boards/st/nucleo_n657x0_q/doc/index.html
Zephyr source: `boards/st/nucleo_n657x0_q/` and `soc/st/stm32/stm32n6x/` in the Zephyr tree

### NuttX References

- **STM32H5 port**: `arch/arm/src/stm32h5/` — closest existing chip port (Cortex-M33, ARMv8-M)
- **ARMv8-M arch**: `arch/arm/src/armv8-m/` — base architecture code to fork
- **NuttX Porting Guide**: https://cwiki.apache.org/confluence/display/NUTTX/Porting+Guide

### ARM Documentation

- **Cortex-M55 TRM**: Technical Reference Manual (pipeline, cache, TCM details)
- **ARMv8.1-M Architecture Reference Manual**: DDI0553
- **ARM blog on ARMv8.1-M software tips**: https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/armv8_2d00_m-based-processor-software-development-hints-and-tips

---

## Bring-up Checklist

### Phase 1: Minimal boot (DEV mode, SRAM only)

- [ ] Set BOOT1 switch to DEV mode (position 2-3) on Nucleo board
- [ ] Create `armv81m` architecture directory (fork from `armv8-m`)
- [ ] Create minimal `stm32n6` chip directory with RCC, GPIO, USART stubs
- [ ] Create `nucleo-n657x0-q` board directory with linker script targeting SRAM
- [ ] Implement `SystemInit()` equivalent: configure clocks at safe speed (~64 MHz)
- [ ] Configure GPIO pin mux for VCP UART
- [ ] Implement UART driver (polling mode first, interrupt-driven later)
- [ ] Implement SysTick timer
- [ ] Build NuttX, load via ST-LINK into SRAM, verify serial output
- [ ] **Goal: NSH prompt over VCP**

### Phase 2: Core functionality

- [ ] Enable I-cache and D-cache
- [ ] Enable LOB (Low-Overhead Branch) in CCR
- [ ] Increase clock speed toward 600 MHz (or 800 MHz overdrive)
- [ ] Interrupt-driven UART
- [ ] GPIO driver (user LED, user button)
- [ ] DMA support
- [ ] Basic timer drivers

### Phase 3: Standalone boot

- [ ] Create or adapt FSBL to load NuttX from external flash
- [ ] Implement XSPI driver for external NOR flash
- [ ] Configure MPU regions for external memory
- [ ] Test load-and-run boot mode
- [ ] Test XIP boot mode (optional)

### Phase 4: Extended peripherals

- [ ] SPI, I2C, I3C drivers
- [ ] USB driver
- [ ] Ethernet driver (Gigabit + TSN)
- [ ] CAN-FD
- [ ] ADC / DAC
- [ ] Additional timers / PWM
- [ ] SDMMC (if applicable)

---

## Common Pitfalls

1. **Flashless design**: Don't look for internal flash base addresses — there are none. Everything starts in SRAM or external memory.

2. **Boot ROM SRAM reservation**: The boot ROM uses some SRAM. Don't place your vector table at the very start of the SRAM region.

3. **Overdrive mode**: Don't try to run at 800 MHz initially. It requires voltage scale 0 and SMPS configuration (PB12). Start at 64 MHz.

4. **Cache coherency**: When caches are enabled, DMA transfers require cache maintenance operations. Leave caches disabled for Phase 1.

5. **XSPI clock**: When configuring XSPI for external flash later, the clock source and speed matter. The Nucleo board's external flash has been tested working at 32-50 MHz XSPI clock. Higher AXI bus clocks can cause issues with XIP if not configured carefully.

6. **Image signing**: For standalone boot (Phase 3), the boot ROM expects signed images with specific headers. Use STM32CubeProgrammer or the ST signing tool to add headers.

7. **Compiler support**: Use a recent ARM GNU toolchain. GCC versions before 2021 have limited or buggy Cortex-M55 support. GCC 12+ is recommended.
