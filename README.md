# Apache NuttX and PX4 on the STM32N6, with on-chip neural inference

A port of the Apache NuttX real-time operating system to the STMicroelectronics
STM32N657X0 — an Arm Cortex-M55 running at 800 MHz with an integrated Neural-ART
(ATON) neural accelerator — followed by a PX4 Autopilot port on top of it, so that
object detection and flight control run concurrently on a single microcontroller.

At the time this work started, no open-source RTOS port existed for this chip family.

## What runs

| Capability | Status |
|---|---|
| NuttX on NUCLEO-N657X0-Q, standalone boot from external flash | working |
| Chip drivers: UART, GPIO, DMA (GPDMA1/HPDMA1), SPI, I2C, ADC, PWM, RTC, RNG, IWDG, XSPI2, USB OTG HS, MPU | working |
| NPU driver exposing `/dev/npu0` through NuttX's AI-engine framework | working |
| YOLOv8n 192x192 person detection on the NPU | working |
| Live webcam detection streamed over USB | working |
| PX4 v1.17 flight stack with an NPU inference module | working |
| Hardware-in-the-loop flight against jMAVSim | working |

## Measured results

Figures below were measured on hardware and are reproducible with the commands in
this README. Where a number was later found to be wrong it has been corrected
rather than quietly dropped — see *Corrections* at the end.

**Neural inference** (YOLOv8n, 192x192, INT8, 128 epochs, weights in external flash)

| | |
|---|---|
| Inference latency | 33 ms |
| Of which NPU hardware execution | 27.1 ms (82%) |
| CPU epoch scheduling | 5.2 ms (16%) |
| Input transfer and cache maintenance | 0.5 ms (2%) |
| Effective throughput | ~28 GOPS (378.8 MMACC per inference) |

**Host-to-board pipeline** (110 KB frame in, detections out, over USB CDC)

| | |
|---|---|
| End-to-end frame time | 51 ms (19.3 fps) |
| USB bulk OUT throughput | 6.39 MB/s |
| Verified continuous operation | 862 frames, no failures |

**Flight testing** (STM32N6 vs Pixhawk 6C, hardware-in-the-loop)

| | |
|---|---|
| Logged flights | 1,012 across 21 scenarios |
| Pass rate | 99.9% |
| Position hold vs Pixhawk 6C | 2-3x less drift |
| Attitude (roll RMS) vs Pixhawk 6C | ~3x lower |
| Effect of NPU load on flight quality | no statistically significant degradation |

Statistical method and full results: `jMAVSim/logs/stats/REPORT.md` (Mann-Whitney U,
Kruskal-Wallis with Dunn post-hoc, Jonckheere-Terpstra trend, bootstrap BCa CIs).

## Hardware

- **Board**: NUCLEO-N657X0-Q, STLINK-V3EC
- **MCU**: STM32N657X0H3Q — Cortex-M55 @ 800 MHz, always in Secure state, **no internal flash**
- **RAM**: 4.2 MB AXI-SRAM at `0x34000000` (NuttX uses SRAM1; the NPU activation pool spans SRAM2-6)
- **External flash**: MX25UM51245G 64 MB over XSPI2, memory-mapped at `0x70000000`
- **Console**: USART1 on PE5/PE6 at 115200, via the ST-Link virtual COM port
- **USB**: USB1 OTG HS on the Type-C connector

Flash layout: FSBL at `0x70000000`, application at `0x70020000`, neural network
weights at `0x71000000`.

## Building and running

Requires `arm-none-eabi-gcc`, STM32CubeProgrammer (with the v2.22+ signing tool) and
ST Edge AI Core 3.x for regenerating models.

```bash
make nuttx          # build NuttX
make fsbl && make sign   # build and sign the first-stage bootloader
make flash          # write FSBL + NuttX to external flash
make flash-weights  # write neural network weights
make serial         # open the console
```

Boot mode is selected by jumpers: **JP1=1, JP2=2** to program, **JP1=1, JP2=1** to run.
The board boots standalone from external flash with no debugger attached.

### Neural inference demo

On the board:

```
npu_test            # three inferences against known-good reference output
npu_test usb        # inference server over USB CDC
```

On the host:

```bash
python3 npu/webcam_infer.py --camera 0            # live detection window
python3 npu/webcam_infer.py --image res/Messi.jpg # single image
```

### Flight demo

```bash
make flash-px4      # write PX4 to external flash instead of NuttX
./demo_hil.sh run   # fly one validation scenario in jMAVSim
```

Or run a campaign with the 3D view (from the jMAVSim tree):

```bash
make test-suite-gui ROUTINE=routines/demo_showcase.json
```

## Repository layout

| Path | Contents |
|---|---|
| `nuttx/` | NuttX source, including the STM32N6 chip port and board support (separate repository) |
| `apps/` | NuttX applications, including the NPU test and inference server |
| `npu/` | Host tooling, the LL_ATON compatibility layer, and generated model code |
| `SampleSTM32Project/` | First-stage bootloader, and ST HAL sources used as a register reference |
| `documents/` | Design notes and diagrams, including a detailed implementation chronicle |
| `res/` | Demo images and architecture diagrams |

The chip port lives in `nuttx/arch/arm/src/stm32n6/`, board support in
`nuttx/boards/arm/stm32n6/nucleo-n657x0-q/`.

`nuttx/` and `apps/` are separate git repositories and are not contained in this
one. To reproduce a build, both must be checked out alongside this tree:

| Directory | Repository | Branch |
|---|---|---|
| `nuttx/` | fork of `apache/nuttx` | `descriptor-dma-spike` (chip port, board support, NPU driver) |
| `apps/` | fork of `apache/nuttx-apps` | `npu-demo` (`examples/npu_test`, `examples/npu_concur`) |

PX4 is likewise a separate tree, on a branch carrying the STM32N6 board target and
the `npu_inference` module.

Vendor SDKs (X-CUBE-AI reference applications, ST reference manuals, the ST Edge AI
workspace) are deliberately not committed; they are large and obtainable from ST.

## Notable engineering problems solved

Each of these is documented in `documents/implementation_process.md`:

- **Clock configuration is write-once.** `RCC_CFGR1` locks after the first write and
  `CFGR2` locks after it, so CPU and system clock selection must be a single store,
  with prescalers configured beforehand.
- **Cache maintenance faults in Secure state.** Enabling `MEMFAULTENA` breaks D-cache
  maintenance on this Cortex-M55; the fault handler is installed without it.
- **Linker symbol placement.** `_eronly` must be defined inside the `.data` output
  section, otherwise a SRAM-only image copies garbage over itself at startup.
- **The NPU stops when the core sleeps.** `WFE`/`WFI` stalls the accelerator mid-epoch
  regardless of low-power clock enables, so the completion wait spins rather than sleeps.
- **Epoch completion must be verified per stream engine.** Treating the first interrupt
  as "all done" lets the runtime advance over transfers still in flight, producing
  plausible but corrupt output.
- **Offboard control needs a heartbeat.** Test steps that only observe telemetry send
  no setpoints, so PX4 leaves offboard mode and flies a failsafe return-to-launch.

## Corrections

Two figures reported earlier in this project were found to be measurement artifacts
and are corrected here:

- **Inference latency is 33 ms, not 7 ms.** The faster figure came from an epoch
  completion race: the runtime advanced before stream engines finished, so the same
  input produced different output on every run. PX4's `NPU_STATUS` telemetry still
  reports the artifact and should not be quoted until the fix is ported to its NuttX
  submodule.
- **CACHEAXI does not accelerate this workload.** Enabling, retaining or fully
  disabling it produced identical timing to within 1 microsecond, because the cache is
  256 KB against a 3 MB weight set that is streamed once per inference with no reuse.

## Licence

NuttX and PX4 are Apache-2.0. The ST LL_ATON runtime and Neural-ART tooling are
subject to STMicroelectronics licence terms and are not redistributed here.
