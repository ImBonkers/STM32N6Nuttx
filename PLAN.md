# Descriptor DMA spike on STM32N6 USB OTG HS

## Why this plan exists

Buffer DMA bulk OUT is broken silicon-wide on STM32N6 DWC_OTG v5.00 (GSNPSID=0x4F54411A). Verified across many register-dump sessions: engine receives data via DMA and decrements PKTCNT, but **none of the three completion paths fire** (DOEPINT.XFRC, GINTSTS.RXFLVL, DOEPINT.NAK). Bulk IN works fine. ST/Zephyr/TinyUSB/Linux-gadget all bypass DMA on N6 by using PIO. PIO is **off the table** for this project (`memory/feedback_no_pio.md` — bottlenecks all USB transfers at ~6 MB/s).

Descriptor DMA is the only remaining DMA path. The silicon supports it (`GHWCFG4.DESC_DMA_ENABLED=1`). RM0486 §73.14.38 marks `DCFG` bit 23 as "Reserved" — descriptor DMA is silicon-supported but ST-undocumented. Linux uses descriptor DMA on production DWC_OTG silicon (their gadget buffer-DMA path was removed as "incomplete"). This is the only known path to HS line rate on this chip.

## Pre-flight context for next session

Before doing anything, read these — they have the full evidence trail:

- `memory/usb-bulk-out-dma.md` — canonical entry, current understanding
- `memory/usb-dwc2-descdma-mandatory.md` — what the previous descriptor-DMA spike got wrong (don't repeat)
- `memory/feedback_no_pio.md` — PIO is non-negotiable
- `memory/usb-driver.md` — driver structure overview
- `CLAUDE.md` — project conventions, build commands, register/clock info

Verify the working tree before touching code:

```bash
cd /home/ubuntu/Documents/GitHub/STM32N6Nuttx
git -C nuttx status
git -C nuttx log --oneline -3
```

Expected: detached HEAD at `85c4887ada`, `stm32_otgdev.c` modified (currently holds `af3bbc327c` content + experimental OUTDONE handler from prior session), several other files modified (Kconfig, Make.defs, ld.script, defconfig, stm32_xspi.c) and `stm32_otg_dma_trace.{c,h}` untracked. **Do not blindly commit any of this** — scope it deliberately.

## Step 0 — Establish a clean working branch

The detached-HEAD state is fragile. First action:

```bash
cd nuttx
git checkout -b descriptor-dma-spike
```

This anchors all subsequent work to a real branch. The user can decide later whether to merge.

## Step 1 — Reset stm32_otgdev.c to the verified baseline

The previous session ended with experimental edits in `stm32_otgdev.c` that did not fix anything. Strip them:

```bash
git -C nuttx checkout af3bbc327c -- arch/arm/src/stm32n6/stm32_otgdev.c
```

Then re-add the only two changes that the build requires:

In `stm32_otgdev.c`, just before `struct stm32_usbdev_s` (around line 562 in the af3bbc327c version), add:

```c
/* Stub variables referenced by apps/examples/usb_speed.  Kept for
 * link compatibility; actual usage drives the trace harness.
 */

uint32_t g_usb_ep0_cnt[8];
uint32_t g_usb_dma_dump[16];
```

Verify it builds:

```bash
cd /home/ubuntu/Documents/GitHub/STM32N6Nuttx
make nuttx 2>&1 | tail -5
```

Expect a clean build, no warnings introduced.

## Step 2 — Verify enumeration baseline (gating test before descriptor DMA work)

Flash and verify the buffer-DMA baseline still works end-to-end before changing anything:

```bash
make flash-nuttx
# Ask user to power-cycle the board
sleep 4
lsusb | grep 0525    # expect: 0525:a4a7 Linux-USB Serial Gadget
ls /dev/ttyACM*       # expect: /dev/ttyACM0 (ST-LINK VCP) and /dev/ttyACM1 (STM32 CDC)
```

Bulk IN test (should work):

```bash
source venv/bin/activate
python3 << 'EOF'
import serial, time, tty, threading
nsh = serial.Serial('/dev/ttyACM0', 115200, timeout=0.5)
cdc = serial.Serial('/dev/ttyACM1', 115200, timeout=2.0)
tty.setraw(nsh.fileno()); tty.setraw(cdc.fileno())
nsh.write(b'\r'); time.sleep(0.3); nsh.read(4096); cdc.reset_input_buffer()

def send(cmd):
    for b in cmd.encode() + b'\r':
        nsh.write(bytes([b])); nsh.flush(); time.sleep(0.005)

send("dd if=/dev/zero of=/dev/ttyACM0 bs=256 count=1")
time.sleep(0.5)
data = cdc.read(2048)
print(f"Bulk IN: received {len(data)} bytes — expect 256")
EOF
```

If enumeration or bulk IN is broken, **stop and diagnose** before any descriptor-DMA work. The whole plan depends on this baseline being clean.

## Step 3 — Read the references before writing code

Before touching the driver, read these in detail (they exist on the filesystem already):

- **Linux dwc2 gadget descriptor DMA**: search the kernel source for `DCFG_DESCDMA`, `dwc2_gadget_set_ep0_desc_chain`, `dwc2_hsotg_program_zlp`. Linux is the canonical reference for descriptor DMA on Synopsys USB IP. The kernel source is in `zephyr/` is not what we want — Zephyr-on-N6 uses PIO. Apple KTRW (`googleprojectzero/ktrw`) `synopsys_otg.c` has device-mode descriptor handling worth comparing.
- **DWC_OTG descriptor STATUS word bit layout**: the previous session's version of the OTG header had defines (BS field, SR/IOC/L/SP/MTRF flags, BYTES mask). They were correct per Synopsys spec. Re-derive from `nuttx/arch/arm/src/stm32n6/hardware/stm32n6xxx_otg.h` if present, otherwise from the Linux `drivers/usb/dwc2/hw.h` header which has the same bit layout for `DEV_DMA_*` macros.
- **Previous descriptor-DMA spike attempt** in this repo's git history: `git -C nuttx log --all --oneline | grep -i desc` may not find anything since the previous spike was uncommitted. The previous session's `stm32_otg_dma_s` extension with `setup_desc`/`ctrl_in_desc`/`in_desc[]`/`out_desc[]` arrays is the right shape — see `memory/usb-dwc2-descdma-mandatory.md` for what worked structurally and what didn't.

## Step 4 — Implement descriptor DMA carefully, one site at a time

The previous spike's failure mode was **layering descriptor-DMA changes on top of broken buffer-DMA edits**. Don't repeat. Do each change in isolation, verify enumeration after each, only proceed if enumeration still works.

### 4a. Add descriptor STATUS-word bit definitions to the OTG header

Add to `nuttx/arch/arm/src/stm32n6/hardware/stm32n6xxx_otg.h`:

```c
/* DMA Descriptor Status Word (Buffer DMA mode, DCFG.DESCDMA=1) */
#define OTG_DMADESC_BYTES_MASK    (0xFFFF << 0)
#define OTG_DMADESC_MTRF          (1 << 23)
#define OTG_DMADESC_SR            (1 << 24)   /* SETUP received (EP0 OUT) */
#define OTG_DMADESC_IOC           (1 << 25)   /* interrupt on complete */
#define OTG_DMADESC_SP            (1 << 26)   /* short packet (IN) */
#define OTG_DMADESC_L             (1 << 27)   /* last descriptor */
#define OTG_DMADESC_STS_MASK      (3 << 28)
#define OTG_DMADESC_BS_MASK       (3 << 30)
#define OTG_DMADESC_BS_HRDY       (0 << 30)
#define OTG_DMADESC_BS_DBUSY      (1 << 30)
#define OTG_DMADESC_BS_DDONE      (2 << 30)
#define OTG_DMADESC_BS_HBUSY      (3 << 30)
```

`OTG_DCFG_DESCDMA = (1 << 23)` should already exist in the header.

Build, do not flash yet.

### 4b. Extend `stm32_otg_dma_s` with descriptor pool

In `stm32_otgdev.c`, around the existing `struct stm32_otg_dma_s` (~line 510 in af3bbc327c version), extend:

```c
struct stm32_dma_desc_s
{
  volatile uint32_t status;
  volatile uint32_t buf;
};

struct stm32_otg_dma_s
{
  uint8_t  setup_buf[64]   aligned_data(32);
  uint8_t  ep0in_buf[64]   aligned_data(32);
  uint8_t  ep0out_buf[CONFIG_USBDEV_SETUP_MAXDATASIZE]
                           aligned_data(32);

  struct stm32_dma_desc_s setup_desc                       aligned_data(32);
  struct stm32_dma_desc_s in_desc[STM32_NENDPOINTS]        aligned_data(32);
  struct stm32_dma_desc_s out_desc[STM32_NENDPOINTS]       aligned_data(32);
};

static struct stm32_otg_dma_s g_otg_dma aligned_data(64);
```

Build, flash, **verify enumeration still works** (Step 2 verification). The new struct fields are unused at this point so enumeration must be unaffected.

### 4c. Set DCFG.DESCDMA=1 at both DCFG write sites

Find every `stm32_putreg(regval, STM32_OTG_DCFG)` site (there are two: USBRST handler ~line 2545 and `stm32_hwinitialize` ~line 6735 in af3bbc327c). Add before each:

```c
#ifdef CONFIG_STM32N6_OTG_DMA
  regval |= OTG_DCFG_DESCDMA;
#endif
```

Build, flash, **verify enumeration**. Expectation: enumeration likely breaks at this point because nothing else has been converted to descriptor mode. That's fine — proceed to step 4d.

### 4d. Convert EP0 SETUP arm in `stm32_ep0out_ctrlsetup` to descriptor mode

Find `stm32_ep0out_ctrlsetup` (~line 1208 in af3bbc327c). Replace the existing DOEPDMA write block with a descriptor build:

```c
priv->dma->setup_desc.status = OTG_DMADESC_BS_HBUSY | OTG_DMADESC_SR |
                               OTG_DMADESC_IOC | OTG_DMADESC_L | 24;
priv->dma->setup_desc.buf = (uint32_t)(uintptr_t)priv->dma->setup_buf;
up_clean_dcache((uintptr_t)&priv->dma->setup_desc,
                (uintptr_t)&priv->dma->setup_desc + sizeof(priv->dma->setup_desc));
stm32_putreg((uint32_t)(uintptr_t)&priv->dma->setup_desc,
             STM32_OTG_DOEPDMA(0));
```

Build, flash, **verify enumeration**. EP0 SETUP must work for any USB to function. If broken: this is where the previous spike's bugs lived — debug here before proceeding.

### 4e-4g. Convert EP IN arm, EP OUT arm, and SETUP-data-OUT arm

Each follows the same pattern: build a descriptor with `BS_HBUSY | IOC | L | SP(for IN) | bytecount`, point DMA register at descriptor address (not buffer). See `memory/usb-dwc2-descdma-mandatory.md` for the exact code patterns from the previous attempt — they were structurally correct.

Verify enumeration after each step.

### 4h. Convert XFRC handlers to read residual from descriptor STATUS word

OUT XFRC handler residual: `priv->dma->out_desc[epno].status & OTG_DMADESC_BYTES_MASK` instead of DOEPTSIZ.

After each change: build, flash, verify enumeration, then test bulk IN, then test bulk OUT.

## Step 5 — Verification gates (run after each major step)

Each gate must pass before moving to the next step. Use the host-side test in Step 2 plus:

```bash
# Bulk OUT verification (the actual goal):
python3 << 'EOF'
import serial, time, tty, threading
nsh = serial.Serial('/dev/ttyACM0', 115200, timeout=0.5)
cdc = serial.Serial('/dev/ttyACM1', 115200, timeout=2.0, write_timeout=3.0)
tty.setraw(nsh.fileno()); tty.setraw(cdc.fileno())
nsh.write(b'\r'); time.sleep(0.3); nsh.read(4096); cdc.reset_input_buffer()

def arm():
    for b in b"usb_dma_probe rx 64\r":
        nsh.write(bytes([b])); nsh.flush(); time.sleep(0.005)

t = threading.Thread(target=arm); t.start()
time.sleep(0.4)
nsh.read(2048)
try:
    cdc.write(bytes(range(64))); cdc.flush()
    print(">>> CDC write OK <<<")
except Exception as e:
    print(f">>> CDC write FAIL: {e} <<<")
import time; time.sleep(2)
print(nsh.read(4096).decode('utf-8','replace'))
EOF
```

Success criteria: `OK: 64 bytes match` from the probe.

If still failing, dump OTG state to characterize:

```
nsh> usb_dma_probe otgregs hang
```

Look at `EP3 DOEPCTL` (NAKSTS bit), `EP3 DOEPINT` (XFRC bit), `EP3 out_desc.status` (BS field — should transition through HBUSY→DBUSY→DDONE; if stuck at HBUSY, engine never picked up the descriptor).

## Step 6 — If descriptor DMA also fails

If after careful implementation descriptor DMA also doesn't fire OUT completion interrupts, that's a definitive answer that this silicon revision doesn't support either DMA mode in any usable way. At that point:

1. Document the new finding in `memory/usb-bulk-out-dma.md`.
2. Capture a precise reproducer (commit-able branch + test script).
3. **Escalate to ST FAE** with the reproducer — this would be a silicon-level limitation worth their attention. Ask specifically: which DMA mode does ST recommend for HS bulk OUT on STM32N657, and provide a working reference if any.
4. **Consider hardware alternatives**: USB2 OTG2 controller might be a separate IP block worth trying. Or external USB2 transceiver (ULPI) bypass.

PIO remains off the table — the alternative if descriptor DMA fails is escalation, not retreat.

## Critical files

- `nuttx/arch/arm/src/stm32n6/stm32_otgdev.c` — main edits (~12 sites for descriptor DMA conversion)
- `nuttx/arch/arm/src/stm32n6/hardware/stm32n6xxx_otg.h` — add DMA descriptor STATUS bit defs
- `nuttx/arch/arm/src/stm32n6/stm32_otg_dma_trace.{c,h}` — trace harness (untracked, useful for diagnosis)
- `nuttx/boards/arm/stm32n6/nucleo-n657x0-q/scripts/ld.script` — `.dmabuf` and `.tracebuf` sections already exist
- `nuttx/boards/arm/stm32n6/nucleo-n657x0-q/configs/nsh/defconfig` — `CONFIG_STM32N6_OTG_DMA=y` is the gate; `CONFIG_STM32N6_OTG_DMA_TRACE=y` and `CONFIG_EXAMPLES_USB_DMA_PROBE=y` enable diagnostic harnesses
- `apps/examples/usb_dma_probe/usb_dma_probe_main.c` — diagnostic probe with `rx N`, `otgregs idle|hang`, `buf` commands
- `npu/usb_dma_session.py` — host driver (raw-mode TTY, NSH+CDC dual)

## Critical constraints

- **PIO is non-negotiable off the table** — see `memory/feedback_no_pio.md`. If descriptor DMA fails, escalate; do not retreat.
- **Verify enumeration after every change** — buffer-DMA enumeration on the af3bbc327c base works; if descriptor-DMA breaks it, the next step is debug-the-last-change, not "iterate forward and hope".
- **Don't mix experiments** — one variable at a time. Previous session's failure was layering descriptor DMA on top of broken buffer DMA, then chasing the wrong symptoms.
- **Trust the memory entries** — they capture verified facts. Don't re-derive what's already documented (especially the silicon's GHWCFG values, the DOEPDMA-advance trick that proved to be wrong on this silicon, and that ST treats DCFG bit 23 as Reserved).

## Reference register values (silicon, verified)

- `GHWCFG4 = 0xE2103E30` — bit 30 supports descriptor DMA, bit 31 default-on
- `GHWCFG2 = 0x228FE052` — OTGARCH = 2 (Internal DMA)
- `GRXFSIZ = 0x000001E4` (484 words default)
- `GSNPSID = 0x4F54411A` — DWC_OTG v5.00 IP

## Build commands cheat-sheet

```bash
cd /home/ubuntu/Documents/GitHub/STM32N6Nuttx
make nuttx           # compile
make flash-nuttx     # flash to external flash
make serial          # tio /dev/ttyACM0
```

NSH terminal access via Python (raw-mode TTY required to avoid cooked-mode interference):

```python
import serial, tty, time
nsh = serial.Serial('/dev/ttyACM0', 115200, timeout=0.2)
tty.setraw(nsh.fileno())
# Always send commands byte-by-byte with ~5ms inter-byte delay:
for b in b"usb_dma_probe otgregs idle\r":
    nsh.write(bytes([b])); nsh.flush(); time.sleep(0.005)
```
