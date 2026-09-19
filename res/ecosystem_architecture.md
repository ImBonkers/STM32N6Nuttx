# PX4 Ecosystem Interoperability with NPU Integration

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                        PX4 ECOSYSTEM (preserved interfaces)                         │
│                                                                                     │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐  ┌───────────────┐  │
│  │  QGroundControl  │  │    jMAVSim /    │  │   PX4 Upstream  │  │  Companion    │  │
│  │  (Ground Station)│  │    Gazebo       │  │   (GitHub)      │  │  Computer     │  │
│  │                  │  │  (HITL/SITL)    │  │                 │  │  (MAVSDK)     │  │
│  │  Mission plan    │  │  Physics sim    │  │  git merge      │  │  Offboard     │  │
│  │  Telemetry view  │  │  Sensor inject  │  │  CI/CD          │  │  control      │  │
│  │  Parameter tune  │  │  Visualization  │  │  Code review    │  │  Vision data  │  │
│  └────────┬─────────┘  └────────┬────────┘  └────────┬────────┘  └───────┬───────┘  │
│           │                     │                     │                   │          │
│           │    MAVLink v2       │   MAVLink v2        │  git              │ MAVLink  │
│           │    (standard)       │   (HIL_SENSOR,      │  (clean           │ (custom  │
│           │                     │    HIL_GPS,          │   commits,        │  msgs)   │
│           │                     │    HIL_ACTUATOR)     │   board-only      │          │
│           │                     │                     │   changes)        │          │
└───────────┼─────────────────────┼─────────────────────┼───────────────────┼──────────┘
            │                     │                     │                   │
            ▼                     ▼                     ▼                   ▼
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                     │
│                    MAVLink Communication Layer                                       │
│                    (unmodified protocol)                                             │
│                                                                                     │
│    ┌──────────────────────────────────────────────────────────────────────────┐      │
│    │  mavlink module                                                          │      │
│    │                                                                          │      │
│    │  Standard messages:              NPU telemetry (no protocol changes):    │      │
│    │  • HEARTBEAT                     • NAMED_VALUE_FLOAT { npu_ms }          │      │
│    │  • ATTITUDE                      • NAMED_VALUE_FLOAT { npu_fps }         │      │
│    │  • LOCAL_POSITION_NED            • NAMED_VALUE_FLOAT { npu_avg }         │      │
│    │  • HIL_SENSOR / HIL_GPS                                                  │      │
│    │  • HIL_ACTUATOR_CONTROLS         Uses existing MAVLink message type —     │      │
│    │  • COMMAND_LONG / ACK            no custom message definitions needed    │      │
│    │  • PARAM_VALUE / SET                                                     │      │
│    │  • STATUSTEXT                                                            │      │
│    └──────────────────────────────────────────────────────────────────────────┘      │
│                                                                                     │
│    Transport: USB CDC/ACM @ 921600 baud (/dev/ttyACM0)                              │
│                                                                                     │
└───────────────────────────────────────┬─────────────────────────────────────────────┘
                                        │
                                        ▼
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                     │
│              uORB Publish-Subscribe Middleware (unmodified)                          │
│                                                                                     │
│    Decouples all modules. NPU module participates as a standard publisher.          │
│    No changes to uORB core, existing topics, or subscription mechanisms.            │
│                                                                                     │
│ ┌─ Standard PX4 Topics (unchanged) ──────────────────────────────────────────────┐  │
│ │                                                                                │  │
│ │  sensor_combined    vehicle_attitude    vehicle_command    actuator_outputs     │  │
│ │  vehicle_status     vehicle_local_pos   vehicle_gps_pos   battery_status       │  │
│ │  manual_control     landing_gear        ...                                    │  │
│ └────────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                     │
└───────────────────────────────────────┬─────────────────────────────────────────────┘
                                        │
                                        ▼
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                     │
│                  PX4 Modular Task Structure                                          │
│                  (nested control loops + NPU module)                                 │
│                                                                                     │
│  ┌───────────────────────────────── Existing PX4 Modules ─────────────────────────┐ │
│  │  (UNMODIFIED — identical to upstream PX4)                                      │ │
│  │                                                                                │ │
│  │  ┌─────────────────────────────────────────────────────────────────────────┐   │ │
│  │  │                         Outer Loop (Position)                           │   │ │
│  │  │  navigator ──► mc_pos_control ──► position setpoint                    │   │ │
│  │  │                                                                         │   │ │
│  │  │  ┌─────────────────────────────────────────────────────────────────┐   │   │ │
│  │  │  │                    Mid Loop (Attitude)                          │   │   │ │
│  │  │  │  flight_mode_manager ──► mc_att_control ──► attitude setpoint  │   │   │ │
│  │  │  │                                                                 │   │   │ │
│  │  │  │  ┌─────────────────────────────────────────────────────────┐   │   │   │ │
│  │  │  │  │                Inner Loop (Rate)                        │   │   │   │ │
│  │  │  │  │  mc_rate_control ──► control_allocator ──► motor mix   │   │   │   │ │
│  │  │  │  └─────────────────────────────────────────────────────────┘   │   │   │ │
│  │  │  └─────────────────────────────────────────────────────────────────┘   │   │ │
│  │  └─────────────────────────────────────────────────────────────────────────┘   │ │
│  │                                                                                │ │
│  │  sensors(-h)  EKF2  commander  land_detector  load_mon  dataman  logger       │ │
│  │  mc_hover_thrust_estimator    pwm_out_sim(HIL)                                │ │
│  └────────────────────────────────────────────────────────────────────────────────┘ │
│                                                                                     │
│  ┌───────────────────────────────── NPU Addition ─────────────────────────────────┐ │
│  │  (NEW — board-specific module, no upstream module changes)                     │ │
│  │                                                                                │ │
│  │  ┌──────────────────────────────────────────────────────────────────────────┐  │ │
│  │  │  npu_inference module                                                    │  │ │
│  │  │                                                                          │  │ │
│  │  │  • Standard PX4 module (ModuleBase)        Integration strategy:         │  │ │
│  │  │  • ScheduledWorkItem timing                • Additive only (new files)   │  │ │
│  │  │  • Publishes via NAMED_VALUE_FLOAT         • No existing module edits    │  │ │
│  │  │  • Runtime control: "npu duty <0-100>"     • Board px4board opt-in       │  │ │
│  │  │  • sched_yield() during HW epochs          • Disabled by default         │  │ │
│  │  │  • 34 ms inference, 29 FPS                 • Merge-safe: board/ only     │  │ │
│  │  └──────────────────────┬───────────────────────────────────────────────────┘  │ │
│  │                         │ ioctl                                                │ │
│  │                         ▼                                                      │ │
│  │  ┌──────────────────────────────────────────────────────────────────────────┐  │ │
│  │  │  /dev/npu0  (NuttX AIE character device)                                 │  │ │
│  │  │  Standard NuttX device interface — open/close/ioctl                       │  │ │
│  │  └──────────────────────────────────────────────────────────────────────────┘  │ │
│  └────────────────────────────────────────────────────────────────────────────────┘ │
│                                                                                     │
└───────────────────────────────────────┬─────────────────────────────────────────────┘
                                        │
                                        ▼
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                     │
│                          NuttX RTOS (kernel boundary)                                │
│                                                                                     │
│  ┌──── Standard NuttX Services ─────────────────────┐  ┌── STM32N6 BSP ──────────┐ │
│  │                                                   │  │  (new chip, new board)  │ │
│  │  POSIX API    Scheduler    VFS    Signals         │  │                         │ │
│  │  pthreads     SCHED_RR    /dev/   Semaphores      │  │  stm32_serial.c         │ │
│  │  Timers       Work queue  procfs  Mutexes         │  │  stm32_otgdev.c         │ │
│  │                                                   │  │  stm32_spi.c            │ │
│  │  Unchanged from upstream NuttX 12.12.0            │  │  stm32_i2c.c            │ │
│  │  PX4 consumes standard POSIX interfaces           │  │  stm32_dma.c            │ │
│  │                                                   │  │  stm32_xspi.c           │ │
│  └───────────────────────────────────────────────────┘  │  stm32_npu.c ◄── NEW    │ │
│                                                         │  stm32_pwr.c            │ │
│                                                         │  stm32n6xx_rcc.c        │ │
│                                                         │  stm32_start.c          │ │
│                                                         └─────────────────────────┘ │
│                                                                                     │
└───────────────────────────────────────┬─────────────────────────────────────────────┘
                                        │
                                        ▼
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                     │
│                        STM32N6 Hardware                                              │
│                                                                                     │
│  ┌─── PX4 Flight Stack ──────────────────┐  ┌─── NPU Subsystem ──────────────────┐ │
│  │  (standard peripherals)                │  │  (isolated hardware domain)        │ │
│  │                                        │  │                                    │ │
│  │  USART1 ── console/debug               │  │  ATON NPU @ 1 GHz                 │ │
│  │  USB OTG ── MAVLink (CDC/ACM)          │  │  CACHEAXI (XSPI2 weight cache)    │ │
│  │  TIM5 ───── HRT (1 MHz)               │  │  STRENG DMA engines                │ │
│  │  TIM2 ───── NuttX system timer         │  │  XSPI2 ── 400 MB/s weight reads   │ │
│  │  I2C1/2 ─── sensor bus                 │  │                                    │ │
│  │  SPI5 ───── sensor bus                 │  │  Memory: AXISRAM 3-6 (2.8 MB)     │ │
│  │  ADC1 ───── voltage monitoring         │  │  Model:  YOLOv8n INT8 192×192     │ │
│  │  GPIO ───── LEDs, buttons              │  │  Weights: ext. flash 0x71000000   │ │
│  │                                        │  │                                    │ │
│  │  Memory: AXISRAM 1-2 (2 MB)           │  │  Shared: XSPI2 (mutex-locked)     │ │
│  │  Code:   0x34000400 — 0x341FFFFF       │  │          NPU0 IRQ (53)            │ │
│  │                                        │  │          CACHEAXI IRQ (57)         │ │
│  └────────────────────────────────────────┘  └────────────────────────────────────┘ │
│                                                                                     │
│  ┌─── Shared Resources (contention managed) ─────────────────────────────────────┐  │
│  │                                                                                │  │
│  │  XSPI2 controller: mutex between NPU (memory-mapped) and littlefs (indirect) │  │
│  │  AXI bus fabric: CPU and NPU share bus, NPU yields via sched_yield()          │  │
│  │  SMPS + VOS SCALE0: must be configured before NPU PLL2/PLL3                   │  │
│  └────────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                     │
└─────────────────────────────────────────────────────────────────────────────────────┘


═══════════════════════════════════════════════════════════════════════════════════════
                    UPSTREAM MERGE COMPATIBILITY (RQ2)
═══════════════════════════════════════════════════════════════════════════════════════

  Change Classification by Repository Location:

  ┌─────────────────────┬────────────────────────┬──────────────────────────────────┐
  │     Location        │     Change Type        │     Merge Impact                 │
  ├─────────────────────┼────────────────────────┼──────────────────────────────────┤
  │                     │                        │                                  │
  │  PX4 upstream       │  NONE modified         │  Clean merge: upstream modules   │
  │  modules            │  (sensors, EKF2,       │  are untouched. git merge from   │
  │  src/modules/       │   commander, mc_*,     │  upstream PX4 has zero conflicts │
  │                     │   navigator, etc.)     │  in flight-critical code.        │
  │                     │                        │                                  │
  ├─────────────────────┼────────────────────────┼──────────────────────────────────┤
  │                     │                        │                                  │
  │  PX4 board          │  NEW files only        │  Isolated: all STM32N6 code in   │
  │  boards/stm/        │  (board support,       │  boards/stm/n6-nucleo/ and       │
  │  n6-nucleo/         │   ROMFS, linker,       │  platforms/nuttx/src/px4/stm/    │
  │                     │   FSBL, NPU model)     │  stm32n6/. No overlap with       │
  │                     │                        │  other boards.                   │
  │                     │                        │                                  │
  ├─────────────────────┼────────────────────────┼──────────────────────────────────┤
  │                     │                        │                                  │
  │  PX4 platform       │  NEW chip family       │  Additive: new stm32n6/ dir      │
  │  platforms/nuttx/   │  (HRT, ADC, board      │  parallel to existing stm32h7/,  │
  │  src/px4/stm/       │   hw info, headers)    │  stm32f4/, etc. No edits to      │
  │  stm32n6/           │                        │  shared platform code.           │
  │                     │                        │                                  │
  ├─────────────────────┼────────────────────────┼──────────────────────────────────┤
  │                     │                        │                                  │
  │  NPU module         │  NEW module            │  Opt-in: CONFIG_MODULES_NPU_     │
  │  src/modules/       │  (npu_inference.c)     │  INFERENCE in px4board. Not      │
  │  npu_inference/     │                        │  built unless board enables it.  │
  │                     │                        │  Standard ModuleBase pattern.    │
  │                     │                        │                                  │
  ├─────────────────────┼────────────────────────┼──────────────────────────────────┤
  │                     │                        │                                  │
  │  NuttX BSP          │  NEW chip + board      │  Separate submodule: NuttX       │
  │  nuttx/arch/arm/    │  (stm32n6/ chip,       │  changes are in the NuttX repo,  │
  │  src/stm32n6/       │   nucleo-n657x0-q)     │  not PX4. PX4 only updates the  │
  │                     │                        │  submodule pointer.              │
  │                     │                        │                                  │
  ├─────────────────────┼────────────────────────┼──────────────────────────────────┤
  │                     │                        │                                  │
  │  Minimal cross-     │  Guarded patches       │  Low risk: NULL tcb guards in    │
  │  cutting changes    │  (3 files, <20 lines)  │  load_mon + logger watchdog +    │
  │                     │                        │  print_load. EKF2 heading fix.   │
  │                     │                        │  All behind NULL checks or       │
  │                     │                        │  board #ifdefs.                  │
  │                     │                        │                                  │
  └─────────────────────┴────────────────────────┴──────────────────────────────────┘

  Ecosystem Interoperability Checklist:

  ✓  MAVLink communication  — standard protocol, NAMED_VALUE_FLOAT for NPU
  ✓  QGroundControl         — connects, displays telemetry, tunes params
  ✓  HITL simulation        — jMAVSim Iris quad, HIL_SENSOR/GPS/ACTUATOR
  ✓  Upstream merge         — board-only additions, zero flight-code edits
  ✓  Module architecture    — uORB pub/sub, WorkItem scheduling, ModuleBase
  ✓  NuttX POSIX API        — standard device drivers, /dev/ interface
  ✓  Nested control loops   — rate → attitude → position (unmodified)
  ✓  Parameter system       — hardcoded in ROMFS (no SD), standard param API
```
