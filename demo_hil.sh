#!/bin/bash
# PX4 + NPU HIL demo runbook: flash the N6, then fly S23 in jMAVSim.
#
#   ./demo_hil.sh              # flash + run S23 (NPU idle)
#   ./demo_hil.sh npu100       # flash + run S23 with NPU pinned at 100% duty
#   ./demo_hil.sh npu100 run   # skip flashing, just run
#
# Board jumpers must be JP1=pos1, JP2=pos1 (boot from external flash).

set -e

SCENARIO=scenarios/S23_hitl_loop_validation.json
[ "$1" = "npu100" ] && SCENARIO=scenarios/S23_hitl_loop_validation_npu100.json

JMAVSIM=$HOME/Documents/GitHub/jMAVSim
PROJ=$(cd "$(dirname "$0")" && pwd)

if [ "$1" != "run" ] && [ "$2" != "run" ]; then
  echo "==> Flashing FSBL + PX4 + YOLOv8n weights"
  lsusb | grep -qi 0483 || { echo "ERROR: no ST-Link on USB. Plug the board in."; exit 1; }
  make -C "$PROJ" flash-px4
  make -C "$PROJ" flash-weights
  echo
  echo "==> Power-cycle the board now (unplug/replug), then press Enter"
  read -r _
fi

echo "==> Waiting for the PX4 MAVLink port to enumerate"
for _ in $(seq 30); do
  [ -e /dev/ttyACM1 ] && break
  sleep 1
done
[ -e /dev/ttyACM1 ] || { echo "ERROR: /dev/ttyACM1 never appeared. Check the OTG-C cable."; exit 1; }
echo "    ST-Link console : /dev/ttyACM0   (make serial)"
echo "    PX4 MAVLink     : /dev/ttyACM1   @ 921600"

echo "==> Running $(basename "$SCENARIO")"
# Invoke java directly rather than `make test`: that target depends on `build`,
# which would rebuild the jar under Java 25 and overwrite the known-good Apr 1
# jar that produced the thesis dataset.
cd "$JMAVSIM"
exec java --add-opens java.desktop/sun.awt=ALL-UNNAMED \
          --add-opens java.desktop/sun.java2d=ALL-UNNAMED \
          --enable-native-access=ALL-UNNAMED \
          -jar out/production/jmavsim_run.jar \
          -serial /dev/ttyACM1 921600 \
          -test "$SCENARIO" -test-keep-running
