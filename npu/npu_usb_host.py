#!/usr/bin/env python3
"""
USB-to-NPU inference host script.

Sends input tensors to STM32N6 over USB CDC/ACM, receives NPU output.
Supports both the small test model and TinyYOLOv2 people detection.

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

# npu_tensor_info_s layout (48 bytes):
#   uint32 n_inputs, n_outputs, input_size, output_size
#   int32  input_shape[4], output_shape[4]
INFO_FMT  = "<4I4i4i"
INFO_SIZE = struct.calcsize(INFO_FMT)  # 48 bytes

# TinyYOLOv2 people detection parameters
# Output dequantization: float_val = (int8_val - zero_point) * scale
PEOPLE_DET_SCALE = 0.1461298167705536
PEOPLE_DET_ZP    = 11

# YOLO anchor boxes (5 anchors for TinyYOLOv2)
YOLO_ANCHORS = np.array([
    [1.08, 1.19],
    [3.42, 4.41],
    [6.63, 11.38],
    [9.42, 5.11],
    [16.62, 10.52],
])

# Person class index in VOC/COCO (TinyYOLOv2 VOC: 20 classes, person=14)
PERSON_CLASS = 14
NUM_CLASSES  = 20
NUM_ANCHORS  = 5
GRID_H       = 7
GRID_W       = 7
INPUT_H      = 224
INPUT_W      = 224


def find_port():
    """Auto-detect the USB CDC/ACM port (ttyACM1 preferred, then ttyACM0)."""
    import os
    for candidate in ["/dev/ttyACM1", "/dev/ttyACM0"]:
        if os.path.exists(candidate):
            return candidate
    return None


def generate_input(info, run=0):
    """Generate test input pattern matching the model's input size."""
    in_size = info["input_size"]
    # Pseudo-random pattern (matches device-side local test)
    buf = np.zeros(in_size, dtype=np.uint8)
    for i in range(in_size):
        buf[i] = (i * 7 + run * 13) & 0xff
    return buf.view(np.int8)


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


def is_people_det(info):
    """Check if the connected model is people_det based on output shape."""
    return info["output_shape"] == [1, 7, 7, 30]


def run_inference(ser, input_data, output_size):
    """Send inference command + input, receive output tensor."""

    # Send command byte separately, then input in small chunks.
    # CDC/ACM has limited RX buffer on device — large writes block.
    ser.write(bytes([CMD_INFER]))
    ser.flush()

    data = input_data.tobytes()
    chunk = 512
    sent = 0
    while sent < len(data):
        end = min(sent + chunk, len(data))
        ser.write(data[sent:end])
        sent = end
    ser.flush()

    output = ser.read(output_size)
    if len(output) != output_size:
        print(f"ERROR: expected {output_size} bytes output, got {len(output)}")
        return None

    return np.frombuffer(output, dtype=np.int8)


def sigmoid(x):
    """Numerically stable sigmoid."""
    x = np.clip(x, -50, 50)
    return np.where(x >= 0,
                    1 / (1 + np.exp(-x)),
                    np.exp(x) / (1 + np.exp(x)))


def softmax(x, axis=-1):
    """Numerically stable softmax."""
    e_x = np.exp(x - np.max(x, axis=axis, keepdims=True))
    return e_x / np.sum(e_x, axis=axis, keepdims=True)


def decode_yolo_output(raw_int8):
    """Decode TinyYOLOv2 int8 output to bounding box detections.

    Args:
        raw_int8: int8 array of shape (1470,) = [7, 7, 30]
                  30 = 5 anchors * (5 box params + 1 class for person)
                  Actually 30 = 5 * (4 + 1 + 20) for VOC = 5 * 25... wait
                  TinyYOLOv2 VOC: 30 = 5 anchors * (5 + 1) for single class
                  Actually: 30 channels = 5 * (4 coords + 1 obj + 1 class)
                  OR: 30 = 5 * 6 (pruned to person only)
                  Let's check: original TinyYOLOv2 has 125 outputs (5*(4+1+20))
                  Our quantized model has 30 outputs = could be 5*(4+1+1) pruned

    Returns:
        List of (x1, y1, x2, y2, confidence, class_id) tuples
    """
    # Dequantize int8 -> float32
    raw_float = (raw_int8.astype(np.float32) - PEOPLE_DET_ZP) * PEOPLE_DET_SCALE

    # Reshape to [7, 7, 30]
    grid = raw_float.reshape(GRID_H, GRID_W, 30)

    # 30 channels = 5 anchors * 6 values per anchor
    # Each anchor: [tx, ty, tw, th, obj_score, class_score]
    # (model pruned to single class or 30 = 5*6)
    vals_per_anchor = 30 // NUM_ANCHORS  # = 6

    detections = []
    conf_threshold = 0.3

    for row in range(GRID_H):
        for col in range(GRID_W):
            for a in range(NUM_ANCHORS):
                offset = a * vals_per_anchor
                tx = grid[row, col, offset + 0]
                ty = grid[row, col, offset + 1]
                tw = grid[row, col, offset + 2]
                th = grid[row, col, offset + 3]
                obj = sigmoid(grid[row, col, offset + 4])

                # Class score(s)
                if vals_per_anchor > 5:
                    cls_score = sigmoid(grid[row, col, offset + 5])
                    conf = obj * cls_score
                else:
                    conf = obj

                if conf < conf_threshold:
                    continue

                # Decode box coordinates
                bx = (sigmoid(tx) + col) / GRID_W
                by = (sigmoid(ty) + row) / GRID_H
                bw = (YOLO_ANCHORS[a, 0] * np.exp(tw)) / GRID_W
                bh = (YOLO_ANCHORS[a, 1] * np.exp(th)) / GRID_H

                # Convert to pixel coords [x1, y1, x2, y2]
                x1 = max(0, (bx - bw / 2) * INPUT_W)
                y1 = max(0, (by - bh / 2) * INPUT_H)
                x2 = min(INPUT_W, (bx + bw / 2) * INPUT_W)
                y2 = min(INPUT_H, (by + bh / 2) * INPUT_H)

                detections.append((x1, y1, x2, y2, float(conf), 0))

    # Sort by confidence descending
    detections.sort(key=lambda d: d[4], reverse=True)
    return detections


def print_output_summary(output, info):
    """Print summary of output tensor."""
    nonzero = np.count_nonzero(output)
    total = len(output)
    print(f"  Non-zero: {nonzero}/{total}")

    if is_people_det(info):
        detections = decode_yolo_output(output)
        if detections:
            print(f"  Detections ({len(detections)}):")
            for i, (x1, y1, x2, y2, conf, cls) in enumerate(detections[:5]):
                print(f"    #{i+1}: ({x1:.0f},{y1:.0f})-({x2:.0f},{y2:.0f}) "
                      f"conf={conf:.3f}")
            if len(detections) > 5:
                print(f"    ... ({len(detections) - 5} more)")
        else:
            print("  No detections above threshold")
    else:
        # Print first few values per channel (npu_test model)
        output_shape = info["output_shape"]
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

    output_size = info["output_size"]
    people_det = is_people_det(info)
    count = 0
    fails = 0
    times = []
    t_start = time.monotonic()

    try:
        while True:
            input_data = generate_input(info, run=count % 200)

            t0 = time.monotonic()
            output = run_inference(ser, input_data, output_size)
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

            if people_det:
                dets = decode_yolo_output(output)
                n_dets = len(dets)
                print(f"\r  #{count}: {elapsed * 1000:.0f} ms | "
                      f"avg {avg_ms:.0f} ms | "
                      f"{fps:.1f} inf/s | "
                      f"dets={n_dets} | "
                      f"nz={nonzero}/{len(output)}   ",
                      end="", flush=True)
            else:
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
    parser.add_argument("--timeout", type=float, default=10.0,
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
                if is_people_det(info):
                    print(f"  Model: TinyYOLOv2 people detection")
                    print(f"  Dequant: scale={PEOPLE_DET_SCALE}, "
                          f"zp={PEOPLE_DET_ZP}")
                else:
                    print(f"  Model: npu_test (Conv2D)")
            return

        # Query info first to confirm connectivity
        info = query_info(ser)
        if info is None:
            print("ERROR: Failed to query device. Is npu_test usb running?")
            sys.exit(1)

        model_name = "TinyYOLOv2" if is_people_det(info) else "npu_test"
        print(f"Connected [{model_name}]. "
              f"Input: {info['input_shape']} ({info['input_size']}B), "
              f"Output: {info['output_shape']} ({info['output_size']}B)")

        if args.live:
            run_live(ser, info)
        else:
            times = []
            for i in range(args.loop):
                input_data = generate_input(info, run=i)

                t0 = time.monotonic()
                output = run_inference(ser, input_data, info["output_size"])
                elapsed = time.monotonic() - t0

                if output is None:
                    print(f"Inference {i + 1}/{args.loop}: FAILED")
                    continue

                times.append(elapsed)
                print(f"Inference {i + 1}/{args.loop}: "
                      f"{elapsed * 1000:.1f} ms (round-trip)")
                print_output_summary(output, info)

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
