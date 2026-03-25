# Top-Level Makefile for STM32N6 NuttX Project
# Orchestrates NuttX RTOS build, FSBL build, signing, and flashing.

# ---- Configurable tool paths (override via env or make VAR=val) ----
CUBEPROG_DIR  ?= $(HOME)/STMicroelectronics/STM32Cube/STM32CubeProgrammer
STPROG        := $(CUBEPROG_DIR)/bin/STM32_Programmer_CLI
SIGNTOOL      := $(CUBEPROG_DIR)/bin/STM32_SigningTool_CLI
EXTLOADER     := $(CUBEPROG_DIR)/bin/ExternalLoader/MX25UM51245G_STM32N6570-NUCLEO.stldr
NUTTX_VENV    ?= $(HOME)/.nuttx-venv
STEDGEAI_DIR  ?= $(HOME)/STMicroelectronics/STEdgeAI3/3.0
STEDGEAI      := $(STEDGEAI_DIR)/Utilities/linux/stedgeai

# ---- Addresses ----
NUTTX_LOAD_ADDR  := 0x34000400
NUTTX_FLASH_ADDR := 0x70020000
FSBL_FLASH_ADDR  := 0x70000000
FSBL_LOAD_ADDR   := 0x34180400
WEIGHTS_FLASH_ADDR := 0x71000000
VTOR_REG         := 0xE000ED08

# ---- Derived paths ----
NUTTX_DIR      := nuttx
NUTTX_BIN      := $(NUTTX_DIR)/nuttx.bin
NUTTX_ELF      := $(NUTTX_DIR)/nuttx

FSBL_DIR       := SampleSTM32Project/Makefile/FSBL
FSBL_BIN       := $(FSBL_DIR)/build/SampleProject_FSBL.bin
FSBL_ELF       := $(FSBL_DIR)/build/SampleProject_FSBL.elf
FSBL_SIGNED    := $(FSBL_DIR)/build/SampleProject_FSBL_signed.bin

NPU_DIR        := npu
NPU_MODEL_DIR  := $(NPU_DIR)/model
NPU_GEN_DIR    := $(NPU_MODEL_DIR)/generated
NPU_VENV       := $(NPU_DIR)/.venv
NPU_ONNX_FP32  := $(NPU_MODEL_DIR)/npu_test_fp32.onnx
NPU_ONNX_INT8  := $(NPU_MODEL_DIR)/npu_test_s8.onnx
NPU_WEIGHTS    := $(NPU_MODEL_DIR)/generated_yolov8n192/yolov8n192_atonbuf.xSPI2.bin

NUTTX_PATH     := $(HOME)/.local/bin:$(NUTTX_VENV)/bin:$(PATH)

.PHONY: all rebuild nuttx fsbl sign flash-dev flash flash-fsbl flash-nuttx \
        flash-weights npu-model npu-venv serial configure menuconfig \
        clean clean-nuttx clean-fsbl clean-npu help

# ---- Default target ----
all: nuttx fsbl sign

# ---- Full rebuild from scratch ----
rebuild:
	$(MAKE) clean
	$(MAKE) configure
	$(MAKE) all

# ---- NuttX ----
nuttx: $(NPU_WEIGHTS)
	PATH=$(NUTTX_PATH) $(MAKE) -C $(NUTTX_DIR) -j$$(nproc)

# ---- FSBL ----
fsbl:
	$(MAKE) -C $(FSBL_DIR)

# ---- Sign FSBL (auto-extracts Reset_Handler from ELF) ----
sign: $(FSBL_ELF)
	@rm -f $(FSBL_SIGNED)
	@RESET=$$(arm-none-eabi-nm $(FSBL_ELF) | awk '/ Reset_Handler$$/ {print $$1}'); \
	EP=$$(printf "%X" $$((0x$$RESET | 1))); \
	echo "Signing FSBL: Reset_Handler=0x$$RESET EP=0x$$EP"; \
	$(SIGNTOOL) -bin $(FSBL_BIN) -nk -of 0x80000000 -t fsbl \
	  -ep 0x$$EP -la $(FSBL_LOAD_ADDR) \
	  -o $(FSBL_SIGNED) -hv 2.3 -align

# ---- Flash: DEV mode (SRAM direct) ----
flash-dev: $(NUTTX_BIN)
	$(STPROG) -c port=SWD mode=UR -halt \
	  -d $(NUTTX_BIN) $(NUTTX_LOAD_ADDR) \
	  -w32 $(VTOR_REG) $(NUTTX_LOAD_ADDR) \
	  -g $(NUTTX_LOAD_ADDR)

# ---- Flash: Normal boot (external flash, both FSBL + NuttX) ----
flash: $(FSBL_SIGNED) $(NUTTX_BIN)
	$(STPROG) -c port=SWD mode=UR -halt
	$(STPROG) -c port=SWD mode=HOTPLUG ap=1 -el $(EXTLOADER) \
	  -w $(FSBL_SIGNED) $(FSBL_FLASH_ADDR)
	$(STPROG) -c port=SWD mode=HOTPLUG ap=1 -el $(EXTLOADER) \
	  -w $(NUTTX_BIN) $(NUTTX_FLASH_ADDR)

# ---- Flash: only signed FSBL to external flash ----
flash-fsbl: $(FSBL_SIGNED)
	$(STPROG) -c port=SWD mode=UR -halt
	$(STPROG) -c port=SWD mode=HOTPLUG ap=1 -el $(EXTLOADER) \
	  -w $(FSBL_SIGNED) $(FSBL_FLASH_ADDR)

# ---- Flash: only NuttX to external flash ----
flash-nuttx: $(NUTTX_BIN)
	$(STPROG) -c port=SWD mode=UR -halt
	$(STPROG) -c port=SWD mode=HOTPLUG ap=1 -el $(EXTLOADER) \
	  -w $(NUTTX_BIN) $(NUTTX_FLASH_ADDR)

# ---- Flash: NPU model weights to external flash ----
flash-weights: $(NPU_WEIGHTS)
	$(STPROG) -c port=SWD mode=UR -halt
	$(STPROG) -c port=SWD mode=HOTPLUG ap=1 -el $(EXTLOADER) \
	  -w $(NPU_WEIGHTS) $(WEIGHTS_FLASH_ADDR)

# ---- NPU model generation pipeline ----
# Creates venv, generates float model, quantizes to INT8, compiles for NPU,
# and renames weights to .bin for STM32CubeProgrammer.

npu-venv: $(NPU_VENV)/bin/python3

$(NPU_VENV)/bin/python3:
	python3 -m venv $(NPU_VENV)
	$(NPU_VENV)/bin/pip install --quiet numpy onnx onnxruntime

$(NPU_ONNX_INT8): $(NPU_DIR)/create_model.py $(NPU_VENV)/bin/python3
	$(NPU_VENV)/bin/python3 $(NPU_DIR)/create_model.py

$(NPU_WEIGHTS): $(NPU_ONNX_INT8)
	@rm -rf $(NPU_GEN_DIR)
	$(STEDGEAI) generate \
	  --model $(NPU_ONNX_INT8) \
	  --target stm32n6 \
	  --st-neural-art \
	  --name npu_test \
	  --output $(NPU_GEN_DIR) \
	  --c-api st-ai \
	  --verbosity 1
	cp $(NPU_GEN_DIR)/npu_test_atonbuf.xSPI2.raw $(NPU_WEIGHTS)
	@echo "NPU model generated: $(NPU_GEN_DIR)/"
	@echo "Weights: $(NPU_WEIGHTS) ($$(stat -c%s $(NPU_WEIGHTS)) bytes)"

npu-model: $(NPU_WEIGHTS)

clean-npu:
	rm -rf $(NPU_GEN_DIR) $(NPU_MODEL_DIR)/npu_test_fp32.onnx $(NPU_VENV)

# ---- Serial console ----
serial:
	tio /dev/ttyACM0 -b 115200

# ---- NuttX configuration ----
configure:
	cd $(NUTTX_DIR) && PATH=$(NUTTX_PATH) tools/configure.sh nucleo-n657x0-q:nsh

menuconfig:
	PATH=$(NUTTX_PATH) $(MAKE) -C $(NUTTX_DIR) menuconfig

savedefconfig:
	PATH=$(NUTTX_PATH) $(MAKE) -C $(NUTTX_DIR) savedefconfig
	cp $(NUTTX_DIR)/defconfig $(NUTTX_DIR)/boards/arm/stm32n6/nucleo-n657x0-q/configs/nsh/defconfig

# ---- Clean ----
clean: clean-nuttx clean-fsbl

clean-nuttx:
	PATH=$(NUTTX_PATH) $(MAKE) -C $(NUTTX_DIR) distclean || true

clean-fsbl:
	$(MAKE) -C $(FSBL_DIR) clean || true

# ---- Help ----
help:
	@echo "STM32N6 NuttX Project - Build Targets"
	@echo ""
	@echo "  Build:"
	@echo "    make all          Build NuttX + FSBL + sign FSBL"
	@echo "    make rebuild      Clean + configure + build everything"
	@echo "    make nuttx        Build NuttX only"
	@echo "    make fsbl         Build FSBL only"
	@echo "    make sign         Sign FSBL binary (auto Reset_Handler)"
	@echo "    make npu-model    Generate INT8 NPU model (venv + quantize + STEdgeAI)"
	@echo ""
	@echo "  Flash:"
	@echo "    make flash-dev    Flash NuttX to SRAM (DEV mode)"
	@echo "    make flash        Flash signed FSBL + NuttX to ext flash"
	@echo "    make flash-fsbl   Flash only signed FSBL to ext flash"
	@echo "    make flash-nuttx  Flash only NuttX to ext flash"
	@echo "    make flash-weights Flash NPU model weights to ext flash"
	@echo ""
	@echo "  Console:"
	@echo "    make serial       Open tio serial console (115200)"
	@echo ""
	@echo "  Config:"
	@echo "    make configure    Run NuttX configure.sh (first-time)"
	@echo "    make menuconfig   Run NuttX menuconfig"
	@echo ""
	@echo "  Clean:"
	@echo "    make clean        Clean NuttX + FSBL"
	@echo "    make clean-nuttx  Clean NuttX only"
	@echo "    make clean-fsbl   Clean FSBL only"
	@echo "    make clean-npu    Clean NPU model + venv"
