# STM32N6 Internals × NuttX × PX4: NPU Integration Architecture

```
═══════════════════════════════════════════════════════════════════════════════════════
                         PX4 APPLICATION LAYER
═══════════════════════════════════════════════════════════════════════════════════════

                              ┌───────────────────────────────────────────┐
                              │         PX4 uORB Message Bus              │
                              │     (lock-free pub/sub, shared memory)    │
                              │                                           │
  ┌─────────────────────┐     │  ┌──────────────┐    ┌───────────────┐   │
  │   npu_inference     │     │  │ debug_key_   │    │ sensor_       │   │
  │   module            │     │  │ value        │    │ combined      │   │
  │                     │────►│  │              │    │               │   │
  │  px4_task_spawn_cmd │     │  │  npu_ms      │    │  HIL_SENSOR   │   │
  │  SCHED_PRIORITY_    │     │  │  npu_fps     │    │  ──────────►  │   │
  │  DEFAULT - 10       │     │  │  npu_avg     │    │  accel/gyro/  │   │
  │  (lower than flight)│     │  │  npu_duty    │    │  mag/baro     │   │
  │                     │     │  └──────┬───────┘    └───────┬───────┘   │
  │  Duty cycle:        │     │         │                    │           │
  │  0%=paused          │     │         ▼                    ▼           │
  │  100%=continuous     │     │  ┌──────────────┐    ┌───────────────┐  │
  │  adaptive sleep     │     │  │   mavlink     │    │     EKF2      │  │
  │                     │     │  │ NAMED_VALUE_  │    │  attitude +   │  │
  └──────────┬──────────┘     │  │ FLOAT → GCS   │    │  position     │  │
             │                │  └──────────────┘    └───────┬───────┘  │
             │                │                              │          │
             │                │  ┌──────────────┐    ┌───────▼───────┐  │
             │                │  │  commander    │    │  mc_rate_     │  │
             │                │  │  arming/mode  │    │  control      │  │
             │                │  │  state machine│    │       │       │  │
             │                │  └──────────────┘    │  mc_att_ctrl  │  │
             │                │                      │       │       │  │
             │                │  ┌──────────────┐    │  mc_pos_ctrl  │  │
             │                │  │   load_mon    │    │       │       │  │
             │                │  │ clock_cpuload │    │  ctrl_alloc   │  │
             │                │  │ (C wrapper)   │    │       │       │  │
             │                │  └──────────────┘    │  pwm_out_sim  │  │
             │                │                      │  (HIL output) │  │
             │                │                      └───────────────┘  │
             │                └───────────────────────────────────────────┘
             │
             │  open("/dev/npu0", O_RDWR)
             │  ioctl(fd, AIE_CMD_LOAD, 0)              ◄── load model
             │  ioctl(fd, AIE_CMD_FEED_INPUT, input)     ◄── 192×192×3 INT8
             │  ioctl(fd, NPUIOC_RUN_SYNC, 0)           ◄── run inference
             │  ioctl(fd, AIE_CMD_GET_OUTPUT, output)    ◄── 756×5 INT8
             │
═════════════╪═════════════════════════════════════════════════════════════════════════
             │           POSIX SYSTEM CALL BOUNDARY (ioctl)
═════════════╪═════════════════════════════════════════════════════════════════════════
             │
             │                NuttX VFS
             │                  │
             ▼                  ▼
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                     │
│  NuttX AIE (AI Engine) Upper-Half Driver     drivers/aie/ai_engine.c               │
│                                                                                     │
│  ┌───────────────────────────────────────────────────────────────────────────────┐  │
│  │  struct aie_upperhalf_s                                                       │  │
│  │  ├── lower → (struct aie_lowerhalf_s *)     pointer to chip driver            │  │
│  │  └── lock  → mutex_t                        serializes all ioctl              │  │
│  │                                                                               │  │
│  │  file_operations g_aie_ops:                                                   │  │
│  │  ├── open()   → no-op                                                        │  │
│  │  ├── close()  → calls lower->ops->deinit()                                   │  │
│  │  └── ioctl()  → switch(cmd):                                                 │  │
│  │       │                                                                       │  │
│  │       ├── AIE_CMD_LOAD      → lower->ops->init(lower, model)                 │  │
│  │       ├── AIE_CMD_FEED_INPUT → lower->ops->feed_input(lower, id, input)      │  │
│  │       ├── AIE_CMD_GET_OUTPUT → lower->ops->get_output(lower, id, output)     │  │
│  │       └── NPUIOC_RUN_SYNC   → lower->ops->control(lower, id, cmd, arg)      │  │
│  │                                        │                                      │  │
│  │  register_driver("/dev/npu0", &g_aie_ops, 0666, upper)                       │  │
│  └────────────────────────────────┼──────────────────────────────────────────────┘  │
│                                   │                                                 │
│              ops vtable dispatch   │                                                 │
│                                   ▼                                                 │
│  ┌───────────────────────────────────────────────────────────────────────────────┐  │
│  │                                                                               │  │
│  │  NuttX STM32N6 NPU Lower-Half Driver     arch/arm/src/stm32n6/stm32_npu.c   │  │
│  │                                                                               │  │
│  │  struct stm32_npu_s                                                           │  │
│  │  ├── lower.ops = &g_npu_ops        ◄── cast-compatible with aie_lowerhalf_s  │  │
│  │  ├── epoch_blocks                   ◄── LL_ATON model epoch list              │  │
│  │  ├── input_bufs / output_bufs       ◄── SRAM buffer descriptors               │  │
│  │  ├── nn_instance                    ◄── LL_ATON runtime state                 │  │
│  │  ├── initialized                                                              │  │
│  │  └── cacheaxi_held                  ◄── tracks XSPI2 mutex ownership          │  │
│  │                                                                               │  │
│  │  g_npu_ops = {                                                                │  │
│  │    .init       = stm32_npu_init        Load model, get epoch blocks           │  │
│  │    .deinit     = stm32_npu_deinit      Release model resources                │  │
│  │    .feed_input = stm32_npu_feed_input  memcpy input → SRAM activation buffer  │  │
│  │    .get_output = stm32_npu_get_output  memcpy SRAM output → user buffer       │  │
│  │    .control    = stm32_npu_control     NPUIOC_RUN_SYNC / NPUIOC_GET_INFO     │  │
│  │  }                                                                            │  │
│  │                                                                               │  │
│  │  ┌─ NPUIOC_RUN_SYNC (inference execution) ────────────────────────────────┐  │  │
│  │  │                                                                        │  │  │
│  │  │  1. Acquire XSPI2 mutex (stm32_xspi_mmap_lock)                        │  │  │
│  │  │  2. Enable CACHEAXI + full invalidate (for XSPI2 weight reads)        │  │  │
│  │  │  3. Run epoch loop:                                                    │  │  │
│  │  │       do {                                                             │  │  │
│  │  │         rv = LL_ATON_RT_RunEpochBlock(nn_instance)                     │  │  │
│  │  │         if (rv == WFE) → LL_ATON_OSAL_WFE()  ──────────────────┐      │  │  │
│  │  │       } while (rv != DONE)                                      │      │  │  │
│  │  │  4. LL_ATON_RT_Reset_Network()                                  │      │  │  │
│  │  │                                                                 │      │  │  │
│  │  └─────────────────────────────────────────────────────────────────┼──────┘  │  │
│  │                                                                    │         │  │
│  │  stm32_npu_initialize():                                          │         │  │
│  │    g_npu_dev.lower.ops = &g_npu_ops                               │         │  │
│  │    LL_ATON_Init()  ── initializes ATON fabric via ST SDK           │         │  │
│  │    return &g_npu_dev.lower                                        │         │  │
│  │                                                                    │         │  │
│  └────────────────────────────────────────────────────────────────────┼─────────┘  │
│                                                                       │            │
│  ┌────────────────────────────────────────────────────────────────────▼─────────┐  │
│  │                                                                              │  │
│  │  NuttX OSAL Layer                    npu/compat/ll_aton_osal_nuttx.c        │  │
│  │  (OS Abstraction — bridges LL_ATON runtime to NuttX scheduler)              │  │
│  │                                                                              │  │
│  │  LL_ATON_OSAL_WFE():                                                        │  │
│  │    ┌──────────────────────────────────────────────────────────────────────┐  │  │
│  │    │  1. Read wait_mask from current epoch block                         │  │  │
│  │    │  2. Poll STRENG[i] CTRL.RUNNING bit for each engine in mask         │  │  │
│  │    │  3. while (any engine running):                                     │  │  │
│  │    │       sched_yield()  ◄── NuttX scheduler runs flight tasks here     │  │  │
│  │    │  4. Acknowledge STRENG IRQ registers                                │  │  │
│  │    │  5. Set triggered_events (like HW ISR would)                        │  │  │
│  │    └──────────────────────────────────────────────────────────────────────┘  │  │
│  │                                                                              │  │
│  │  LL_ATON_OSAL_INSTALL_IRQ_FUNC(irq_line, handler):                          │  │
│  │    irq_attach(NPU_IRQ_BASE + irq_line + 16, npu_irq_handler)               │  │
│  │    up_enable_irq(...)                                                        │  │
│  │                                                                              │  │
│  │  LL_ATON_OSAL_SIGNAL_EVENT():                                                │  │
│  │    sem_post(&g_wfe_sem)                                                      │  │
│  │                                                                              │  │
│  └──────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                     │
│  ┌───────────────── Other NuttX Kernel Services ─────────────────────────────────┐  │
│  │                                                                               │  │
│  │  Scheduler            Timers              VFS            USB CDC/ACM          │  │
│  │  ┌──────────────┐    ┌──────────────┐    ┌──────────┐   ┌──────────────┐     │  │
│  │  │ SCHED_RR     │    │ TIM2 (NuttX) │    │ /dev/    │   │ /dev/ttyACM0 │     │  │
│  │  │              │    │ systick      │    │  npu0    │   │  DWC2 OTG HS │     │  │
│  │  │ sched_yield()│    │ IRQ 132      │    │  ttyS0   │   │  PIO mode    │     │  │
│  │  │ called by NPU│    │              │    │  ttyACM0 │   │  6 MB/s      │     │  │
│  │  │ OSAL during  │    │ TIM5 (PX4)   │    │  adc0    │   │              │     │  │
│  │  │ HW epoch     │    │ HRT 1MHz     │    │  rtc0    │   │  MAVLink     │     │  │
│  │  │ ──► flight   │    │ IRQ 174      │    │  spi5    │   │  transport   │     │  │
│  │  │ tasks run    │    │ hrt_absolute │    │  i2c1/2  │   │              │     │  │
│  │  └──────────────┘    │ _time()=rCNT │    │  random  │   └──────────────┘     │  │
│  │                      └──────────────┘    └──────────┘                         │  │
│  └───────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                     │
└─────────────────────────────────────────────────────────────────────────────────────┘

═══════════════════════════════════════════════════════════════════════════════════════
                     BOARD-LEVEL INITIALIZATION SEQUENCE
═══════════════════════════════════════════════════════════════════════════════════════

┌─────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                     │
│  Board Init     boards/arm/stm32n6/nucleo-n657x0-q/src/stm32_bringup.c            │
│                                                                                     │
│  stm32_bringup() called from board_late_initialize():                               │
│                                                                                     │
│  ┌─ stm32_npu_setup() ───────────────────────────────────────────────────────────┐  │
│  │                                                                               │  │
│  │  Phase 1: stm32_npu_hw_init()   [board/stm32_npu.c — no LL_ATON headers]    │  │
│  │  ┌─────────────────────────────────────────────────────────────────────────┐  │  │
│  │  │  1. RAMCFG: Power on SRAM3-6 (clear SRAMSD bit 20)                    │  │  │
│  │  │  2. RIFSC: RIMC_ATTR[1] = 0x310 (NPU master: CID=1, SEC, PRIV)       │  │  │
│  │  │  3. ATON fabric init:                                                  │  │  │
│  │  │     a. For each block: write CTRL.CLR, then CTRL.EN                    │  │  │
│  │  │     b. AGATES0/1/BGATES = 0xFFFFFFFF (open all gates)                 │  │  │
│  │  │     c. BUSIF0/1 enable, INTCTRL enable                                │  │  │
│  │  │  4. Disable SRAM interleaving (SYSCFG offset 0x78, bit 0 = 0)         │  │  │
│  │  │  5. CACHEAXI: enable CACHEAXIRAM clock → reset → invalidate → enable  │  │  │
│  │  │  6. STRENG memcpy self-test: SE0→STRSWITCH→SE1, 96 bytes              │  │  │
│  │  │     (CID_CACHE=0x01 non-cacheable for SRAM→SRAM)                      │  │  │
│  │  │  7. Disable CACHEAXI controller (avoid XSPI2 conflict)                │  │  │
│  │  └─────────────────────────────────────────────────────────────────────────┘  │  │
│  │                                                                               │  │
│  │  Phase 2: stm32_npu_initialize()  [chip/stm32_npu.c — has LL_ATON headers]  │  │
│  │  ┌─────────────────────────────────────────────────────────────────────────┐  │  │
│  │  │  1. g_npu_dev.lower.ops = &g_npu_ops                                  │  │  │
│  │  │  2. LL_ATON_Init() — ST SDK fabric/runtime init                        │  │  │
│  │  │  3. return &g_npu_dev.lower                                            │  │  │
│  │  └─────────────────────────────────────────────────────────────────────────┘  │  │
│  │                                                                               │  │
│  │  Phase 3: aie_register("/dev/npu0", lower)  [drivers/aie/ai_engine.c]        │  │
│  │  ┌─────────────────────────────────────────────────────────────────────────┐  │  │
│  │  │  1. Allocate aie_upperhalf_s, set upper->lower = lower                 │  │  │
│  │  │  2. nxmutex_init(&upper->lock)                                         │  │  │
│  │  │  3. register_driver("/dev/npu0", &g_aie_ops, 0666, upper)              │  │  │
│  │  └─────────────────────────────────────────────────────────────────────────┘  │  │
│  └───────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                     │
│  Then: mount procfs, RTC, ADC1, SPI5, I2C1/2, PWM, IWDG, USB CDC/ACM, XSPI2/LFS  │
│                                                                                     │
└─────────────────────────────────────────────────────────────────────────────────────┘

═══════════════════════════════════════════════════════════════════════════════════════
              STM32N6 HARDWARE: NPU + SHARED RESOURCE MAP
═══════════════════════════════════════════════════════════════════════════════════════

┌─────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                     │
│                   AXI Bus Fabric (shared by CPU and NPU)                            │
│                                                                                     │
│  ┌──────────────────────────────┐         ┌──────────────────────────────────────┐  │
│  │     Cortex-M55 CPU           │         │    ATON NPU (Neural-ART)             │  │
│  │     800 MHz                  │         │    1 GHz core, 900 MHz SRAM          │  │
│  │                              │         │                                      │  │
│  │  I-Cache ◄─┐ D-Cache ◄─┐    │         │  ┌─────────────┐  ┌──────────────┐  │  │
│  │  32KB      │ 32KB WB   │    │         │  │ STRENG ×10  │  │ Compute      │  │  │
│  │            │           │    │         │  │ (DMA eng.)  │  │ CONVACC ×4   │  │  │
│  │            │  MPU      │    │         │  │             │  │ DECUN ×2     │  │  │
│  │            │  Region 0 │    │         │  │ SE0: read   │  │ ACTIV ×2     │  │  │
│  │            │  4MB WB   │    │         │  │ SE1: write  │  │ ARITH ×4     │  │  │
│  │            │  R/W-Alloc│    │         │  │ SE2-9: work │  │ POOL ×2      │  │  │
│  │            │           │    │         │  └──────┬──────┘  └──────────────┘  │  │
│  └────────────┼───────────┼────┘         │         │                          │  │
│               │           │              │    STRSWITCH                        │  │
│               │           │              │    (routes SE↔SE)                   │  │
│               │           │              │         │                          │  │
│               │           │              │    INTCTRL                          │  │
│               │           │              │    (IRQ routing → NPU0 IRQ 53)     │  │
│               │           │              │         │                          │  │
│               │           │              │    BUSIF0/1                         │  │
│               │           │              │    (AXI bus interface)              │  │
│               │           │              └─────────┼──────────────────────────┘  │
│               │           │                        │                            │
│       ┌───────┴───────────┴────────────────────────┴──────────────────────────┐  │
│       │                    AXI Interconnect                                    │  │
│       └───────┬──────────────────┬────────────────────────┬───────────────────┘  │
│               │                  │                        │                      │
│               ▼                  ▼                        ▼                      │
│  ┌────────────────────┐  ┌──────────────┐  ┌──────────────────────────────────┐  │
│  │                    │  │              │  │                                  │  │
│  │  AXISRAM 1-2       │  │  AXISRAM 3-6 │  │      CACHEAXI                   │  │
│  │  2 MB              │  │  2.8 MB      │  │      0x580DFC00                  │  │
│  │  0x34000400-       │  │  0x34200000- │  │                                  │  │
│  │  0x341FFFFF        │  │  0x343FFFFF  │  │  Write-back cache for XSPI2     │  │
│  │                    │  │              │  │  Toggled per-inference:           │  │
│  │  NuttX kernel      │  │  NPU activat.│  │   ON during NPUIOC_RUN_SYNC    │  │
│  │  PX4 modules       │  │  scratch     │  │   OFF otherwise (XSPI2 safe)    │  │
│  │  stacks, heap      │  │  I/O buffers │  │                                  │  │
│  │                    │  │              │  │  CID_CACHE=0x01 for SRAM         │  │
│  │  CPU exclusive     │  │  NPU writes  │  │  (non-cacheable, avoids BUSIF)   │  │
│  │                    │  │  via STRENG  │  │                                  │  │
│  └────────────────────┘  └──────────────┘  └────────────────┬─────────────────┘  │
│                                                              │                    │
│                                                              │ cache miss         │
│                                                              ▼                    │
│  ┌────────────────────────────────────────────────────────────────────────────┐    │
│  │                                                                            │    │
│  │  XSPI2 Controller (shared — mutex-locked)                                  │    │
│  │  0x58030000                                                                │    │
│  │                                                                            │    │
│  │  ┌───────────────────────┐           ┌───────────────────────────────┐     │    │
│  │  │  Indirect Mode        │   mutex   │  Memory-Mapped Mode           │     │    │
│  │  │  (register-based I/O) │◄─────────►│  (reads at 0x7xxxxxxx)       │     │    │
│  │  │                       │           │                               │     │    │
│  │  │  Used by:             │           │  Used by:                     │     │    │
│  │  │  • littlefs (R/W)    │           │  • NPU weight reads           │     │    │
│  │  │  • MTD block driver   │           │    (via CACHEAXI)             │     │    │
│  │  │  • flash erase/prog   │           │  • CPU direct reads           │     │    │
│  │  │                       │           │                               │     │    │
│  │  │  stm32_xspi_mmap_    │           │  OPI DTR 8-8-8 @ 200 MHz     │     │    │
│  │  │  unlock() restores    │           │  = 400 MB/s                   │     │    │
│  │  └───────────────────────┘           └───────────────────────────────┘     │    │
│  │                                                                            │    │
│  │  stm32_xspi_mmap_lock()  — NPU acquires, switches to memory-mapped       │    │
│  │  stm32_xspi_mmap_unlock() — NPU releases, switches back to indirect      │    │
│  │                                                                            │    │
│  └────────────────────────────────┬───────────────────────────────────────────┘    │
│                                   │ XSPI2 bus                                      │
│                                   ▼                                                │
│  ┌────────────────────────────────────────────────────────────────────────────┐    │
│  │  MX25UM51245G 64 MB External NOR Flash                                     │    │
│  │                                                                            │    │
│  │  0x70000000  ┌──────────┐  FSBL (128 KB, signed)                          │    │
│  │  0x70020000  ├──────────┤  NuttX/PX4 firmware (1.375 MB)                  │    │
│  │  0x70180000  ├──────────┤  DNN weights — YOLOv8n INT8 (16 MB)            │    │
│  │  0x71000000  ├──────────┤  NPU test weights (2.8 KB)                      │    │
│  │  0x71120000  ├──────────┤  littlefs partition (47 MB)                     │    │
│  │  0x74000000  └──────────┘                                                  │    │
│  └────────────────────────────────────────────────────────────────────────────┘    │
│                                                                                     │
└─────────────────────────────────────────────────────────────────────────────────────┘

═══════════════════════════════════════════════════════════════════════════════════════
             COMPLETE INFERENCE CALL CHAIN (single frame)
═══════════════════════════════════════════════════════════════════════════════════════

  PX4 npu_inference       NuttX VFS       AIE upper       STM32N6 lower       OSAL          ATON HW
  ─────────────────       ─────────       ─────────       ─────────────       ────          ───────
        │                     │               │                │                │              │
   open("/dev/npu0")─────►   │               │                │                │              │
        │              ┌─ lookup inode ──►    │                │                │              │
        │              │  aie_open()──────►   │                │                │              │
        │ ◄────────────┘    (no-op)          │                │                │              │
        │                     │               │                │                │              │
   ioctl(AIE_CMD_LOAD) ─────►│               │                │                │              │
        │              ┌─ aie_ioctl() ───►   │                │                │              │
        │              │  nxmutex_lock       │                │                │              │
        │              │  switch(LOAD)───────►                │                │              │
        │              │           stm32_npu_init()──────────►│                │              │
        │              │                     LL_ATON_RT_Init  │                │              │
        │              │                     get epoch_blocks │                │              │
        │              │                     get I/O buffers  │                │              │
        │              │           return id ◄────────────────┘                │              │
        │              │  nxmutex_unlock     │                │                │              │
        │ ◄────────────┘                     │                │                │              │
        │                     │               │                │                │              │
   ioctl(FEED_INPUT) ───────►│               │                │                │              │
        │              ┌─ aie_ioctl() ───►   │                │                │              │
        │              │  feed_input()───────►                │                │              │
        │              │           memcpy(SRAM_buf, input)────►                │              │
        │ ◄────────────┘                     │                │                │              │
        │                     │               │                │                │              │
   ioctl(RUN_SYNC) ─────────►│               │                │                │              │
        │              ┌─ aie_ioctl() ───►   │                │                │              │
        │              │  control(RUN_SYNC)──►                │                │              │
        │              │           xspi_mmap_lock()           │                │              │
        │              │           CACHEAXI enable+invalidate │                │              │
        │              │           ┌──── epoch loop ──────────┼────────────────┼────────────┐ │
        │              │           │  RunEpochBlock() ────────┼────────────────┼──► HW epoch│ │
        │              │           │  rv = WFE                │                │     running│ │
        │              │           │  LL_ATON_OSAL_WFE()──────┼───────────────►│            │ │
        │              │           │                          │   poll STRENG  │    STRENG  │ │
        │              │           │                          │   CTRL.RUNNING │    engines │ │
        │              │           │                          │       │        │    process │ │
        │              │           │                          │  sched_yield() │    weights │ │
        │              │           │                          │   ┌─────────┐  │    from    │ │
        │              │           │                          │   │ NuttX   │  │    XSPI2   │ │
        │              │           │                          │   │ runs    │  │    via     │ │
        │              │           │                          │   │ EKF2,   │  │    CACHEAXI│ │
        │              │           │                          │   │ mc_rate │  │            │ │
        │              │           │                          │   │ etc.    │  │            │ │
        │              │           │                          │   └─────────┘  │            │ │
        │              │           │                          │   poll again   │  ◄── done  │ │
        │              │           │                          │   not running  │            │ │
        │              │           │                          │   set events ──┼──► ack IRQ │ │
        │              │           │  rv = NO_WFE (SW step)   │                │            │ │
        │              │           │  RunEpochBlock() again   │                │            │ │
        │              │           │  rv = DONE               │                │            │ │
        │              │           └──────────────────────────┼────────────────┼────────────┘ │
        │              │           Reset_Network()            │                │              │
        │ ◄────────────┘           return OK                  │                │              │
        │                     │               │                │                │              │
   ioctl(GET_OUTPUT) ───────►│               │                │                │              │
        │              ┌─ aie_ioctl() ───►   │                │                │              │
        │              │  get_output()───────►                │                │              │
        │              │           memcpy(output, SRAM_buf)───►                │              │
        │ ◄────────────┘                     │                │                │              │
        │                     │               │                │                │              │
   elapsed = hrt_absolute_time() - start     │                │                │              │
   orb_publish(debug_key_value, npu_ms)      │                │                │              │
        │                     │               │                │                │              │


═══════════════════════════════════════════════════════════════════════════════════════
         CONCURRENCY MODEL: NPU vs FLIGHT CONTROL SCHEDULING
═══════════════════════════════════════════════════════════════════════════════════════

  Time ──────────────────────────────────────────────────────────────────────────►

  Priority
    HIGH   │ mc_rate_control ████░░░░████░░░░████░░░░████░░░░████   (250 Hz)
           │ mc_att_control  ██░░░░░░██░░░░░░██░░░░░░██░░░░░░██     (250 Hz)
           │ EKF2            █░░░░░░░█░░░░░░░█░░░░░░░█░░░░░░░█      (250 Hz)
           │
    MED    │ commander       ░░░░█░░░░░░░░░░░░░░█░░░░░░░░░░░░░░█    (5 Hz)
           │ navigator       ░░░░░█░░░░░░░░░░░░░░░█░░░░░░░░░░░░░█   (5 Hz)
           │
    LOW    │ npu_inference   ██████████YYYY██████████YYYY█████████   (29 FPS)
    (-10)  │                          ^^^^
           │                   sched_yield() — CPU given to flight tasks
           │                   during HW epoch (NPU runs independently)
           │
           │ Y = yield (NPU hardware running, CPU free)
           │ █ = CPU active (feed input, SW epochs, get output, publish)
           │ ░ = sleeping

  Key design:
  • npu_inference runs at SCHED_PRIORITY_DEFAULT - 10 (below flight tasks)
  • During HW epochs, OSAL calls sched_yield() → NuttX preempts to flight tasks
  • NPU hardware runs independently on AXI bus while CPU serves control loops
  • Adaptive duty cycle: sleep proportional to inference time between frames
  • At duty=0% (boot default), NPU thread polls param every 100ms (near-zero load)
```
