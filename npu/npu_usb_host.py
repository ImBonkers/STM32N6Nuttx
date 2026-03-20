#!/usr/bin/env python3
"""
USB-to-NPU inference host script.

Sends input tensors to STM32N6 over USB CDC/ACM, receives NPU output.
Requires: pip install pyserial numpy

Usage:
    python3 npu_usb_host.py                     # single inference
    python3 npu_usb_host.py --live               # continuous until Ctrl-C
    python3 npu_usb_host.py --loop 10           # 10 repeated inferences
    python3 npu_usb_host.py --port /dev/ttyACM1 # explicit port
    python3 npu_usb_host.py --query              # query tensor info only
"""

import argparse
import struct
import sys
import time

import numpy as np
import serial

# Protocol commands
CMD_QUIT  = 0x00
CMD_INFER = 0x01
CMD_QUERY = 0x02

# Model dimensions: [1, 3, 32, 32] int8 -> [1, 16, 32, 32] int8
INPUT_SIZE  = 3072   # 1 * 3 * 32 * 32
OUTPUT_SIZE = 16384  # 1 * 16 * 32 * 32

# npu_tensor_info_s layout (48 bytes):
#   uint32 n_inputs, n_outputs, input_size, output_size
#   int32  input_shape[4], output_shape[4]
INFO_FMT  = "<4I4i4i"
INFO_SIZE = struct.calcsize(INFO_FMT)  # 48 bytes


def find_port():
    """Auto-detect the USB CDC/ACM port (ttyACM1 preferred, then ttyACM0)."""
    import os
    for candidate in ["/dev/ttyACM1", "/dev/ttyACM0"]:
        if os.path.exists(candidate):
            return candidate
    return None


def generate_input(run=0):
    """Generate checkerboard + gradient test pattern matching device-side."""
    buf = np.zeros(INPUT_SIZE, dtype=np.int8)
    for ch in range(3):
        for y in range(32):
            for x in range(32):
                idx = ch * 32 * 32 + y * 32 + x
                if ch == 0:
                    val = (100 - run) if ((x // 8 + y // 8) & 1) else (-100 + run)
                elif ch == 1:
                    val = x * 4 - 64 + run
                else:
                    val = y * 4 - 64 + run
                buf[idx] = np.int8(max(-128, min(127, val)))
    return buf


def query_info(ser):
    """Send QUERY command, receive and parse tensor info."""
    ser.write(bytes([CMD_QUERY]))
    ser.flush()
    data = ser.read(INFO_SIZE)
    if len(data) != INFO_SIZE:
        print(f"ERROR: expected {INFO_SIZE} bytes, got {len(data)}")
        return None

    fields = struct.unpack(INFO_FMT, data)
    info = {
        "n_inputs":     fields[0],
        "n_outputs":    fields[1],
        "input_size":   fields[2],
        "output_size":  fields[3],
        "input_shape":  list(fields[4:8]),
        "output_shape": list(fields[8:12]),
    }
    return info


def run_inference(ser, input_data):
    """Send inference command + input, receive output tensor."""

    # Send command byte separately, then input in small chunks.
    # CDC/ACM has limited RX buffer on device — large writes block.
    ser.write(bytes([CMD_INFER]))
    ser.flush()

    data = input_data.tobytes()
    chunk = 64
    sent = 0
    while sent < len(data):
        end = min(sent + chunk, len(data))
        ser.write(data[sent:end])
        sent = end
    ser.flush()

    output = ser.read(OUTPUT_SIZE)
    if len(output) != OUTPUT_SIZE:
        print(f"ERROR: expected {OUTPUT_SIZE} bytes output, got {len(output)}")
        return None

    return np.frombuffer(output, dtype=np.int8)


def print_output_summary(output, output_shape):
    """Print summary of output tensor."""
    nonzero = np.count_nonzero(output)
    total = len(output)
    print(f"  Non-zero: {nonzero}/{total}")

    # Print first few values per channel
    n_channels = output_shape[1] if len(output_shape) > 1 else 1
    hw = output_shape[2] * output_shape[3] if len(output_shape) > 3 else total
    for ch in range(min(n_channels, 4)):
        start = ch * hw
        vals = output[start:start + 8]
        vals_str = ", ".join(str(v) for v in vals)
        print(f"  ch{ch}[0:8]: [{vals_str}]")

    if n_channels > 4:
        print(f"  ... ({n_channels - 4} more channels)")


def run_live(ser, info):
    """Continuous inference loop with live stats. Ctrl-C to stop."""
    print("\n--- Live inference (Ctrl-C to stop) ---\n")

    count = 0
    fails = 0
    times = []
    t_start = time.monotonic()

    try:
        while True:
            input_data = generate_input(run=count % 200)

            t0 = time.monotonic()
            output = run_inference(ser, input_data)
            elapsed = time.monotonic() - t0

            count += 1

            if output is None:
                fails += 1
                print(f"\r  #{count}: FAILED", end="", flush=True)
                continue

            times.append(elapsed)
            nonzero = np.count_nonzero(output)
            wall = time.monotonic() - t_start

            # Rolling average over last 10
            recent = times[-10:]
            avg_ms = sum(recent) / len(recent) * 1000
            fps = count / wall if wall > 0 else 0

            print(f"\r  #{count}: {elapsed * 1000:.0f} ms | "
                  f"avg {avg_ms:.0f} ms | "
                  f"{fps:.1f} inf/s | "
                  f"nz={nonzero}/{len(output)} | "
                  f"out[0]={output[0]:+d}   ",
                  end="", flush=True)

    except KeyboardInterrupt:
        pass

    print()  # newline after \r output

    wall = time.monotonic() - t_start
    ok = count - fails
    if times:
        avg = sum(times) / len(times) * 1000
        fps = ok / wall if wall > 0 else 0
        print(f"\n--- {ok}/{count} inferences in {wall:.1f}s ---")
        print(f"  Avg: {avg:.1f} ms | {fps:.1f} inf/s")
        print(f"  Min: {min(times) * 1000:.1f} ms | "
              f"Max: {max(times) * 1000:.1f} ms")
    else:
        print(f"\n--- 0 inferences completed ---")


def main():
    parser = argparse.ArgumentParser(description="USB-to-NPU inference host")
    parser.add_argument("--port", type=str, default=None,
                        help="Serial port (default: auto-detect ttyACM1)")
    parser.add_argument("--baud", type=int, default=115200,
                        help="Baud rate (default: 115200)")
    parser.add_argument("--loop", type=int, default=1,
                        help="Number of inference iterations")
    parser.add_argument("--live", action="store_true",
                        help="Continuous inference until Ctrl-C")
    parser.add_argument("--query", action="store_true",
                        help="Query tensor info and exit")
    parser.add_argument("--timeout", type=float, default=5.0,
                        help="Serial read timeout in seconds")
    args = parser.parse_args()

    port = args.port or find_port()
    if port is None:
        print("ERROR: No ttyACM device found. Is the board connected?")
        sys.exit(1)

    print(f"Opening {port} at {args.baud} baud...")
    ser = serial.Serial(port, args.baud, timeout=args.timeout)

    try:
        if args.query:
            info = query_info(ser)
            if info:
                print(f"  Inputs:  {info['n_inputs']}")
                print(f"  Outputs: {info['n_outputs']}")
                print(f"  Input shape:  {info['input_shape']} "
                      f"= {info['input_size']} bytes")
                print(f"  Output shape: {info['output_shape']} "
                      f"= {info['output_size']} bytes")
            return

        # Query info first to confirm connectivity
        info = query_info(ser)
        if info is None:
            print("ERROR: Failed to query device. Is npu_test usb running?")
            sys.exit(1)

        print(f"Connected. Input: {info['input_shape']} ({info['input_size']}B), "
              f"Output: {info['output_shape']} ({info['output_size']}B)")

        if args.live:
            run_live(ser, info)
        else:
            times = []
            for i in range(args.loop):
                input_data = generate_input(run=i)

                t0 = time.monotonic()
                output = run_inference(ser, input_data)
                elapsed = time.monotonic() - t0

                if output is None:
                    print(f"Inference {i + 1}/{args.loop}: FAILED")
                    continue

                times.append(elapsed)
                print(f"Inference {i + 1}/{args.loop}: "
                      f"{elapsed * 1000:.1f} ms (round-trip)")
                print_output_summary(output, info["output_shape"])

            if times:
                avg = sum(times) / len(times) * 1000
                print(f"\n{len(times)}/{args.loop} inferences OK, "
                      f"avg {avg:.1f} ms round-trip")

    finally:
        # Send quit command so device app exits cleanly
        try:
            ser.write(bytes([CMD_QUIT]))
            ser.flush()
        except Exception:
            pass
        ser.close()
        print("Done.")


if __name__ == "__main__":
    main()
