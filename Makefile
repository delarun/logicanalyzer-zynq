# =====================================================================
# Antminer S9 v1.3 — сборка базового проекта
#
#   make hw        — Vivado: проект, битстрим, XSA
#   make sw        — Vitis/XSCT: FSBL, lwIP echo, memtest
#   make boot      — BOOT.BIN для SD-карты (FSBL + битстрим + echo)
#   make jtag      — залить битстрим и запустить echo через XVC
#   make memtest   — то же, но тест DDR
#
# Логический анализатор (hw/la, sw/la):
#   make la-sim    — симуляция ядра захвата (xsim)
#   make la-hw     — Vivado: битстрим и XSA анализатора
#   make la-sw     — Vitis: FSBL и прошивка s9la
#   make la-jtag   — залить и запустить через XVC
#   make la-boot   — BOOT.BIN (FSBL + битстрим + s9la)
#   make la-flash  — записать BOOT.BIN в NAND через XVC (перемычки в JTAG)
#
#   make clean     — удалить build/ целиком (включая исходники sigrok в build/sigrok)
#
# Переменные: XILINX_DIR, XILINX_VER, XVC=TCP:<ip>:2542, JOBS
# =====================================================================

XILINX_DIR ?= /mnt/data/Xilinx
XILINX_VER ?= 2022.2
XVC        ?= TCP:192.168.7.1:2542
JOBS       ?= 8

SETTINGS := source $(XILINX_DIR)/Vitis/$(XILINX_VER)/settings64.sh
SHELL    := /bin/bash
# Vivado плохо переносит нелатинскую локаль (десятичная запятая и т.п.)
export LC_ALL := C.UTF-8
export LANG   := C.UTF-8
export JOBS

# Vitis 2022.2 на свежих дистрибутивах: если xsct падает с "channel tcfchan#0 closed",
# переименуй $(XILINX_DIR)/Vitis/2022.2/lib/lnx64.o/Default/libstdc++.so.6 в *.bak
# (старая libstdc++ ломает загрузку GTK/SWT в Java-сервере Vitis).

B      := build
HW     := $(B)/hw_base
WS     := $(B)/sw_base
XSA    := $(HW)/s9_base.xsa
BIT    := $(HW)/s9_base.bit
ECHO   := $(WS)/echo/Debug/echo.elf
MEMT   := $(WS)/memtest/Debug/memtest.elf
BOOTBIN:= $(B)/BOOT.BIN

.PHONY: all hw sw boot jtag memtest clean la-sim la-hw la-sw la-jtag la-boot la-flash

all: boot

hw: $(XSA)
$(XSA) $(BIT) &: hw/base/create_project.tcl hw/base/base.xdc
	mkdir -p $(HW)
	$(SETTINGS) && vivado -mode batch -nojournal -log $(HW)/vivado.log \
		-source hw/base/create_project.tcl -tclargs $(abspath $(HW))

sw: $(ECHO)
# Локальный репозиторий Vitis: lwIP с патчем под PHY S9 (версия 1.9 перекрывает штатную 1.8)
LWIP_SRC  := $(XILINX_DIR)/Vitis/$(XILINX_VER)/data/embeddedsw/ThirdParty/sw_services/lwip211_v1_8
LWIP_REPO := $(B)/repo
$(LWIP_REPO)/.stamp: sw/patches/lwip211-generic-phy.patch
	rm -rf $(LWIP_REPO) && mkdir -p $(LWIP_REPO)/sw_services
	cp -r $(LWIP_SRC) $(LWIP_REPO)/sw_services/lwip211_v1_9
	patch -p1 -d $(LWIP_REPO)/sw_services/lwip211_v1_9 < sw/patches/lwip211-generic-phy.patch
	touch $@

$(ECHO) $(MEMT) &: $(XSA) sw/base/create_sw.tcl $(LWIP_REPO)/.stamp
	$(SETTINGS) && xsct sw/base/create_sw.tcl $(abspath $(XSA)) $(abspath $(WS)) $(abspath $(LWIP_REPO))

boot: $(BOOTBIN)
$(BOOTBIN): $(ECHO) $(BIT)
	@FSBL=$$(find $(WS)/s9_base -name fsbl.elf | head -1); \
	test -n "$$FSBL" || { echo "fsbl.elf не найден"; exit 1; }; \
	printf 'the_ROM_image:\n{\n  [bootloader] %s\n  %s\n  %s\n}\n' \
		"$$(realpath $$FSBL)" "$(abspath $(BIT))" "$(abspath $(ECHO))" > $(B)/boot.bif; \
	cat $(B)/boot.bif
	$(SETTINGS) && bootgen -image $(B)/boot.bif -arch zynq -o $(BOOTBIN) -w on
	@echo "Готово: $(BOOTBIN) → скопировать в корень FAT32-раздела SD, перемычки в режим SD"

# $(1) — elf, $(2) — workspace, $(3) — битстрим
define jtag_run
	@PS7=$$(find $(2) -name ps7_init.tcl | head -1); \
	test -n "$$PS7" || { echo "ps7_init.tcl не найден"; exit 1; }; \
	$(SETTINGS) && xsct scripts/jtag_run.tcl $(XVC) $(abspath $(3)) "$$(realpath $$PS7)" $(abspath $(1))
endef

jtag: $(ECHO)
	$(call jtag_run,$(ECHO),$(WS),$(BIT))

memtest: $(MEMT)
	$(call jtag_run,$(MEMT),$(WS),$(BIT))

# ---------------------------------------------------------------------
# Логический анализатор
# ---------------------------------------------------------------------
LA_HW   := $(B)/hw_la
LA_WS   := $(B)/sw_la
LA_XSA  := $(LA_HW)/s9_la.xsa
LA_BIT  := $(LA_HW)/s9_la.bit
LA_ELF  := $(LA_WS)/s9la/Debug/s9la.elf
LA_NANDW:= $(LA_WS)/nandwrite/Debug/nandwrite.elf
LA_BOOT := $(B)/la/BOOT.BIN
LA_SRC  := $(wildcard sw/la/src/* sw/nandwrite/src/*)

la-sim:
	$(SETTINGS) && hw/la/sim/run.sh

la-hw: $(LA_XSA)
$(LA_XSA) $(LA_BIT) &: hw/la/create_project.tcl hw/la/la.xdc hw/la/rtl/s9la_core.v hw/common/ps7_s9.tcl
	mkdir -p $(LA_HW)
	$(SETTINGS) && vivado -mode batch -nojournal -log $(LA_HW)/vivado.log \
		-source hw/la/create_project.tcl -tclargs $(abspath $(LA_HW))

la-sw: $(LA_ELF)
$(LA_ELF) $(LA_NANDW) &: $(LA_XSA) sw/la/create_sw.tcl $(LA_SRC) $(LWIP_REPO)/.stamp
	$(SETTINGS) && xsct sw/la/create_sw.tcl $(abspath $(LA_XSA)) $(abspath $(LA_WS)) \
		$(abspath $(LWIP_REPO)) $(abspath sw/la/src)

la-jtag: $(LA_ELF)
	$(call jtag_run,$(LA_ELF),$(LA_WS),$(LA_BIT))

la-boot: $(LA_BOOT)
$(LA_BOOT): $(LA_ELF) $(LA_BIT)
	@mkdir -p $(dir $@)
	@FSBL=$$(find $(LA_WS)/s9_la -name fsbl.elf | head -1); \
	test -n "$$FSBL" || { echo "fsbl.elf не найден"; exit 1; }; \
	printf 'the_ROM_image:\n{\n  [bootloader] %s\n  %s\n  %s\n}\n' \
		"$$(realpath $$FSBL)" "$(abspath $(LA_BIT))" "$(abspath $(LA_ELF))" > $(dir $@)boot.bif
	$(SETTINGS) && bootgen -image $(dir $@)boot.bif -arch zynq -o $@ -w on

la-flash: $(LA_BOOT) $(LA_NANDW)
	@PS7=$$(find $(LA_WS) -name ps7_init.tcl | head -1); \
	$(SETTINGS) && xsct scripts/nand_write.tcl $(XVC) "$$(realpath $$PS7)" \
		$(abspath $(LA_NANDW)) $(abspath $(LA_BOOT))

clean:
	rm -rf $(B) .Xil *.jou *.log
