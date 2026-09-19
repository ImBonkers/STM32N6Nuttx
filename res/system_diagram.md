# STM32N6 + NuttX + PX4 System Architecture

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                              HOST PC / GROUND STATION                               │
│                                                                                     │
│  ┌──────────────┐    ┌──────────────┐    ┌───────────────────┐    ┌──────────────┐  │
│  │   QGroundC.  │    │   jMAVSim    │    │ STM32Cube         │    │  tio serial  │  │
│  │  (GCS GUI)   │◄──►│  (HIL sim)   │    │ Programmer CLI    │    │  /dev/ttyACM0│  │
│  └──────┬───────┘    └──────┬───────┘    └───────┬───────────┘    └──────┬───────┘  │
│         │  MAVLink          │ MAVLink            │ SWD                   │ 115200   │
│         └─────────┬─────────┘                    │                      │           │
│                   │ USB CDC/ACM 921600           │                      │           │
└───────────────────┼──────────────────────────────┼──────────────────────┼───────────┘
                    │                              │                      │
════════════════════╪══════════════════════════════╪══════════════════════╪════════════
  NUCLEO-N657X0-Q   │    CN8 Type-C               │ STLINK-V3EC          │ VCP
  Board             │                              │                      │
                    ▼                              ▼                      ▼
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                          STM32N657X0H3Q  (Cortex-M55 @ 800 MHz)                     │
│                          ARMv8.1-M Mainline · TrustZone (Secure state)              │
│                                                                                     │
│  ┌─── CPU Core ──────────────────────────────────────────────────────────────────┐  │
│  │                                                                               │  │
│  │  Cortex-M55 (+nomve)     I-Cache 32KB        D-Cache 32KB (Write-Back)        │  │
│  │  FPv5-D16 FPU            ┌──────────┐        ┌──────────┐                     │  │
│  │  hard-float ABI          │ enabled  │        │ enabled  │  MPU Region 0:      │  │
│  │                          └──────────┘        └──────────┘  AXISRAM 4MB WB     │  │
│  │  MSPLIM/PSPLIM cleared via --wrap=__start                  (MEMFAULTENA OFF)  │  │
│  │                                                                               │  │
│  │  SysTick: UNUSED (stops in CSLEEP) ──► TIM2 IRQ 132 = NuttX system timer     │  │
│  │                                    ──► TIM5 IRQ 174 = PX4 HRT (1 MHz)        │  │
│  └───────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                     │
│  ┌─── Clock Tree ────────────────────────────────────────────────────────────────┐  │
│  │                                                                               │  │
│  │  HSI 64 MHz ──► PLL1 (M=2,N=25) ──► VCO 800 MHz (SMPS overdrive+SCALE0)     │  │
│  │                    │                                                          │  │
│  │                    ├──► IC1  /1 = 800 MHz ──► CPUSW ──► CPU                   │  │
│  │                    ├──► IC2  /2 = 400 MHz ──► SYSSW ──► System               │  │
│  │                    ├──► IC3  /4 = 200 MHz ──► XSPI2 clock                    │  │
│  │                    ├──► IC6  /3 = 267 MHz ──► AHB buses                      │  │
│  │                    └──► IC11 /2 = 400 MHz ──► APB buses                      │  │
│  │                                                                               │  │
│  │  HSI 64 MHz ──► PLL2 ──► IC6  = 1000 MHz ──► NPU core clock                 │  │
│  │  HSI 64 MHz ──► PLL3 ──► IC11 =  900 MHz ──► NPU SRAM clock                 │  │
│  │                                                                               │  │
│  │  HPRE=/2 ──► HCLK 200 MHz    USART1 kernel = HSI 64 MHz (independent)       │  │
│  │  RCC: must use ENSR (set registers), CFGR1+CFGR2 lock after first write     │  │
│  └───────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                     │
│  ┌─── Memory Map ────────────────────────────────────────────────────────────────┐  │
│  │                                                                               │  │
│  │  0x34000000 ┌─────────────────────┐                                           │  │
│  │             │ 0x400 reserved      │ Boot ROM / vector table header            │  │
│  │  0x34000400 ├─────────────────────┤◄── NuttX/PX4 _stext (VTOR)               │  │
│  │             │                     │                                           │  │
│  │             │   AXISRAM 1+2       │  .text + .rodata + .data + .bss + heap    │  │
│  │             │   ~2 MB             │  NuttX kernel + PX4 modules               │  │
│  │             │  (FLEXMEM+SRAM1+2)  │                                           │  │
│  │  0x34200000 ├─────────────────────┤◄── NPU boundary                           │  │
│  │             │                     │                                           │  │
│  │             │   AXISRAM 3-6       │  NPU activations + scratch                │  │
│  │             │   ~2.8 MB           │  (4 × 448 KB banks)                       │  │
│  │             │                     │                                           │  │
│  │  0x34400000 └─────────────────────┘                                           │  │
│  │                                                                               │  │
│  │  0x70000000 ┌─────────────────────┐  MX25UM51245G 64 MB                       │  │
│  │             │ 128 KB  FSBL        │  via XSPI2 (OPI DTR 8-8-8 @ 200 MHz)     │  │
│  │  0x70020000 ├─────────────────────┤  = 400 MB/s reads                         │  │
│  │             │ 1.375 MB  NuttX/PX4 │  (copied to SRAM by FSBL at boot)        │  │
│  │  0x70180000 ├─────────────────────┤                                           │  │
│  │             │ 16 MB  DNN weights  │  YOLOv8n (memory-mapped, CACHEAXI)       │  │
│  │  0x71000000 ├─────────────────────┤                                           │  │
│  │             │ NPU test weights    │                                           │  │
│  │  0x71120000 ├─────────────────────┤                                           │  │
│  │             │ 47 MB  littlefs     │  /mnt/lfs (if enabled)                    │  │
│  │  0x74000000 └─────────────────────┘                                           │  │
│  └───────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                     │
│  ┌─── Peripherals (Secure aliases 0x5xxxxxxx) ──────────────────────────────────┐  │
│  │                                                                               │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐       │  │
│  │  │ USART1   │  │ USB1 OTG │  │  SPI5    │  │  I2C1/2  │  │  ADC1    │       │  │
│  │  │ PE5/PE6  │  │  HS      │  │ PE15/    │  │ I2C2:    │  │ PC0/PA4  │       │  │
│  │  │ AF7      │  │ DWC2     │  │ PG1/PG2  │  │ TCPP0203 │  │ SW trig  │       │  │
│  │  │ 115200   │  │ v4.11a   │  │ AF5      │  │ @0x34    │  │ int-drv  │       │  │
│  │  │ IRQ 75   │  │ CDC/ACM  │  │ poll+DMA │  │ IRQ      │  │ IRQ 62   │       │  │
│  │  │ FIFO 8B  │  │ PIO mode │  │          │  │ 100-103  │  │          │       │  │
│  │  │          │  │ 6 MB/s   │  │          │  │          │  │          │       │  │
│  │  │          │  │ IRQ 177  │  │          │  │          │  │          │       │  │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘  └──────────┘       │  │
│  │                                                                               │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐       │  │
│  │  │ TIM1 PWM │  │  TIM2    │  │  TIM5    │  │  RTC     │  │  XSPI2   │       │  │
│  │  │ PE9 out  │  │ NuttX    │  │ PX4 HRT  │  │ LSE osc  │  │ MX25UM51 │       │  │
│  │  │          │  │ systimer │  │ 1 MHz    │  │ /dev/    │  │ 64 MB    │       │  │
│  │  │          │  │ IRQ 132  │  │ IRQ 174  │  │  rtc0    │  │ OPI DTR  │       │  │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘  └──────────┘       │  │
│  │                                                                               │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐                      │  │
│  │  │  RNG     │  │  IWDG    │  │ GPDMA1   │  │ HPDMA1   │                      │  │
│  │  │ CONDRST  │  │ LSI      │  │ 16 ch    │  │ 16 ch    │                      │  │
│  │  │ AHB3     │  │ watchdog │  │ 8B FIFO  │  │ 16/64B   │                      │  │
│  │  │ IRQ 180  │  │          │  │ IRQ 84-99│  │ IRQ 68-83│                      │  │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘                      │  │
│  │                                                                               │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐                                    │  │
│  │  │ GPIO     │  │  PWR     │  │  RIFSC   │  Security: SAU disabled (all       │  │
│  │  │ PG0 grn  │  │ VddIO2-5│  │ per-periph│  Secure), RIFSC per-periph        │  │
│  │  │ PG8 blu  │  │ SMPS OD  │  │ SEC/PRIV │  SEC/PRIV, RCC internal RIF       │  │
│  │  │ PC13 btn │  │ VOS SCL0│  │ AHB3     │  → use ENSR set-registers          │  │
│  │  └──────────┘  └──────────┘  └──────────┘                                    │  │
│  └───────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                     │
│  ┌─── NPU: Neural-ART Accelerator (ATON) ───────────────────────────────────────┐  │
│  │                                                                               │  │
│  │  Base: 0x580E0000 (Secure)        IRQs: NPU0-3 = 53-56, CACHEAXI = 57       │  │
│  │  Core: 1 GHz (PLL2→IC6)          SRAM: 900 MHz (PLL3→IC11)                  │  │
│  │                                                                               │  │
│  │  ┌────────────┐   ┌────────────┐   ┌────────────────┐   ┌────────────────┐   │  │
│  │  │  STRENG    │   │  ATON      │   │   CACHEAXI     │   │  SRAM 3-6      │   │  │
│  │  │  engines   │   │  fabric    │   │   0x580DFC00   │   │  2.8 MB        │   │  │
│  │  │  (DMA)     │──►│  CTRL/     │──►│   WB cache     │──►│  activations   │   │  │
│  │  │            │   │  gates/    │   │   for XSPI2    │   │  CID_CACHE=    │   │  │
│  │  │  SE0/SE1   │   │  BUSIF     │   │   (toggled per │   │  0x01 (non-    │   │  │
│  │  │            │   │            │   │    inference)   │   │  cacheable)    │   │  │
│  │  └────────────┘   └────────────┘   └────────────────┘   └────────────────┘   │  │
│  │                                         │                                     │  │
│  │  Model: YOLOv8n 192×192 INT8            │ memory-mapped                       │  │
│  │  4 HW epochs, 34 ms @ 29 FPS            ▼                                    │  │
│  │  Weights at 0x71000000 ◄──────── XSPI2 (OPI DTR 400 MB/s)                   │  │
│  │                                                                               │  │
│  └───────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                     │
└─────────────────────────────────────────────────────────────────────────────────────┘

════════════════════════════════════════════════════════════════════════════════════════
                              SOFTWARE STACK
════════════════════════════════════════════════════════════════════════════════════════

┌─────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                     │
│                              PX4 AUTOPILOT                                          │
│                                                                                     │
│  ┌─── Estimation ────┐  ┌─── Control ─────────────┐  ┌─── Navigation ───────────┐  │
│  │                    │  │                          │  │                          │  │
│  │  ┌──────────────┐  │  │  ┌────────────────────┐  │  │  ┌──────────────────┐   │  │
│  │  │    EKF2      │  │  │  │  commander         │  │  │  │   navigator      │   │  │
│  │  │  (attitude/  │  │  │  │  (state machine,   │  │  │  │   (waypoints,    │   │  │
│  │  │   position)  │  │  │  │   arming, mode)    │  │  │  │    RTL, land)    │   │  │
│  │  └──────┬───────┘  │  │  └────────┬───────────┘  │  │  └──────────────────┘   │  │
│  │         │ uORB     │  │           │ uORB         │  │                          │  │
│  │  ┌──────▼───────┐  │  │  ┌────────▼───────────┐  │  │  ┌──────────────────┐   │  │
│  │  │   sensors    │  │  │  │ flight_mode_mgr    │  │  │  │  land_detector   │   │  │
│  │  │  (-h = HIL)  │  │  │  │                    │  │  │  │  (multicopter)   │   │  │
│  │  └──────────────┘  │  │  └────────────────────┘  │  │  └──────────────────┘   │  │
│  └────────────────────┘  │                          │  └──────────────────────────┘  │
│                          │  ┌────────────────────┐  │                                │
│                          │  │ mc_rate_control    │  │  ┌─── Infrastructure ───────┐  │
│                          │  └────────┬───────────┘  │  │                          │  │
│                          │  ┌────────▼───────────┐  │  │  ┌──────────────────┐   │  │
│                          │  │ mc_att_control     │  │  │  │   mavlink        │   │  │
│                          │  └────────┬───────────┘  │  │  │  /dev/ttyACM0    │   │  │
│                          │  ┌────────▼───────────┐  │  │  │  921600 baud     │   │  │
│                          │  │ mc_pos_control     │  │  │  │  20 KB/s         │   │  │
│                          │  └────────┬───────────┘  │  │  └──────────────────┘   │  │
│                          │  ┌────────▼───────────┐  │  │  ┌──────────────────┐   │  │
│                          │  │ mc_hover_thrust    │  │  │  │   load_mon       │   │  │
│                          │  └────────┬───────────┘  │  │  │   (cpuload via   │   │  │
│                          │  ┌────────▼───────────┐  │  │  │    NuttX C wrap) │   │  │
│                          │  │ control_allocator  │  │  │  └──────────────────┘   │  │
│                          │  └────────┬───────────┘  │  │  ┌──────────────────┐   │  │
│                          │  ┌────────▼───────────┐  │  │  │   dataman        │   │  │
│                          │  │ pwm_out_sim (HIL)  │  │  │  │  (RAM-backed,    │   │  │
│                          │  │  heartbeat output  │  │  │  │   no SD card)    │   │  │
│                          │  └────────────────────┘  │  │  └──────────────────┘   │  │
│                          └──────────────────────────┘  └──────────────────────────┘  │
│                                                                                     │
│  ┌─── NPU Module ────────────────────────────────────────────────────────────────┐  │
│  │                                                                               │  │
│  │  npu_inference  ──► /dev/npu0 ──► LL_ATON runtime ──► ATON HW epochs         │  │
│  │  (PX4 module)       (NuttX AIE)   (STEdgeAI v3.0)     (YOLOv8n INT8)        │  │
│  │                                                                               │  │
│  │  34 ms inference · 29 FPS · sched_yield() during HW epochs                   │  │
│  │  MAVLink: NAMED_VALUE_FLOAT { npu_ms, npu_fps, npu_avg }                     │  │
│  │  Runtime: "npu duty <0-100>" for CPU/latency tradeoff                        │  │
│  └───────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                     │
│  ┌─── uORB (Pub/Sub Message Bus) ───────────────────────────────────────────────┐  │
│  │                                                                               │  │
│  │  sensor_combined ◄── sensors          vehicle_attitude ◄── EKF2              │  │
│  │  vehicle_command ◄── commander        actuator_outputs ──► pwm_out_sim       │  │
│  │  vehicle_status  ◄── commander        vehicle_local_position ◄── EKF2        │  │
│  │  (HIL_SENSOR → sensor_combined → EKF2 → attitude/position → controllers)     │  │
│  └───────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                     │
├─────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                     │
│                     APACHE NuttX RTOS (12.12.0)                                     │
│                                                                                     │
│  ┌─── Kernel Services ───────────────────────────────────────────────────────────┐  │
│  │                                                                               │  │
│  │  Scheduler          Memory Mgr       VFS              Networking              │  │
│  │  ┌──────────────┐   ┌──────────┐     ┌──────────┐    ┌──────────┐            │  │
│  │  │ SCHED_RR     │   │ Flat     │     │ /dev/    │    │ (unused) │            │  │
│  │  │ USEC_PER_    │   │ build    │     │  ttyS0   │    │          │            │  │
│  │  │ TICK=1000    │   │ no MMU   │     │  ttyACM0 │    └──────────┘            │  │
│  │  │ (PX4: 10us) │   │ heap in  │     │  npu0    │                             │  │
│  │  │              │   │ AXISRAM  │     │  rtc0    │    Signals    POSIX         │  │
│  │  │ CPULOAD via  │   │          │     │  random  │    Semaphores Timers        │  │
│  │  │ SYSCLK       │   │          │     │  leds/   │    Mutexes   pthreads      │  │
│  │  └──────────────┘   └──────────┘     │  buttons/│                             │  │
│  │                                      │  adc0    │                             │  │
│  │  WFI/CSLEEP:                         │  pwm0    │                             │  │
│  │  BSECEN + LPEN registers configured  │  spi5    │                             │  │
│  │  TIM2 wakes from sleep (not SysTick) │  i2c1/2  │                             │  │
│  │                                      └──────────┘                             │  │
│  └───────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                     │
│  ┌─── Device Drivers (arch/arm/src/stm32n6/) ───────────────────────────────────┐  │
│  │                                                                               │  │
│  │  stm32_serial.c ── USART1 interrupt-driven, FIFO 8B, HSI 64MHz kernel clk   │  │
│  │  stm32_otgdev.c ── USB OTG HS (DWC2), PIO mode, CDC/ACM, 9 EPs             │  │
│  │  stm32_spi.c ───── SPI5 polling + DMA (aligned bounce buffers)              │  │
│  │  stm32_i2c.c ───── I2C1/I2C2 interrupt mode                                 │  │
│  │  stm32_adc.c ───── ADC1 software-triggered, interrupt-driven                │  │
│  │  stm32_pwm.c ───── TIM1/TIM3/TIM4 PWM output                               │  │
│  │  stm32_rtc.c ───── RTC (LSE), ICSR/SR/SCR registers                         │  │
│  │  stm32_rng.c ───── RNG with CONDRST init                                    │  │
│  │  stm32_iwdg.c ──── Independent watchdog (LSI)                               │  │
│  │  stm32_xspi.c ──── XSPI2 MX25UM51245G, OPI DTR 8-8-8 + memory-mapped      │  │
│  │  stm32_dma.c ───── GPDMA1 (16ch) + HPDMA1 (16ch), type-aware allocator     │  │
│  │  stm32_gpio.c ──── GPIO with SYSCFG compensation cells (errata ES0620)      │  │
│  │  stm32_npu.c ───── NPU/ATON AIE framework driver (/dev/npu0)               │  │
│  │  stm32_pwr.c ───── VddIO2-5 enable, SMPS overdrive, VOS SCALE0             │  │
│  │  stm32_mpuinit.c ─ MPU: AXISRAM 4MB WB, PRIVDEFENA, no MEMFAULTENA         │  │
│  │                                                                               │  │
│  └───────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                     │
│  ┌─── Boot Sequence (stm32_start.c) ────────────────────────────────────────────┐  │
│  │                                                                               │  │
│  │  Power On ──► Boot ROM ──► FSBL (0x70000000) ──► NuttX (0x34000400)          │  │
│  │                               │                      │                        │  │
│  │                               ▼                      ▼                        │  │
│  │                    ┌──────────────────┐   ┌────────────────────────────┐      │  │
│  │                    │ FSBL:            │   │ NuttX __start:             │      │  │
│  │                    │ 1. SMPS overdrive│   │ 0. Clear MSPLIM/PSPLIM    │      │  │
│  │                    │ 2. VOS SCALE0    │   │ 1. Disable SysTick        │      │  │
│  │                    │ 3. PLL1→800MHz   │   │ 2. Set VTOR               │      │  │
│  │                    │ 4. XSPI2 init    │   │ 3. Enable FPU             │      │  │
│  │                    │ 5. Copy NuttX    │   │ 4. PWR: VddIO2-5          │      │  │
│  │                    │    → SRAM        │   │ 5. SYSCFG compensation    │      │  │
│  │                    │ 6. Jump          │   │ 6. Skip PLL (FSBL did it) │      │  │
│  │                    └──────────────────┘   │ 7. LPEN for WFI           │      │  │
│  │                                           │ 8. GPIO/USART clocks      │      │  │
│  │                                           │ 9. nx_start() ──► NSH     │      │  │
│  │                                           └────────────────────────────┘      │  │
│  └───────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                     │
├─────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                     │
│                       PX4 PLATFORM HAL (stm32n6/)                                   │
│                                                                                     │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐            │
│  │  hrt.c       │  │  adc.cpp     │  │  board_hw_   │  │  board init  │            │
│  │  TIM5 1MHz   │  │  ADC wrapper │  │  info.c      │  │  XSPI2 mmap │            │
│  │  periodic +  │  │              │  │  MCU version │  │  NPU HW init │            │
│  │  oneshot     │  │              │  │              │  │  LED/button   │            │
│  │  LPEN for    │  │              │  │              │  │  USB CDC init │            │
│  │  WFI         │  │              │  │              │  │              │            │
│  └──────────────┘  └──────────────┘  └──────────────┘  └──────────────┘            │
│                                                                                     │
│  ROMFS: hardcoded params (no SD card / persistent storage)                          │
│  HIL config: SYS_HITL=1, SYS_AUTOSTART=1001 (Iris quad)                            │
│                                                                                     │
└─────────────────────────────────────────────────────────────────────────────────────┘

════════════════════════════════════════════════════════════════════════════════════════
                           DATA FLOW: HIL + NPU
════════════════════════════════════════════════════════════════════════════════════════

  jMAVSim                    STM32N6                              ATON NPU
  ┌──────┐                  ┌───────────────────────────────┐    ┌──────────┐
  │      │  HIL_SENSOR      │                               │    │          │
  │ Sim  │ ────────────────►│  sensors → EKF2 → commander   │    │ YOLOv8n  │
  │ loop │  HIL_GPS         │     │         │        │       │    │ 192×192  │
  │      │  HIL_STATE_QUAT  │     ▼         ▼        ▼       │    │ INT8     │
  │      │                  │  flight_mode_mgr                │    │          │
  │      │                  │     │                           │    │ 4 HW     │
  │      │  HEARTBEAT       │     ▼                           │    │ epochs   │
  │      │ ◄────────────────│  rate → att → pos → hover      │    │          │
  │      │  HIL_ACTUATOR    │     │                           │    │ 34 ms    │
  │      │  _CONTROLS       │     ▼                           │    │          │
  │      │ ◄────────────────│  control_allocator              │    │          │
  │      │                  │     │                           │    │          │
  │      │                  │     ▼                           │    │          │
  │      │                  │  pwm_out_sim (HIL heartbeat)    │    │          │
  │      │                  │                               │    │          │
  │      │                  │  npu_inference ──► /dev/npu0 ──┼───►│ inference│
  │      │  NAMED_VALUE_    │       ◄── npu_ms, npu_fps ────┼────│          │
  │      │  FLOAT           │                               │    │          │
  └──────┘                  └───────────────────────────────┘    └──────────┘
       │                              │
       │         USB CDC/ACM          │
       └──────────────────────────────┘
         MAVLink @ 921600 baud

════════════════════════════════════════════════════════════════════════════════════════
                              BUS ARCHITECTURE
════════════════════════════════════════════════════════════════════════════════════════

                        ┌─────────────────┐
                        │   Cortex-M55    │
                        │   800 MHz       │
                        └────────┬────────┘
                                 │
                    ┌────────────┼────────────┐
                    │            │            │
               ┌────▼────┐ ┌────▼────┐ ┌────▼────┐
               │  AHB5   │ │  AHB4   │ │  AHB3   │
               │ 267 MHz │ │ 267 MHz │ │ 267 MHz │
               └────┬────┘ └────┬────┘ └────┬────┘
                    │            │            │
          ┌─────┬──┴──┐    ┌───┴───┐    ┌───┴───┐
          │     │     │    │       │    │       │
        USB1  HPDMA1 XSPI2 PWR   GPIO  RIFSC  RNG
        OTG   (16ch)       SMPS  PORTA  (sec   RCC
        HS    +AXI         VOS   ...G   filter)
              clks
                    │            │            │
               ┌────▼────┐ ┌────▼────┐ ┌────▼────┐
               │  APB1   │ │  APB2   │ │  APB4   │
               │ 200 MHz │ │ 200 MHz │ │ 200 MHz │
               └────┬────┘ └────┬────┘ └────┬────┘
                    │            │            │
              ┌─────┼─────┐     │       ┌────┼────┐
              │     │     │     │       │         │
            TIM2  TIM3  TIM5 USART1  SYSCFG    BSEC
            (sys) (PWM) (HRT) (console)(comp    (WFI)
                               64MHz   cells)
                    │
               ┌────▼────┐
               │  AHB1   │
               │ 267 MHz │
               └────┬────┘
                    │
              ┌─────┼─────┐
              │     │     │
           GPDMA1  ADC1  SPI5
           (16ch)

                        ┌─────────────────┐
                        │    ATON NPU      │
                        │   1 GHz core     │
                        └────────┬────────┘
                                 │
                    ┌────────────┼────────────┐
                    │            │            │
               CACHEAXI     STRENG       SRAM 3-6
               (WB cache    engines      900 MHz
                for XSPI)   (DMA)        2.8 MB
```
