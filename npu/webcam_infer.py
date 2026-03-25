#!/usr/bin/env python3
"""
Webcam -> NPU live inference pipeline.

Supports TinyYOLOv2 (people_det) and YOLOv8n person detection.
Auto-detects model from query response.

Requires: pip install opencv-python numpy pyserial
"""

import argparse
import glob
import os
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


def preprocess_frame(frame, input_w, input_h):
    """Resize webcam frame to NHWC RGB uint8."""
    resized = cv2.resize(frame, (input_w, input_h),
                         interpolation=cv2.INTER_LINEAR)
    rgb = cv2.cvtColor(resized, cv2.COLOR_BGR2RGB)
    return rgb.astype(np.uint8).flatten().view(np.int8)


def decode_yolov8(raw_bytes, conf_threshold, iou_threshold, input_w, input_h):
    """Decode YOLOv8n float32 output [756, 5] to detections.

    Driver reads DequantizeLinear float32 output from activation pool.
    Layout: [756, 5] = [grid_cells, (cx, cy, w, h, conf)]
    All values normalized 0-1 relative to input size.
    756 = 24² + 12² + 6² (3 feature map scales for 192x192 input).
    """
    # DequantizeLinear float32 output [756, 5] = 15120 bytes
    # Channels: [cx, cy, w, h, conf] normalized 0-1
    data = np.frombuffer(raw_bytes[:15120], dtype=np.float32).reshape(756, 5)

    detections = []
    for i in range(756):
        conf = float(data[i, 4])
        if conf < conf_threshold:
            continue

        cx = float(data[i, 0]) * input_w
        cy = float(data[i, 1]) * input_h
        w = float(data[i, 2]) * input_w
        h = float(data[i, 3]) * input_h

        x1 = max(0, cx - w / 2)
        y1 = max(0, cy - h / 2)
        x2 = min(input_w, cx + w / 2)
        y2 = min(input_h, cy + h / 2)

        if x2 > x1 + 2 and y2 > y1 + 2:
            detections.append((x1, y1, x2, y2, conf, 0))

    detections.sort(key=lambda d: d[4], reverse=True)
    return nms(detections, iou_threshold)


def decode_yolov2(raw_int8, conf_threshold, iou_threshold):
    """Decode TinyYOLOv2 int8 output [7,7,30] to detections."""
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
    parser.add_argument("--image", type=str, default=None)
    parser.add_argument("--port", type=str, default=None)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--conf", type=float, default=0.5)
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

    # Auto-detect model
    out_shape = info["output_shape"]
    in_shape = info["input_shape"]
    is_yolov8 = out_shape[1] == 5  # [1, 5, 756, 0]
    input_w = in_shape[2] if in_shape[1] > 3 else in_shape[3]
    input_h = in_shape[1] if in_shape[1] > 3 else in_shape[2]
    model_name = "YOLOv8n" if is_yolov8 else "TinyYOLOv2"

    print(f"Connected [{model_name}]. Input: {in_shape} ({info['input_size']}B), "
          f"Output: {out_shape} ({info['output_size']}B)")

    def decode(output):
        if is_yolov8:
            return decode_yolov8(output.tobytes(), args.conf, args.iou,
                                 input_w, input_h)
        else:
            return decode_yolov2(output, args.conf, args.iou)

    # --- Static image mode ---
    if args.image:
        frame = cv2.imread(args.image)
        if frame is None:
            print(f"ERROR: Cannot load {args.image}")
            ser.write(bytes([CMD_QUIT])); ser.close(); sys.exit(1)

        h, w = frame.shape[:2]
        scale_x, scale_y = w / input_w, h / input_h
        print(f"Image: {w}x{h}, conf={args.conf}")

        input_data = preprocess_frame(frame, input_w, input_h)
        output = run_inference(ser, input_data, info["output_size"])
        if output is None:
            print("Inference FAILED")
        else:
            detections = decode(output)
            print(f"dets={len(detections)}")
            for (x1, y1, x2, y2, conf, _) in detections:
                print(f"  ({x1:.0f},{y1:.0f})-({x2:.0f},{y2:.0f}) {conf:.0%}")

            draw_detections(frame, detections, scale_x, scale_y)
            out_dir = os.path.join(os.path.dirname(args.image), "out")
            os.makedirs(out_dir, exist_ok=True)
            base = os.path.splitext(os.path.basename(args.image))[0]
            out_path = os.path.join(out_dir, f"{base}_det.jpg")
            cv2.imwrite(out_path, frame)
            print(f"Saved: {out_path}")

            if not args.no_display:
                cv2.imshow("NPU Inference", frame)
                cv2.waitKey(0)
                cv2.destroyAllWindows()

        ser.write(bytes([CMD_QUIT])); ser.flush(); ser.close()
        return

    # --- Camera mode ---
    cam_idx = args.camera
    if cam_idx is None:
        cam_idx = find_camera()
        if cam_idx is None:
            print("ERROR: No camera found.")
            ser.write(bytes([CMD_QUIT])); ser.close(); sys.exit(1)

    cap = cv2.VideoCapture(cam_idx)
    if not cap.isOpened():
        print(f"ERROR: Cannot open camera {cam_idx}")
        ser.write(bytes([CMD_QUIT])); ser.close(); sys.exit(1)

    cam_w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    cam_h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    scale_x = cam_w / input_w
    scale_y = cam_h / input_h
    print(f"Camera: {cam_w}x{cam_h}, model={model_name}, conf={args.conf}")

    frame_count = 0
    t_start = time.monotonic()

    try:
        while True:
            ret, frame = cap.read()
            if not ret:
                break

            input_data = preprocess_frame(frame, input_w, input_h)
            t0 = time.monotonic()
            output = run_inference(ser, input_data, info["output_size"])
            latency = time.monotonic() - t0
            frame_count += 1

            if output is None:
                continue

            detections = decode(output)

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
        ser.write(bytes([CMD_QUIT])); ser.flush()
    except Exception:
        pass
    ser.close()
    cap.release()
    if not args.no_display:
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
