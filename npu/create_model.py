#!/usr/bin/env python3
"""Create and quantize a minimal INT8 Conv2D model for the STM32N6 ATON NPU.

Output: npu_test_s8.onnx — a fully signed-INT8 quantized model that STEdgeAI
compiles to 100% HW epochs (zero CPU fallback).

Architecture:
    [1, 3, 32, 32] int8
    -> Conv2D(16 filters, 3x3, pad=1) -> ReLU
    -> Conv2D(16 filters, 3x3, pad=1) -> ReLU
    -> [1, 16, 32, 32] int8

Usage:
    python3 -m venv .venv
    .venv/bin/pip install numpy onnx onnxruntime
    .venv/bin/python3 create_model.py
"""

import os
import numpy as np
import onnx
from onnx import helper, TensorProto, numpy_helper
from onnxruntime.quantization import quantize_static, CalibrationDataReader, QuantType

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MODEL_DIR = os.path.join(SCRIPT_DIR, "model")
FP32_PATH = os.path.join(MODEL_DIR, "npu_test_fp32.onnx")
INT8_PATH = os.path.join(MODEL_DIR, "npu_test_s8.onnx")
CALIB_SAMPLES = 20


def create_float_model():
    """Create a float32 Conv2D model (will be quantized next).

    Uses structured weights that produce varied, non-zero outputs:
    - Conv1: 16 filters including edge detectors (horizontal, vertical,
      diagonal) and averaging filters, with positive bias to ensure
      outputs survive ReLU after quantization.
    - Conv2: channel mixing with positive bias.
    """

    W1 = np.zeros((16, 3, 3, 3), dtype=np.float32)

    # Filters 0-3: horizontal edge detectors (per input channel + average)
    for ch in range(3):
        W1[ch, ch, 0, :] = -1.0
        W1[ch, ch, 2, :] = 1.0
    W1[3] = W1[0] + W1[1] + W1[2]

    # Filters 4-7: vertical edge detectors
    for ch in range(3):
        W1[4 + ch, ch, :, 0] = -1.0
        W1[4 + ch, ch, :, 2] = 1.0
    W1[7] = W1[4] + W1[5] + W1[6]

    # Filters 8-11: averaging (blur) filters per channel + combined
    for ch in range(3):
        W1[8 + ch, ch, :, :] = 1.0 / 9.0
    W1[11] = W1[8] + W1[9] + W1[10]

    # Filters 12-15: diagonal and identity-like
    for ch in range(3):
        W1[12 + ch, ch, 1, 1] = 2.0  # center-weighted (amplify)
    W1[15, :, 1, 1] = 1.0  # average all channels at center

    # Positive bias ensures many outputs survive ReLU
    B1 = np.full(16, 0.5, dtype=np.float32)

    # Conv2: simple channel mixing — average groups of 4 input channels
    W2 = np.zeros((16, 16, 3, 3), dtype=np.float32)
    for i in range(16):
        # Each output filter reads from 4 input channels at center pixel
        for j in range(4):
            src = (i + j) % 16
            W2[i, src, 1, 1] = 0.25
    B2 = np.full(16, 0.2, dtype=np.float32)

    inits = [
        numpy_helper.from_array(W1, "w1"),
        numpy_helper.from_array(B1, "b1"),
        numpy_helper.from_array(W2, "w2"),
        numpy_helper.from_array(B2, "b2"),
    ]

    X = helper.make_tensor_value_info("input", TensorProto.FLOAT, [1, 3, 32, 32])
    Y = helper.make_tensor_value_info("output", TensorProto.FLOAT, [1, 16, 32, 32])

    nodes = [
        helper.make_node("Conv", ["input", "w1", "b1"], ["c1"],
                         kernel_shape=[3, 3], pads=[1, 1, 1, 1]),
        helper.make_node("Relu", ["c1"], ["r1"]),
        helper.make_node("Conv", ["r1", "w2", "b2"], ["c2"],
                         kernel_shape=[3, 3], pads=[1, 1, 1, 1]),
        helper.make_node("Relu", ["c2"], ["output"]),
    ]

    graph = helper.make_graph(nodes, "npu_test", [X], [Y], initializer=inits)
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 13)])
    model.ir_version = 7
    onnx.checker.check_model(model)

    os.makedirs(MODEL_DIR, exist_ok=True)
    onnx.save(model, FP32_PATH)
    print(f"Float32 model: {FP32_PATH}")
    print(f"  Architecture: [1,3,32,32] -> Conv(16,3x3) -> ReLU -> Conv(16,3x3) -> ReLU -> [1,16,32,32]")
    print(f"  Weights: {W1.nbytes + B1.nbytes + W2.nbytes + B2.nbytes} bytes")


class RandomCalibReader(CalibrationDataReader):
    """Generate calibration data matching the test input pattern."""
    def __init__(self, n=CALIB_SAMPLES):
        np.random.seed(0)
        self.data = iter([
            {"input": np.random.rand(1, 3, 32, 32).astype(np.float32) * 2.0}
            for _ in range(n)
        ])

    def get_next(self):
        return next(self.data, None)


def quantize_model():
    """Quantize float32 model to signed INT8 (QDQ format).

    IMPORTANT: STEdgeAI v3.0 requires signed int8 for BOTH weights AND
    activations to generate pure HW epochs on the ATON NPU. Using uint8
    activations causes 'unsigned integer format not supported' errors.
    """
    quantize_static(
        FP32_PATH,
        INT8_PATH,
        calibration_data_reader=RandomCalibReader(),
        quant_format=1,  # QDQ format
        weight_type=QuantType.QInt8,
        activation_type=QuantType.QInt8,
        per_channel=False,
    )
    print(f"INT8 model: {INT8_PATH}")
    print(f"  Quantization: signed INT8 weights + signed INT8 activations")
    print(f"  Format: QDQ (QuantizeLinear/DequantizeLinear nodes)")


if __name__ == "__main__":
    create_float_model()
    quantize_model()
    print()
    print("Next step: compile for NPU with STEdgeAI v3.0:")
    print(f"  stedgeai generate --model {INT8_PATH} --target stm32n6 "
          f"--st-neural-art --name npu_test --output {MODEL_DIR}/generated --c-api st-ai")
