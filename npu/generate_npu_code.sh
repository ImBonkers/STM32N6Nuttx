#!/bin/bash
# Generate NPU code from the INT8 ONNX model using STEdgeAI v3.0
#
# Prerequisites:
#   1. Run create_model.py first to generate model/npu_test_s8.onnx
#   2. STEdgeAI v3.0 installed (with ST Neural-ART module)
#
# Output: model/generated/ containing:
#   - npu_test.c / npu_test.h         — network implementation (epoch blobs)
#   - stai_npu_test.c / stai_npu_test.h — STAI interface
#   - npu_test_atonbuf.xSPI2.raw      — weights for external flash

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODEL_DIR="$SCRIPT_DIR/model"
INT8_MODEL="$MODEL_DIR/npu_test_s8.onnx"
OUTPUT_DIR="$MODEL_DIR/generated"

# Find STEdgeAI v3.0
STEDGEAI="${STEDGEAI:-$HOME/STMicroelectronics/STEdgeAI3/3.0/Utilities/linux/stedgeai}"

if [ ! -x "$STEDGEAI" ]; then
    echo "ERROR: STEdgeAI not found at $STEDGEAI"
    echo "Install STEdgeAI v3.0 with Neural-ART module, or set STEDGEAI env var."
    exit 1
fi

if [ ! -f "$INT8_MODEL" ]; then
    echo "ERROR: INT8 model not found at $INT8_MODEL"
    echo "Run create_model.py first."
    exit 1
fi

echo "STEdgeAI: $($STEDGEAI --version 2>&1 | head -1)"
echo "Model:    $INT8_MODEL"
echo "Output:   $OUTPUT_DIR"
echo

rm -rf "$OUTPUT_DIR"

"$STEDGEAI" generate \
    --model "$INT8_MODEL" \
    --target stm32n6 \
    --st-neural-art \
    --name npu_test \
    --output "$OUTPUT_DIR" \
    --c-api st-ai \
    --verbosity 1

echo
echo "Generated files:"
ls -la "$OUTPUT_DIR"/*.c "$OUTPUT_DIR"/*.h "$OUTPUT_DIR"/*.raw 2>/dev/null
