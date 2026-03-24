#!/usr/bin/env python3
"""
Webcam -> NPU live inference pipeline.

Captures webcam frames, sends to STM32N6 over USB CDC/ACM for
TinyYOLOv2 people detection, displays results with bounding boxes.

Requires: pip install opencv-python numpy pyserial

Usage:
    python3 npu/webcam_infer.py
    python3 npu/webcam_infer.py --port /dev/ttyACM1
    python3 npu/webcam_infer.py --conf 0.5
    python3 npu/webcam_infer.py --no-display
"""

import argparse
import glob
import sys
import time

import cv2
import numpy as np

sys.path.insert(0, sys.path[0] or ".")
from npu_usb_host import (
    CMD_QUIT, CMD_INFER,
    GRID_H, GRID_W, INPUT_H, INPUT_W,
    NUM_ANCHORS, PEOPLE_DET_SCALE, PEOPLE_DET_ZP,
    YOLO_ANCHORS,
    find_port, query_info, run_inference, sigmoid,
)

BOX_COLOR = (0, 255, 0)
TEXT_COLOR = (255, 255, 255)
BG_COLOR = (0, 0, 0)


def find_camera():
    devices = sorted(glob.glob("/dev/video*"))
    for dev in devices:
        try:
            idx = int(dev.replace("/dev/video", ""))
            cap = cv2.VideoCapture(idx)
            if cap.isOpened():
                cap.release()
                return idx
        except (ValueError, Exception):
            continue
    return None


def nms(detections, iou_threshold=0.3):
    if not detections:
        return []
    boxes = np.array([(d[0], d[1], d[2], d[3]) for d in detections])
    scores = np.array([d[4] for d in detections])
    x1, y1, x2, y2 = boxes[:, 0], boxes[:, 1], boxes[:, 2], boxes[:, 3]
    areas = (x2 - x1) * (y2 - y1)
    order = scores.argsort()[::-1]
    keep = []
    while order.size > 0:
        i = order[0]
        keep.append(i)
        if order.size == 1:
            break
        xx1 = np.maximum(x1[i], x1[order[1:]])
        yy1 = np.maximum(y1[i], y1[order[1:]])
        xx2 = np.minimum(x2[i], x2[order[1:]])
        yy2 = np.minimum(y2[i], y2[order[1:]])
        inter = np.maximum(0, xx2 - xx1) * np.maximum(0, yy2 - yy1)
        iou = inter / (areas[i] + areas[order[1:]] - inter + 1e-6)
        order = order[np.where(iou <= iou_threshold)[0] + 1]
    return [detections[i] for i in keep]


def preprocess_frame(frame):
    """Resize webcam frame to 224x224 NCHW RGB uint8."""
    resized = cv2.resize(frame, (INPUT_W, INPUT_H),
                         interpolation=cv2.INTER_LINEAR)
    rgb = cv2.cvtColor(resized, cv2.COLOR_BGR2RGB)
    nchw = np.transpose(rgb, (2, 0, 1))[np.newaxis, ...]
    return nchw.astype(np.uint8).flatten().view(np.int8)


def decode_detections(raw_int8, conf_threshold, iou_threshold):
    """Decode TinyYOLOv2 int8 output to filtered detections."""
    raw_float = (raw_int8.astype(np.float32) - PEOPLE_DET_ZP) * PEOPLE_DET_SCALE
    grid = raw_float.reshape(GRID_H, GRID_W, 30)
    vals_per_anchor = 30 // NUM_ANCHORS

    detections = []
    for row in range(GRID_H):
        for col in range(GRID_W):
            for a in range(NUM_ANCHORS):
                off = a * vals_per_anchor
                obj = sigmoid(grid[row, col, off + 4])
                if vals_per_anchor > 5:
                    cls = sigmoid(grid[row, col, off + 5])
                    conf = float(obj * cls)
                else:
                    conf = float(obj)

                if conf < conf_threshold:
                    continue

                tx = grid[row, col, off + 0]
                ty = grid[row, col, off + 1]
                tw = grid[row, col, off + 2]
                th = grid[row, col, off + 3]

                bx = (sigmoid(tx) + col) / GRID_W
                by = (sigmoid(ty) + row) / GRID_H
                bw = (YOLO_ANCHORS[a, 0] * np.exp(np.clip(tw, -10, 10))) / GRID_W
                bh = (YOLO_ANCHORS[a, 1] * np.exp(np.clip(th, -10, 10))) / GRID_H

                x1 = max(0, (bx - bw / 2) * INPUT_W)
                y1 = max(0, (by - bh / 2) * INPUT_H)
                x2 = min(INPUT_W, (bx + bw / 2) * INPUT_W)
                y2 = min(INPUT_H, (by + bh / 2) * INPUT_H)

                if x2 > x1 + 2 and y2 > y1 + 2:
                    detections.append((x1, y1, x2, y2, conf, 0))

    detections.sort(key=lambda d: d[4], reverse=True)
    return nms(detections, iou_threshold)


def draw_detections(frame, detections, scale_x, scale_y):
    for (x1, y1, x2, y2, conf, _) in detections:
        dx1, dy1 = int(x1 * scale_x), int(y1 * scale_y)
        dx2, dy2 = int(x2 * scale_x), int(y2 * scale_y)
        cv2.rectangle(frame, (dx1, dy1), (dx2, dy2), BOX_COLOR, 2)
        label = f"person {conf:.0%}"
        (tw, th), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.5, 1)
        cv2.rectangle(frame, (dx1, dy1 - th - 6), (dx1 + tw + 4, dy1),
                      BOX_COLOR, -1)
        cv2.putText(frame, label, (dx1 + 2, dy1 - 4),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, BG_COLOR, 1, cv2.LINE_AA)


def draw_stats(frame, fps, latency_ms, n_dets):
    for i, line in enumerate([f"FPS: {fps:.1f}",
                              f"Latency: {latency_ms:.0f} ms",
                              f"Detections: {n_dets}"]):
        y = 25 + i * 25
        cv2.putText(frame, line, (12, y), cv2.FONT_HERSHEY_SIMPLEX,
                    0.6, TEXT_COLOR, 1, cv2.LINE_AA)


def main():
    parser = argparse.ArgumentParser(description="Webcam -> NPU inference")
    parser.add_argument("--camera", type=int, default=None)
    parser.add_argument("--port", type=str, default=None)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--conf", type=float, default=0.3)
    parser.add_argument("--iou", type=float, default=0.3)
    parser.add_argument("--no-display", action="store_true")
    parser.add_argument("--timeout", type=float, default=10.0)
    args = parser.parse_args()

    import serial
    port = args.port or find_port()
    if not port:
        print("ERROR: No ttyACM device found.")
        sys.exit(1)

    print(f"Opening {port}...")
    ser = serial.Serial(port, args.baud, timeout=args.timeout)
    info = query_info(ser)
    if not info:
        print("ERROR: Failed to query device.")
        ser.close()
        sys.exit(1)

    print(f"Connected. Input: {info['input_shape']} ({info['input_size']}B), "
          f"Output: {info['output_shape']} ({info['output_size']}B)")

    cam_idx = args.camera
    if cam_idx is None:
        cam_idx = find_camera()
        if cam_idx is None:
            print("ERROR: No camera found.")
            ser.write(bytes([CMD_QUIT]))
            ser.close()
            sys.exit(1)

    cap = cv2.VideoCapture(cam_idx)
    if not cap.isOpened():
        print(f"ERROR: Cannot open camera {cam_idx}")
        ser.write(bytes([CMD_QUIT]))
        ser.close()
        sys.exit(1)

    cam_w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    cam_h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    scale_x = cam_w / INPUT_W
    scale_y = cam_h / INPUT_H
    print(f"Camera: {cam_w}x{cam_h}, conf={args.conf}, iou={args.iou}")

    frame_count = 0
    t_start = time.monotonic()

    try:
        while True:
            ret, frame = cap.read()
            if not ret:
                break

            input_data = preprocess_frame(frame)
            t0 = time.monotonic()
            output = run_inference(ser, input_data, info["output_size"])
            latency = time.monotonic() - t0
            frame_count += 1

            if output is None:
                continue

            in_csum = sum(int(b) & 0xff for b in input_data.tobytes())
            out_csum = sum(int(b) & 0xff for b in output.tobytes())

            detections = decode_detections(output, args.conf, args.iou)

            det_str = ""
            for (x1, y1, x2, y2, conf, _) in detections[:5]:
                det_str += f" ({x1:.0f},{y1:.0f})-({x2:.0f},{y2:.0f})@{conf:.0%}"
            if len(detections) > 5:
                det_str += f" +{len(detections)-5}more"

            print(f"  #{frame_count} in={in_csum} out={out_csum} "
                  f"dets={len(detections)}{det_str}")

            wall = time.monotonic() - t_start
            fps = frame_count / wall if wall > 0 else 0

            if args.no_display:
                print(f"\rFrame {frame_count}: {latency*1000:.0f} ms | "
                      f"{fps:.1f} FPS | dets={len(detections)}   ",
                      end="", flush=True)
            else:
                draw_detections(frame, detections, scale_x, scale_y)
                draw_stats(frame, fps, latency * 1000, len(detections))
                cv2.imshow("NPU Inference", frame)
                key = cv2.waitKey(1) & 0xFF
                if key == ord('q') or key == 27:
                    break

    except KeyboardInterrupt:
        pass

    print()
    wall = time.monotonic() - t_start
    if frame_count > 0:
        print(f"{frame_count} frames in {wall:.1f}s = {frame_count/wall:.1f} FPS")

    try:
        ser.write(bytes([CMD_QUIT]))
        ser.flush()
    except Exception:
        pass
    ser.close()
    cap.release()
    if not args.no_display:
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
