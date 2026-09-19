# Implementation Process: Porting NuttX RTOS and PX4 Autopilot to the STM32N6 Cortex-M55 Platform

> A thesis-grade engineering chronicle documenting the design, implementation, and integration of Apache NuttX RTOS and PX4 Autopilot on the STM32N657X0 microcontroller, including Neural Processing Unit (NPU) inference for real-time object detection.

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Target Hardware](#2-target-hardware)
3. [Repository Structure and Methodology](#3-repository-structure-and-methodology)
4. [Phase 1: Initial Board Bring-Up (March 11, 2026)](#4-phase-1-initial-board-bring-up)
5. [Phase 2: Clock Configuration and Core Peripherals (March 11-12, 2026)](#5-phase-2-clock-configuration-and-core-peripherals)
6. [Phase 3: Standalone Boot from External Flash (December 2025 - March 2026)](#6-phase-3-standalone-boot-from-external-flash)
7. [Phase 4: Extended Peripheral Drivers (March 13-25, 2026)](#7-phase-4-extended-peripheral-drivers)
8. [NPU Integration: Neural-ART Accelerator (March 17-25, 2026)](#8-npu-integration-neural-art-accelerator)
9. [PX4 Autopilot Integration (March 25-26, 2026)](#9-px4-autopilot-integration)
10. [Cross-Cutting Technical Challenges](#10-cross-cutting-technical-challenges)
11. [Quantitative Summary](#11-quantitative-summary)
12. [Timeline Visualization](#12-timeline-visualization)
13. [Lessons Learned](#13-lessons-learned)
14. [Appendices](#14-appendices)

---

## 1. Project Overview

### 1.1 Objective

Port the Apache NuttX real-time operating system (RTOS) to the STMicroelectronics STM32N657X0 microcontroller — an ARM Cortex-M55 running at 800 MHz with an integrated Neural Processing Unit (NPU). Subsequently, integrate the PX4 Autopilot flight control software on this platform, leveraging the NPU for real-time YOLOv8n person detection at 29 FPS.

### 1.2 Significance

The STM32N6 represents STMicroelectronics' first MCU with an integrated neural accelerator (Neural-ART/ATON). At the time of this work, no open-source RTOS port existed for this chip family. The combination of a real-time flight controller with on-chip neural inference on a microcontroller (not an application processor) represents a novel integration point for edge-AI in safety-critical embedded systems.

### 1.3 Scope

| Component | Description |
|-----------|-------------|
| **NuttX RTOS** | Full chip-level BSP + board support for NUCLEO-N657X0-Q |
| **Peripheral drivers** | UART, SPI, I2C, ADC, PWM, RTC, RNG, IWDG, DMA, USB, XSPI, NPU |
| **FSBL** | First-Stage Boot Loader for external flash boot |
| **NPU inference** | INT8 YOLOv8n 192x192 person detection at 33ms/frame |
| **PX4 integration** | Hardware-in-the-loop (HIL) flight controller with NPU module |

### 1.4 Development Timeline

- **December 26, 2025**: Project inception — FSBL, HAL reference code, initial STATUS.md
- **March 11-12, 2026**: NuttX chip/board bring-up (Phase 1-2)
- **March 13-25, 2026**: Peripheral drivers and NPU (Phase 4)
- **March 25-26, 2026**: PX4 Autopilot integration
- **Total NuttX porting duration**: 15 calendar days (March 11-25)
- **Total PX4 integration duration**: 2 calendar days (March 25-26)

---

## 2. Target Hardware

### 2.1 MCU: STM32N657X0H3Q

| Feature | Specification |
|---------|---------------|
| Core | ARM Cortex-M55, ARMv8.1-M Mainline |
| Max frequency | 800 MHz (VOS SCALE0 + SMPS overdrive) |
| FPU | FPv5-D16 double-precision (no MVE/Helium) |
| TrustZone | Always active, code runs in Secure state |
| Internal flash | **None** |
| AXI-SRAM | 3.75 MB (6 banks at 0x34000000: 2x1 MB + 4x448 KB) |
| NPU | Neural-ART ATON accelerator, 1 GHz core clock |
| USB | OTG HS with embedded UTMI+ PHY (DWC2 v4.11a) |
| External flash | Via XSPI2 (OPI DTR 8-8-8 at 200 MHz, 400 MB/s) |

### 2.2 Board: NUCLEO-N657X0-Q

| Feature | Detail |
|---------|--------|
| Debug | ST-LINK V3EC (SWD + VCP on USART1) |
| Console | USART1 PE5(TX)/PE6(RX) AF7, 115200 8N1 |
| LEDs | PG0 (green), PG8 (blue), PG10 (red) — active-LOW |
| Button | PC13 (active-HIGH) |
| USB | Type-C on CN8 (USB1_OTG_HS) |
| External flash | MX25UM51245G 64 MB via XSPI2 |

### 2.3 Memory Map

#### AXI-SRAM Bank Layout (from CMSIS header `stm32n657xx.h`)

| Bank | Secure Base | Size | Usage |
|------|-------------|------|-------|
| SRAM1 | 0x34000000 | 1024 KB | NuttX code + data + heap |
| SRAM2 | 0x34100000 | 1024 KB | NuttX heap (continued) |
| SRAM3 | 0x34200000 | 448 KB | NPU activations |
| SRAM4 | 0x34270000 | 448 KB | NPU activations |
| SRAM5 | 0x342E0000 | 448 KB | NPU activations |
| SRAM6 | 0x34350000 | 448 KB | NPU activations |
| CACHEAXIRAM | 0x343C0000 | 256 KB | NPU AXI cache (or general SRAM) |
| **Total** | | **4096 KB (4 MB)** | |

Note: VENCRAM (128 KB at 0x34400000) is excluded — reserved for video encoder. Each bank requires its RCC MEMENR clock enable bit. The linker script allocates the entire 4 MB region (0x34000400-0x34400000) to NuttX; in practice, SRAM3-6 are used exclusively by the NPU when inference is active.

```
0x34000000 +------------------+ SRAM1 (1 MB)
           | Boot ROM vectors | 0x400 bytes reserved
0x34000400 +------------------+
           | NuttX text+data  | Code, rodata, data, bss
           | Heap / Stack     | Remainder of SRAM1+SRAM2
0x34100000 +------------------+ SRAM2 (1 MB)
           | (continued heap) | NuttX heap extends here
0x34200000 +------------------+ SRAM3 (448 KB)
           | NPU activations  |
0x34270000 +------------------+ SRAM4 (448 KB)
           | NPU activations  |
0x342E0000 +------------------+ SRAM5 (448 KB)
           | NPU activations  |
0x34350000 +------------------+ SRAM6 (448 KB)
           | NPU activations  |
0x343C0000 +------------------+ CACHEAXIRAM (256 KB)
           | NPU cache / SRAM | Usable as general SRAM when CACHEAXI disabled
0x34400000 +------------------+ End of AXI-SRAM (4 MB total)

0x70000000 +------------------+
           | FSBL (128 KB)    | Signed, external flash
0x70020000 +------------------+
           | NuttX (1 MB)     | External flash
0x70120000 +------------------+
           | DNN weights      | 16 MB
0x71000000 +------------------+
           | NPU test weights | 2.8 KB
0x71120000 +------------------+
           | littlefs         | 47 MB
0x74000000 +------------------+
```

---

## 3. Repository Structure and Methodology

### 3.1 Three-Repository Architecture

The project spans three Git repositories:

| Repository | Purpose | Commits | Date Range |
|------------|---------|---------|------------|
| **STM32N6Nuttx** (top-level) | Build system, FSBL, host tooling, documentation | 34 | Dec 26, 2025 - Mar 26, 2026 |
| **nuttx/** (submodule) | NuttX RTOS kernel + STM32N6 chip/board support | 104 (STM32N6) | Mar 11-25, 2026 |
| **PX4-Autopilot** (separate) | PX4 flight controller + STM32N6 board/platform | 6 (STM32N6) | Mar 25-26, 2026 |

The NuttX repository is a fork of the upstream Apache NuttX (62,212 total commits dating to February 2007). The 104 STM32N6-specific commits were authored on the `master` branch.

### 3.2 Porting Methodology

The port followed a bottom-up, hardware-first approach:

1. **Reference code study**: ST HAL drivers in `SampleSTM32Project/` and Zephyr RTOS (`zephyr/`) provided register-level reference
2. **Structural template**: The existing NuttX STM32H5 port (`nuttx/arch/arm/src/stm32h5/`) served as the architectural template, sharing the same ARMv8-M base
3. **Iterative bring-up**: Each peripheral was implemented, tested on hardware, then committed — resulting in tight commit-test-fix cycles visible in the Git history
4. **AI-assisted development**: The project used Claude Code (Anthropic CLI) as a development accelerator, with persistent context maintained via `CLAUDE.md` project instructions

### 3.3 Build System

A top-level `Makefile` (200 lines) orchestrated all build, sign, and flash operations:

```
make all          # Build NuttX + FSBL + sign
make flash-dev    # Flash to SRAM (DEV mode, no FSBL)
make flash        # Flash signed FSBL + NuttX to external flash
make npu-model    # End-to-end NPU model generation pipeline
make px4          # Build PX4 for stm_n6-nucleo_default
```

---

## 4. Phase 1: Initial Board Bring-Up

### 4.1 Scope

Get NuttX booting to the first `printf` on USART1 in DEV mode (SRAM execution).

### 4.2 Commit: `b9b9624c8d` — March 11, 2026

**"feat(stm32n6): add STM32N6 chip support and nucleo-n657x0-q board"**

- 51 files changed, 8,408 insertions
- Full chip layer: RCC, GPIO, USART, PWR, IRQ, SysTick, startup
- Board layer: USART1 console, SRAM-only linker script, NSH defconfig

### 4.3 Key Technical Challenges

#### 4.3.1 ARMv8-M Boot ROM Stack Limits

The STM32N6 boot ROM sets `MSPLIM` and `PSPLIM` registers to values that conflict with NuttX's stack layout. Any stack access below these limits triggers a `UsageFault`. The solution was to clear both registers as the very first instructions of the `__start` entry point, implemented via the GCC linker `--wrap=__start` mechanism to guarantee execution before any C prologue code.

#### 4.3.2 TrustZone Security Model

TrustZone cannot be disabled on the STM32N6. All code runs in Secure state, requiring:
- **Secure peripheral aliases** (0x5xxxxxxx) for all MMIO access
- **RIFSC configuration** for DMA masters (RIMC_ATTR: CID=1, SEC, PRIV)
- **RCC RIF protection**: Direct read-modify-write of clock enable registers (ENR) is blocked. All clock enables must use atomic SET registers (ENSR at offset +0x800)

#### 4.3.3 FPU Initialization Ordering

The Cortex-M55 with hard-float ABI (`-mfloat-abi=hard`) requires the FPU to be enabled before *any* C function call, because the compiler may use FP registers for argument passing. FPU enable was placed in assembly before the first `bl` instruction.

#### 4.3.4 Power Supply Sequencing

GPIO ports on VddIO2/3/4/5 domains require explicit power supply enable via PWR SVMCR registers before any GPIO access. Without this, GPIO configuration writes silently fail.

### 4.4 Initial Status

At the end of Phase 1, the system booted to `nx_start()` but hit an unexpected IRQ 5 during kernel initialization. This was later traced to SysTick configuration inherited from the boot ROM.

---

## 5. Phase 2: Clock Configuration and Core Peripherals

### 5.1 Scope

Configure PLL1 for maximum CPU frequency, enable caches, switch to TIM2 system timer, and achieve a stable NSH shell.

### 5.2 Key Commits (March 11-12, 2026)

| Commit | Description |
|--------|-------------|
| `ae9cff155f` | Enable AXISRAM clocks, fix SRAM size for heap |
| `ad107edab0` | Disable WFI, enable USART FIFO, add early fault handler |
| `6bac9a6b7f` | TIM2 timer, enable LPEN for WFI sleep |
| `3298ffd102` | PLL1 clock config with CPUSW+SYSSW switch |
| `cba0ec4f9f` | 600 MHz CPU via final IC divider configuration |
| `7e48a36159` | Enable I-cache and D-cache |
| `48f7abe0a1` | GPIO LED/button drivers with SYSCFG compensation |

### 5.3 STM32N6 Clock Architecture

The STM32N6 has a unique clock tree with 4 PLLs, each producing up to 4 outputs, feeding 20 IC (Interconnect Clock) dividers that supply the bus matrix. This is significantly more complex than previous STM32 families.

```
HSI (64 MHz) → PLL1 (M=4, N=75) → VCO 1200 MHz
  ├─ IC1 = PLL1/2 = 600 MHz → CPU (CPUSW)
  ├─ IC2 = PLL1/3 = 400 MHz → SYSCLK (SYSSW)
  ├─ IC6 = PLL1/4 = 300 MHz → AHB bus
  └─ IC11= PLL1/3 = 400 MHz → APB bus
HPRE = /2 → HCLK = 200 MHz
USART1 kernel clock = HSI (64 MHz, independent)
```

#### 5.3.1 Clock Register Locking Behavior (STM32N6-specific)

A critical discovery was that `CFGR1` (clock source select) and `CFGR2` (bus prescalers) **lock after the first write** in a specific sequence:

1. `CFGR2` must be written **before** `CFGR1` (because CFGR2 locks after CFGR1 is written)
2. `CPUSW` and `SYSSW` fields in `CFGR1` must be written in a **single `putreg32()`** (because the register locks after one write)

This behavior is undocumented in RM0486 and was discovered through trial-and-error debugging. Violating either rule causes the clock switch to silently fail, leaving the system on the default HSI clock.

#### 5.3.2 CFGR2 Prescaler Encoding

Unlike all previous STM32 families (which use a "4+x" encoding where values 0-3 mean /1 and 4=log2(divisor)+3), the STM32N6 uses a simple power-of-2 encoding: 0=/1, 1=/2, 2=/4, 3=/8. Using the old encoding caused the AHB bus to run at an incorrect frequency, which manifested as UART baud rate errors.

### 5.4 WFI/Sleep Support

The Cortex-M55 `WFI` instruction enters CSLEEP mode, during which all clocks gated by LPEN (Low-Power Enable) registers are stopped. Without proper LPEN configuration, WFI halted all peripheral clocks including USART1 and timers. The fix required:
1. Enabling LPEN bits for all active peripherals (USART1, TIM2, GPIO, SRAM banks)
2. Enabling `BSECEN` (APB4 bit 1) — discovered as a requirement from errata ES0620
3. Switching from SysTick to TIM2 (SysTick stops during CSLEEP)

---

## 6. Phase 3: Standalone Boot from External Flash

### 6.1 Scope

Boot NuttX autonomously from external flash via a signed FSBL, without requiring a debugger.

### 6.2 FSBL Development (December 2025)

The FSBL work preceded the NuttX port by nearly 3 months. Key top-level repository commits:

| Date | Commit | Description |
|------|--------|-------------|
| Dec 26, 2025 | `5205240` | Initial commit with STATUS.md |
| Dec 26, 2025 | `6dad972` | Add SampleSTM32Project with full ST HAL (1,009,418 lines) |
| Dec 26, 2025 | `06ead11` | FSBL with XSPI flash support and serial debug |
| Dec 30, 2025 | `dd23995` | Document XSPI and serial debug findings |

### 6.3 Boot Flow

```
Power-on → Boot ROM → Verify FSBL signature → Load FSBL to SRAM
  → FSBL: SMPS overdrive → VOS SCALE0 → PLL1 @ 800 MHz → XSPI2 init
  → Copy NuttX from 0x70020000 (flash) → 0x34000400 (SRAM)
  → Set VTOR → Jump to NuttX __start
  → NuttX: Clear MSPLIM/PSPLIM → Enable FPU → Disable SysTick
  → Detect PLL1 already running (skip clock config) → NSH shell
```

### 6.4 Key Technical Challenges

#### 6.4.1 Firmware Signing

The STM32N6 boot ROM requires cryptographically signed firmware. The signing tool (`STM32_SigningTool_CLI v2.22+`) must be invoked with the `-align` flag for header v2.3 compatibility. Earlier versions (v2.19/v2.20) lacked this flag entirely.

#### 6.4.2 Clock Detection on NuttX Entry

When booted via FSBL, PLL1 is already running at 800 MHz. NuttX's clock initialization code must detect this (by reading `CFGR1.CPUSWS == IC1`) and skip reconfiguration. Attempting to reconfigure PLL1 while it's active causes a hard fault due to the CFGR1 locking behavior described in Section 5.3.1.

#### 6.4.3 SysTick Inheritance

The FSBL uses ST's HAL, which enables SysTick at 1 ms period. If NuttX does not disable SysTick immediately upon entry, the pending SysTick interrupt fires before NuttX's vector table is installed, causing an unexpected IRQ crash. The fix: `putreg32(0, NVIC_SYSTICK_CTRL)` as the first operation in `__start`.

---

## 7. Phase 4: Extended Peripheral Drivers

### 7.1 Overview

Phase 4 implemented 12 peripheral drivers in 13 days, with significant debugging effort around DMA cache coherency and USB protocol compliance.

### 7.2 DMA Subsystem (March 13, 2026)

#### 7.2.1 Dual-Engine Architecture

The STM32N6 has two independent DMA engines — a first for the STM32 family in NuttX:

| Engine | Base | Channels | FIFO | Use Case |
|--------|------|----------|------|----------|
| HPDMA1 | 0x58020000 | 16 | 16B (ch0-11), 64B (ch12-15) | High-performance M2M |
| GPDMA1 | 0x50021000 | 16 | 8B (ch0-11), 32B (ch12-15) | Low-power P2M/M2P |

The implementation (`stm32_dma.c`, 1,070 lines) includes a type-aware channel allocator that automatically selects the optimal engine based on transfer type:
- **P2M/M2P**: Prefers GPDMA1 (lower power)
- **M2M**: Prefers HPDMA1 ch12-15 (64B FIFO for burst transfers)
- **2D transfers**: Restricted to ch12-15 (hardware limitation)

Key commits: `1374b0f831` (initial), `8234ad7e56` (overhaul with dual-engine), `373bd88ec5` (AXI clock fix for HPDMA1).

#### 7.2.2 AXI Clock Dependency

HPDMA1 requires AXI infrastructure clocks (`ACLKN`, `ACLKNC` via BUSENSR) in addition to its peripheral clock. Without these, HPDMA1 channels enable but silently fail to transfer data. This dependency is not present for GPDMA1 and is not documented in the reference manual's DMA chapter.

### 7.3 MPU and Cache Coherency (March 13, 2026)

#### 7.3.1 MPU Configuration

Commit `4790c0e3e7` added an MPU Write-Back Read/Write-Allocate region for the entire AXI-SRAM (0x34000000, 4 MB). This was necessary because Cortex-M55 MVA-based D-cache maintenance operations (DCCMVAC, DCIMVAC) require explicit cacheable memory attributes from the MPU to function.

#### 7.3.2 MEMFAULTENA Bug (Cortex-M55 Secure State)

A critical hardware bug was discovered: enabling `MEMFAULTENA` in the `SHCSR` register causes D-cache maintenance operations to **silently fail** on Cortex-M55 in Secure state. The workaround is to leave `MEMFAULTENA` disabled; MemManage faults escalate to HardFault with full CFSR/MMFAR diagnostics.

#### 7.3.3 MVA Cache Operations Bug

Commit `138d10f357` documented and worked around a deeper issue: MVA-based cache operations (`DCIMVAC`, `DCCIMVAC`) fail silently at certain buffer alignments on Cortex-M55 in Secure state. The fix was to replace all MVA operations with set/way operations (`DCCISW`) across all DMA drivers. While less efficient (full cache clean+invalidate vs. targeted range), set/way operations work reliably because they operate on the cache structure directly rather than through the address translation path.

### 7.4 SPI Driver (March 13, 2026)

Commit `ede77155b2` — 2,405 lines. SPI5 on PE15(SCK)/PG1(MISO)/PG2(MOSI), AF5. Uses the STM32N6's TSIZE-based transfer model (different from older STM32 SPI peripherals). Polling mode verified first, then DMA mode added with 32-byte-aligned bounce buffers to work around the D-cache coherency issues.

### 7.5 USB OTG HS Device Driver (March 14-23, 2026)

The USB driver was the most complex peripheral, requiring **32 commits** over 10 days.

#### 7.5.1 Initial Implementation (March 14)

Commit `43f653534f` — 7,340 lines (largest single driver). Adapted from the STM32H7 DWC2 driver with extensive STM32N6-specific changes:

- **GCCFG register**: Completely different bit layout from H7 (VBUS override vs. PWRDWN model)
- **USBPHYC_CR**: FSEL field at different bit position, OTGDISABLE0 default polarity reversed
- **PHY reference clock**: HSE/2 divider (HSEDIV2SEL) required before HSE enable
- **AHB5 reset sequence**: PHY requires specific reset ordering (assert OTG1PHYCTLRST + OTG1RST + OTGPHY1RST, release in order with delays)

#### 7.5.2 DMA Mode Evolution

The USB DMA implementation went through three distinct architectures:

| Mode | Commit | Approach | Outcome |
|------|--------|----------|---------|
| PIO (slave) | `43f653534f` | CPU copies FIFO data | Working, 6 MB/s |
| Descriptor DMA | `6681199963` | 8-byte descriptors | Failed: never enumerated |
| Buffer DMA | `ff5109e362` | Direct buffer pointers | Working, ~12 MB/s |
| PIO (final) | `85c4887ada` | Reverted to PIO | Adopted due to DMA 4-byte shift bug |

A persistent 4-byte data shift in DMA mode (where every read returned data offset by 4 bytes) was ultimately not resolved. PIO mode at 6 MB/s proved sufficient for the CDC/ACM use case.

#### 7.5.3 USB Cache Coherency (21 commits, March 21-23)

The USB DMA cache coherency debugging consumed the most concentrated effort in the entire project. Key issues:
- DMA descriptors required cache-aligned buffers
- Cortex-M55 MVA invalidate (`DCIMVAC`) corrupted adjacent cache lines
- Final solution: set/way clean+invalidate (`DCCISW`) before DMA setup, matching the approach used for all other DMA drivers

### 7.6 I2C Driver (March 14, 2026)

Commits `f47482a4fb` + `bc825d8005`. Ported from the STM32H5 I2C driver with Cortex-M55-specific interrupt handling fixes. I2C2 enabled for TCPP0203 USB Type-C controller at address 0x34.

### 7.7 ADC Driver (March 15, 2026)

Commits `bf7b24d7a0` + `ec60f2034b`. Software-triggered single-conversion mode on ADC1. Required RIFSC security configuration for peripheral access from Secure state.

### 7.8 PWM, RTC, RNG, IWDG (March 15-16, 2026)

| Driver | Commit | Lines | Notes |
|--------|--------|-------|-------|
| PWM | `708934b23d` | — | TIM3/TIM4, ported from H5 |
| RTC | `4446635b82` | — | LSE source, ICSR/SR/SCR register model |
| RNG | `1ff72bc47d` | — | CONDRST initialization, AHB3 LPEN required |
| IWDG | `1ff72bc47d` | — | LSI source, standard NuttX watchdog interface |

### 7.9 XSPI2 External Flash Driver (March 16-21, 2026)

#### 7.9.1 SPI 1-1-1 Mode (March 16)

Commit `5a08b651c7` — 2,411 lines. Initial MX25UM51245G driver in standard SPI mode (1-1-1) at 25 MHz with littlefs filesystem. Required extending the existing NuttX `mx25rxx` MTD driver with MX25UM51245G-specific commands and 4-byte addressing.

#### 7.9.2 OPI STR 8-8-8 Mode (March 20)

Commit `094ad7923b`. Switched to Octal SPI (8-8-8) Single Transfer Rate at 100 MHz for faster NPU weight reads.

#### 7.9.3 OPI DTR 8-8-8 Mode (March 21)

Commit `4a02d41fac` — the performance breakthrough. Dual Transfer Rate at 200 MHz provided 400 MB/s effective bandwidth:

```
SPI 1-1-1 @ 25 MHz:  ~3 MB/s   → 34.5 seconds for 16 MB weights
OPI STR @ 100 MHz:   ~100 MB/s → 160 ms
OPI DTR @ 200 MHz:   ~400 MB/s → 90 ms   (380x speedup from baseline)
```

Key implementation details from ST BSP analysis:
- DHQC (Data Hold Quarter Clock) is deprecated on STM32N6 — replaced by automatic DLL calibration
- DTR requires even byte count for data phase
- Flash mode switch uses Write CR2 command (not RSTEN/RST, which proved unreliable)
- Controller requires full reinit after memory-mapped mode abort

#### 7.9.4 Memory-Mapped Mode Toggle

Commits `312f9928b1` + `36e60207a0`. The XSPI2 peripheral can operate in either indirect mode (register-driven read/write/erase) or memory-mapped mode (CPU reads flash via address bus). The NPU needs memory-mapped mode for weight access, while littlefs needs indirect mode. The implementation automatically toggles between modes using a mutex-protected state machine.

### 7.10 Linker Script Fix (March 13, 2026)

Commit `adbbf760d7` — a critical bug fix. In SRAM-only builds (no flash), the `_eronly` symbol must equal `_sdata` so the data initialization copy loop is skipped (data is already in place). Placing an `ALIGN()` directive between `_eronly` and `.data` caused GNU `ld` to place `.data` before the alignment padding, corrupting all initialized data. The fix: define `_eronly = ABSOLUTE(.)` as the first line inside the `.data : { }` section.

Symptom: NULL device pointers in `earlyserialinit()` → bus fault. This class of bug is particularly insidious because it only manifests when the linker's layout decisions change.

---

## 8. NPU Integration: Neural-ART Accelerator

### 8.1 Overview

The STM32N6's Neural-ART ATON accelerator is a fixed-function neural network inference engine with dedicated stream engines (STRENGs), a dedicated AXI cache (CACHEAXI), and support for INT8 quantized models compiled by STMicroelectronics' STEdgeAI toolchain.

### 8.2 NPU Hardware Initialization (March 17, 2026)

Commit `fee9573f1a` — 925 insertions. The initialization sequence is extensive:

1. **RAMCFG**: Power on SRAM3-6 (clear SRAMSD bit 20 in each bank's control register)
2. **RIFSC**: Configure NPU as bus master (RIMC_ATTR[1] = CID=1, SEC, PRIV)
3. **ATON fabric**: CTRL.CLR → CTRL.EN → enable all clock gates (AGATES0/1/BGATES) → enable BUSIF0/1 + INTCTRL
4. **Disable SRAM interleaving** (SYSCFG offset 0x78, bit 0 = 0)
5. **CACHEAXI**: enable clock → reset → invalidate → enable
6. **SMPS overdrive** + VOS SCALE0 (must happen *before* PLL2/PLL3 configuration)
7. **PLL2 @ 1 GHz** (NPU core via IC6), **PLL3 @ 900 MHz** (NPU SRAM via IC11)

#### 8.2.1 CACHEAXI Conflict

A major discovery (documented across commits `dc772f271e`, `05eab0cc3c`, `3e5673d83b`): the CACHEAXI, once enabled, intercepts all AXI traffic to the external memory region — including CPU reads of XSPI2 flash. This breaks the XSPI2 indirect-mode driver, which returns zeros instead of flash data.

Solution: disable CACHEAXI after NPU self-test, re-enable only during NPU inference (toggle per-inference via the `/dev/npu0` ioctl).

#### 8.2.2 STRENG Memcpy Validation

The STRENG stream engines were validated with a 96-byte memory-to-memory copy test. Critical finding: `CID_CACHE` must use non-cacheable mode (`0x01`) for SRAM-to-SRAM transfers. With `CACHEABLE=1`, NPU writes remain in the CACHEAXI write-back cache and never reach SRAM.

### 8.3 Model Pipeline

```
create_model.py → float32 ONNX
  → onnxruntime quantize → signed INT8 ONNX
  → STEdgeAI v3.0 (--target stm32n6 --st-neural-art) → C code + binary weights
  → Flash weights to 0x71000000 (make flash-weights)
```

**Critical constraint**: STEdgeAI v3.0 requires **signed INT8** for both weights and activations. Using uint8 activations causes export errors. Using float32 produces only software epochs (CPU-executed), defeating the purpose of the NPU.

### 8.4 Model Evolution

| Model | Commit | Epochs | Inference Time | Notes |
|-------|--------|--------|----------------|-------|
| INT8 Conv2D (test) | `ca553c6d1b` | 4 HW | ~1 ms | 2-layer validation model |
| TinyYOLOv2 224x224 | `7bbc4e0c81` | 11 HW + 1 SW | 34.5 s (SPI) → 90 ms (OPI DTR) | ST LL_ATON runtime |
| YOLOX-Nano 192x192 | `597ccdcf48` | HW + SW | — | Constant output bug (unresolved) |
| YOLOv8n 192x192 | `2d02e9b423` | HW + SW | 33 ms | Production model, RunEpochBlock API |

### 8.5 Driver Architecture Evolution

The NPU driver went through three architectural iterations:

#### Phase 1: App-compiled (March 17-18)
Model code compiled directly into the application via VPATH. Tightly coupled, no kernel abstraction.

#### Phase 2: NuttX AIE Framework (March 19)
Commit `dc772f271e` restructured into a proper upper/lower-half NuttX driver:

```
Application (npu_test_main.c)
  → open("/dev/npu0") → ioctl(NPUIOC_RUN_SYNC) → close()
    Upper half: ai_engine.c (generic NuttX framework)
    Lower half: stm32_npu.c (STM32N6-specific)
      ├── stm32_aton_hw.c (register programming)
      ├── npu_model.c (generated epoch code)
      └── board/stm32_npu.c (HW init + registration)
```

#### Phase 3: Async with OSAL (March 25)
Commit `dcd3ad4452`. Switched to `RT_MODE=2` (asynchronous) with NuttX OSAL (Operating System Abstraction Layer). The NPU runs epochs asynchronously while the scheduler yields the CPU via `sched_yield()` + `WFE` polling.

### 8.6 SMPS Overdrive for 800 MHz + NPU

Running the CPU at 800 MHz and the NPU at 1 GHz requires SMPS (Switched-Mode Power Supply) overdrive:

1. Set PB12 HIGH (enables SMPS high-voltage output)
2. Configure VOS SCALE0 in PWR registers
3. Poll VOSRDY until voltage stabilizes (no fixed delay — hardware-dependent)

Critical ordering: SMPS overdrive must complete **before** configuring PLL2/PLL3 for NPU clocks, because 1 GHz operation requires the higher supply voltage.

---

## 9. PX4 Autopilot Integration

### 9.1 Overview

PX4 Autopilot was integrated on the STM32N6 as a hardware-in-the-loop (HIL) flight controller communicating with jMAVSim, with on-chip NPU inference running in parallel.

### 9.2 Branch Strategy

The PX4 work lives on branch `pr-nuttx-12-12`, which is based on the PX4 NuttX 12.12.0 upgrade branch (1,188 total commits from upstream). The 6 STM32N6-specific commits were added on top.

### 9.3 Board Support (March 25, 2026)

Commit `0a7620690c` — 58 files, 4,495 insertions.

```
boards/stm/n6-nucleo/
├── default.px4board          # Module selection
├── nuttx-config/
│   ├── nsh/defconfig          # NuttX kernel config
│   ├── scripts/script.ld      # SRAM linker script
│   └── include/board.h        # Clock/pin definitions
├── src/
│   ├── init.c                 # Board initialization
│   ├── stm32_boot.c           # Early boot
│   ├── stm32_bringup.c        # Peripheral registration
│   ├── stm32_npu.c            # NPU HW init
│   ├── stm32_xspi.c           # XSPI2 flash init
│   ├── stm32_adc.c            # ADC channels
│   └── ...                    # SPI, I2C, USB, PWM, LEDs, buttons
├── extras/
│   ├── npu_model/             # Generated YOLOv8n192 model code
│   └── stm32n6_px4.mpool      # NPU memory pool
├── fsbl/                      # FSBL source + prebuilt binaries
└── ROMFS/n6_nucleo/init.d/rcS # Startup script (HIL quadcopter)
```

### 9.4 Platform Layer (March 25, 2026)

```
platforms/nuttx/src/px4/stm/stm32n6/
├── hrt/hrt.c                  # High Resolution Timer (TIM5, 1 MHz)
├── adc/adc.cpp                # ADC stub
├── board_hw_info/             # MCU version info
└── include/px4_arch/          # Hardware abstraction headers
```

### 9.5 High Resolution Timer (HRT)

The HRT was the most challenging PX4 platform component. The initial implementation had broken periodic callbacks — `ScheduleOnInterval` never fired because:

1. `deadline` wasn't zeroed before callback invocation
2. Periodic re-arm used current time instead of original `deadline + period`
3. `hrt_call_enter` didn't match the reference sorted-insert logic
4. Missing `hrt_call_at`, `hrt_called`, `hrt_call_delay` functions

Commit `268d89faca` rewrote the HRT based on `stm32_common/hrt/hrt.c` reference. A key fix: `hrt_call_reschedule()` now handles deadlines in the past by scheduling ASAP (`now + 50us`) instead of waiting for the 32-bit counter to wrap (~4295 seconds).

The broken HRT masked failures in `load_mon` (no CPU load data), `logger` (no command processing), and all timer-driven `ScheduledWorkItems`. Only subscription-triggered work items (sensor callbacks) functioned, making the system appear partially operational.

### 9.6 NPU Inference Module (March 26, 2026)

Commit `b9c74bfc38` — `src/modules/npu_inference/` (311 lines).

```c
// Core loop: open /dev/npu0, feed image, run inference, read results
fd = open("/dev/npu0", O_RDWR);
ioctl(fd, NPUIOC_FEED, &input_buf);   // Send 192x192 INT8 image
ioctl(fd, NPUIOC_RUN_SYNC, NULL);      // Run inference (33ms)
ioctl(fd, NPUIOC_GET_OUTPUT, &output);  // Read detections
```

Performance: **34 ms inference (29 FPS)** on the ATON NPU, with CPU available for flight control during NPU execution.

Features:
- Runtime-tunable poll ratio for CPU/latency tradeoff
- Statistics tracking (avg/min/max/fps with reset)
- MAVLink telemetry via `NAMED_VALUE_FLOAT` (npu_ms, npu_fps, npu_avg)
- Memory isolation: NPU uses SRAM3-6 only (no overlap with PX4's 2 MB region in SRAM1-2)

### 9.7 ROMFS Startup Script

The custom `rcS` startup script configures PX4 as a HIL quadcopter:
- MAVLink on USB CDC/ACM (20 KB/s rate)
- Hardcoded parameters (no persistent storage — SRAM-only system)
- Simulated PWM output to keep jMAVSim heartbeat alive
- NPU inference module auto-start

### 9.8 Build Integration

The top-level `Makefile` added PX4 targets (commit `519b210`):

```makefile
PX4_DIR := $(HOME)/Documents/GitHub/PX4-Autopilot
px4:
    cd $(PX4_DIR) && make stm_n6-nucleo_default
flash-px4-dev:
    # Flash to SRAM via SWD (DEV mode)
flash-px4:
    # Flash signed FSBL + PX4 to external flash
```

---

## 10. Cross-Cutting Technical Challenges

### 10.1 TrustZone in an RTOS Context

The STM32N6's always-on TrustZone presents unique challenges for RTOS development:

| Challenge | Impact | Solution |
|-----------|--------|----------|
| Secure peripheral aliases | All MMIO addresses offset by +0x10000000 | Dedicated memory map header with 0x5xxxxxxx bases |
| RCC RIF protection | Direct ENR R-M-W fails | All clock enables via ENSR (SET registers) |
| RIFSC per-peripheral security | DMA masters can't access peripherals | RIMC_ATTR + RISC SECCFGR configuration |
| DAP read restrictions | Debugger can't read RCC registers | Dump registers from CPU side via firmware |
| PWR SVMCR RIF protection | Reading SVMCR causes bus fault | Blind writes based on known hardware state |

### 10.2 D-Cache Coherency on Cortex-M55 Secure

The D-cache coherency problem was the single most pervasive technical challenge, affecting every DMA-capable peripheral. The root cause: **MVA-based cache maintenance operations fail silently on Cortex-M55 in Secure state** at certain buffer alignments. This is likely a silicon erratum not documented in ES0620.

The universal workaround (`stm32_dcache.h:stm32_dcache_clean_invalidate()`) uses set/way `DCCISW` operations, which are slower (full cache flush) but reliable. This was applied to:
- XSPI2 flash DMA reads
- ADC1 DMA conversion results
- SPI5 DMA TX flush + RX invalidate
- USB OTG DMA buffers

### 10.3 CACHEAXI State Persistence

The NPU's CACHEAXI cache **persists across SWD soft resets**. After running NPU inference, a power cycle is required before the next boot — otherwise, stale CACHEAXI entries intercept XSPI2 reads and return cached data from the previous session. This is documented as pitfall #33 in CLAUDE.md.

### 10.4 Compiler Flag: `+nomve`

The GCC flag `-mcpu=cortex-m55+nomve` is critical. Without `+nomve`, GCC generates MVE (Helium) SIMD instructions that fault on this chip (MVE is optional in the Cortex-M55 design and not implemented on the STM32N6). This manifests as cryptic UsageFault exceptions in library functions like `memcpy` or `memset`.

---

## 11. Quantitative Summary

### 11.1 Code Volume

| Component | Files | Lines of Code |
|-----------|-------|---------------|
| NuttX chip layer (`arch/arm/src/stm32n6/`) | 37 .c + 47 .h = 84 | 53,864 |
| — Hardware register headers | 47 .h | 6,911 |
| — Driver source code | 37 .c | 46,953 |
| NuttX board layer (`boards/arm/stm32n6/`) | 13 .c | 2,649 |
| PX4 board (`boards/stm/n6-nucleo/`) | — | 69,878 (incl. generated NPU model) |
| PX4 platform (`platforms/.../stm32n6/`) | — | 1,057 |
| PX4 NPU module (`src/modules/npu_inference/`) | 2 .c | 311 |

### 11.2 Largest Individual Drivers

| Driver | File | Lines |
|--------|------|-------|
| USB OTG HS | `stm32_otgdev.c` | 7,340 |
| SPI | `stm32_spi.c` | 2,405 |
| XSPI2 | `stm32_xspi.c` | 2,411 |
| DMA | `stm32_dma.c` | 1,070 |
| NPU (lower half) | `stm32_npu.c` | 479 |

### 11.3 Commit Statistics

| Repository | STM32N6 Commits | Insertions | Deletions | Duration |
|------------|-----------------|------------|-----------|----------|
| NuttX | 104 | 64,696 | 5,274 | 15 days |
| Top-level | 34 | ~1,020,000 | ~5,000 | 3 months |
| PX4 | 6 | 72,639 | 126 | 2 days |

### 11.4 Peripheral Driver Summary

| Peripheral | Status | DMA | IRQ | Key Commit |
|------------|--------|-----|-----|------------|
| USART1 | Working | No | 75 | `b9b9624c8d` |
| GPIO (LED/Button) | Working | — | — | `48f7abe0a1` |
| TIM2 (SysTick) | Working | — | 132 | `6bac9a6b7f` |
| GPDMA1 | Working | — | 84-99 | `1374b0f831` |
| HPDMA1 | Working | — | 68-83 | `8234ad7e56` |
| SPI5 | Working | DMA+Poll | — | `ede77155b2` |
| USB OTG HS | Working (PIO) | No* | 177 | `43f653534f` |
| I2C1/I2C2 | Working | No | 100-103 | `f47482a4fb` |
| ADC1 | Working | DMA | 62 | `bf7b24d7a0` |
| PWM (TIM3/4) | Working | No | — | `708934b23d` |
| RTC | Working | — | — | `4446635b82` |
| RNG | Working | — | 180 | `1ff72bc47d` |
| IWDG | Working | — | — | `1ff72bc47d` |
| XSPI2 (flash) | Working | No* | 143 | `5a08b651c7` |
| NPU (ATON) | Working | — | 53-56 | `fee9573f1a` |
| CACHEAXI | Working | — | 57 | `1c73c64e92` |

*DMA implemented but reverted due to cache bugs; polling mode used.

---

## 12. Timeline Visualization

```
Dec 26 ──── Project inception (FSBL, HAL reference, STATUS.md)
  │         [~2.5 months gap — planning, hardware study]
  │
Mar 11 ──── Phase 1: Initial chip support (8,408 lines)
  │           └─ USART1 console, SRAM boot, first printf
  │
Mar 12 ──── Phase 2: Clock + core peripherals (9 commits)
  │           ├─ PLL1 → 600 MHz CPU
  │           ├─ I-cache + D-cache
  │           ├─ TIM2 system timer
  │           ├─ GPIO LED/button
  │           └─ WFI sleep ✓ → NSH shell fully operational
  │
Mar 13 ──── Phase 4 begins: DMA + MPU + SPI + linker fix (12 commits)
  │           ├─ GPDMA1 + HPDMA1 dual-engine DMA
  │           ├─ MPU Write-Back region
  │           ├─ SPI5 polling mode
  │           └─ Critical _eronly linker bug fix
  │
Mar 14 ──── USB + I2C (6 commits)
  │           ├─ USB OTG HS with CDC/ACM (7,340 lines)
  │           ├─ PHY TX fix (AHB5 reset sequence)
  │           └─ I2C driver (I2C1 + I2C2)
  │
Mar 15 ──── ADC + PWM + RTC (5 commits)
  │
Mar 16 ──── RNG + IWDG + XSPI2 flash + littlefs (5 commits)
  │           └─ MX25UM51245G 64 MB, SPI 1-1-1 @ 25 MHz
  │
Mar 17 ──── NPU bring-up + 800 MHz (5 commits)
  │           ├─ ATON fabric init, STRENG memcpy PASS
  │           ├─ PLL2 @ 1 GHz, PLL3 @ 900 MHz
  │           ├─ SMPS overdrive + VOS SCALE0 → 800 MHz CPU
  │           └─ CACHEAXI enable + invalidate
  │
Mar 18 ──── First NPU inference! (6 commits)
  │           ├─ INT8 Conv2D: 4 pure HW epochs ✓
  │           ├─ XSPI2 memory-mapped mode toggle
  │           └─ LL_ATON compat shim + model tooling
  │
Mar 19 ──── NPU /dev/npu0 kernel driver (2 commits)
  │           └─ AIE framework upper/lower-half architecture
  │
Mar 20 ──── TinyYOLOv2 + OPI + DCCISW fix (9 commits)
  │           ├─ TinyYOLOv2 people detection model
  │           ├─ OPI STR 8-8-8 @ 100 MHz
  │           ├─ Universal DCCISW cache fix
  │           └─ USB-to-NPU host inference script
  │
Mar 21 ──── OPI DTR 400 MB/s + USB DMA (8 commits)
  │           ├─ OPI DTR 8-8-8 @ 200 MHz (380x speedup!)
  │           ├─ USB descriptor DMA → buffer DMA
  │           └─ VDDIO 1.8V for high-speed IO
  │
Mar 22-23 ── USB DMA debugging marathon (14 commits)
  │           ├─ USBRST handler rewrite
  │           ├─ EP0 state machine fixes
  │           ├─ Bulk OUT DMA cache coherency
  │           └─ Final: revert to PIO mode (stable)
  │
Mar 24 ──── YOLOX-Nano + YOLOv8n models (7 commits)
  │           ├─ SW epoch support
  │           ├─ FSBL copy size increase (256 KB → 1 MB)
  │           └─ Webcam inference host scripts
  │
Mar 25 ──── YOLOv8n async + PX4 integration (8 commits)
  │           ├─ RunEpochBlock API, async OSAL
  │           ├─ PX4 board support (58 files, 4,495 lines)
  │           ├─ NuttX submodule integration
  │           └─ PX4 build/flash targets in Makefile
  │
Mar 26 ──── PX4 refinement (3 commits)
              ├─ HRT rewrite (TIM5 reference impl)
              ├─ NPU inference module (34 ms, 29 FPS)
              ├─ CPU load measurement fix
              └─ FSBL SP limit raise for PX4+NPU binary
```

---

## 13. Lessons Learned

### 13.1 Hardware-Specific Discoveries

1. **Undocumented register locking**: CFGR1/CFGR2 lock-after-write behavior required single-write strategies not described in the reference manual
2. **Silicon errata beyond ES0620**: The Cortex-M55 Secure state MVA cache bug and MEMFAULTENA interference are not in the official errata sheet
3. **CACHEAXI is a system-level resource**: Its state persists across resets and affects all AXI traffic, not just NPU — requiring careful lifecycle management
4. **Boot ROM sets security state**: MSPLIM/PSPLIM from boot ROM must be cleared before any stack use; this is an ARMv8-M architectural requirement not specific to STM32

### 13.2 Porting Strategy Insights

1. **Bottom-up works**: Starting with UART → clocks → timers → DMA → peripherals built a solid foundation for each subsequent driver
2. **Reference implementations are essential**: The ST HAL drivers, Zephyr RTOS port, and NuttX STM32H5 port each provided crucial register-level details
3. **Cache coherency dominates debugging time**: Of 104 NuttX commits, approximately 25 (24%) were cache-related fixes
4. **DMA is optional but cache-awareness is not**: Several peripherals reverted from DMA to polling mode, but the D-cache coherency infrastructure remained essential

### 13.3 Development Velocity

The 104 NuttX commits in 15 days (averaging ~7 commits/day) with 64,696 lines of insertions demonstrates that AI-assisted development with persistent project context (via CLAUDE.md) can significantly accelerate embedded systems porting work. The most productive single day was March 13 (12 commits: DMA, MPU, SPI, linker fix).

---

## 14. Appendices

### Appendix A: Complete NuttX STM32N6 Chip Layer Source Files

| File | Purpose | Lines |
|------|---------|-------|
| `stm32_start.c` | Boot sequence, MSPLIM/PSPLIM, FPU, VTOR | — |
| `stm32n6xx_rcc.c` | PLL/clock configuration | — |
| `stm32_rcc.c` | RCC helper functions | — |
| `stm32_serial.c` | USART driver (interrupt-driven) | — |
| `stm32_lowputc.c` | Early console output | — |
| `stm32_gpio.c` | GPIO configuration | — |
| `stm32_irq.c` | NVIC interrupt controller | — |
| `stm32_idle.c` | WFI idle loop | — |
| `stm32_allocateheap.c` | Heap memory manager | — |
| `stm32_timerisr.c` | TIM2 system timer | — |
| `stm32_dma.c` | GPDMA1 + HPDMA1 DMA driver | 1,070 |
| `stm32_mpuinit.c` | MPU region configuration | — |
| `stm32_spi.c` | SPI driver (polling + DMA) | 2,405 |
| `stm32_otgdev.c` | USB OTG HS device mode | 7,340 |
| `stm32_i2c.c` | I2C driver | — |
| `stm32_adc.c` | ADC driver | — |
| `stm32_pwm.c` | PWM timer driver | — |
| `stm32_rtc.c` | RTC driver | — |
| `stm32_rtc_lowerhalf.c` | RTC NuttX lower half | — |
| `stm32_iwdg.c` | Independent watchdog | — |
| `stm32_rng.c` | Random number generator | — |
| `stm32_xspi.c` | XSPI2 flash driver (SPI/OPI/DTR) | 2,411 |
| `stm32_pwr.c` | Power management | — |
| `stm32_lsi.c` | LSI oscillator | — |
| `stm32_npu.c` | NPU AIE lower-half driver | 479 |
| `stm32_aton_hw.c` | ATON register programming | 19 |
| `stm32_npu_model.c` | Generated model wrapper | — |
| `stm32_mcu_cache.c` | D-cache compat for NPU runtime | — |
| `stm32_ll_aton.c` | LL_ATON runtime (ST library) | — |
| `stm32_ll_aton_runtime.c` | LL_ATON runtime core | — |
| `stm32_ll_aton_lib.c` | LL_ATON library | — |
| `stm32_ll_aton_lib_sw.c` | LL_ATON SW epoch support | — |
| `stm32_ll_aton_cipher.c` | LL_ATON cipher support | — |
| `stm32_ll_aton_util.c` | LL_ATON utilities | — |
| `stm32_ll_aton_osal_nuttx.c` | LL_ATON NuttX OSAL | — |
| `stm32_ll_sw_float.c` | SW float operators | — |
| `stm32_ll_sw_integer.c` | SW integer operators | — |

### Appendix B: Complete NuttX Board Layer Source Files

| File | Purpose |
|------|---------|
| `stm32_boot.c` | Early board init |
| `stm32_bringup.c` | Peripheral registration |
| `stm32_appinit.c` | Application init |
| `stm32_autoleds.c` | Automatic LED control |
| `stm32_userleds.c` | User LED API |
| `stm32_buttons.c` | Button driver |
| `stm32_spi.c` | SPI board config |
| `stm32_adc.c` | ADC board config |
| `stm32_pwm.c` | PWM board config |
| `stm32_xspi.c` | XSPI2 board config |
| `stm32_npu.c` | NPU HW init (RAMCFG/RIFSC/ATON/CACHEAXI) |
| `stm32_usbdev.c` | USB suspend stub |
| `stm32_dmatest.c` | DMA validation test |

### Appendix C: PX4 STM32N6 Commit Log

| Hash | Date | Subject |
|------|------|---------|
| `0a7620690c` | Mar 25 | feat(boards): add STM32N6 NUCLEO-N657X0-Q board support |
| `c8240c08be` | Mar 25 | feat(nuttx): update NuttX submodule with STM32N6 port |
| `23ebd84441` | Mar 25 | fix(ekf2): allow heading control on ground before in-flight mag alignment |
| `268d89faca` | Mar 26 | fix(stm32n6): rewrite HRT to match reference impl, fix NULL tcb crashes |
| `b9c74bfc38` | Mar 26 | feat(stm32n6): NPU inference module, accurate CPU load, top fix |
| `c63b82464f` | Mar 26 | feat(nuttx): update submodule with NPU + cpuload fix |

### Appendix D: Performance Benchmarks

| Metric | Value |
|--------|-------|
| CPU clock | 800 MHz (Cortex-M55) |
| NPU clock | 1 GHz (ATON core) |
| NPU SRAM clock | 900 MHz |
| Flash bandwidth (SPI 1-1-1) | ~3 MB/s |
| Flash bandwidth (OPI STR) | ~100 MB/s |
| Flash bandwidth (OPI DTR) | ~400 MB/s |
| YOLOv8n inference | 33 ms (29 FPS) |
| USB CDC/ACM throughput | 6 MB/s (PIO mode) |
| Weight load time (16 MB, DTR) | 90 ms |
| Weight load time (16 MB, SPI) | 34.5 s |

### Appendix E: Key Reference Documents

| Document | ID | Purpose |
|----------|----|---------|
| Reference Manual | RM0486 | Register-level MCU documentation |
| Board User Manual | UM3417 | NUCLEO-N657X0-Q schematic and layout |
| Errata | ES0620 | Known silicon issues |
| Cortex-M55 Errata | AT633/AT634 | ARM core errata notices |
| Programming Manual | PM0273 | Cortex-M55 instruction set and system control |
| Boot ROM Guide | UM3234 | STM32N6 boot ROM behavior |
| Datasheet | STM32N657A0 | Electrical characteristics and pinout |
