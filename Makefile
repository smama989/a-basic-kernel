# ═══════════════════════════════════════════════════════
#  Makefile — PowerKernel 32-bit Module v2.0
# ═══════════════════════════════════════════════════════
#
#  make              – build module
#  make load         – sudo insmod
#  make unload       – sudo rmmod
#  make status       – lsmod + dmesg tail
#  make test         – write + read /dev/powerkernel
#  make proc         – cat /proc/powerkernel
#  make clean        – remove build artefacts
#  make info         – show module info (modinfo)

obj-m += powerkernel.o

KDIR  ?= /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)
MOD    = powerkernel

# ── Build ──────────────────────────────────────────────
all:
	@echo "Building PowerKernel module..."
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	@echo "✓ Build complete: $(MOD).ko"

# ── Clean ──────────────────────────────────────────────
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	@echo "✓ Cleaned"

# ── Load / Unload ──────────────────────────────────────
load:
	@echo "Loading $(MOD).ko ..."
	sudo insmod $(MOD).ko
	@echo "✓ Module loaded"
	@sleep 1
	@dmesg | grep powerkernel | tail -15

unload:
	@echo "Unloading $(MOD) ..."
	sudo rmmod $(MOD)
	@echo "✓ Module unloaded"
	@dmesg | grep powerkernel | tail -5

reload: unload load

# ── Status ─────────────────────────────────────────────
status:
	@echo "=== lsmod ==="
	@lsmod | grep powerkernel || echo "(not loaded)"
	@echo ""
	@echo "=== dmesg (last 20 lines) ==="
	@dmesg | grep powerkernel | tail -20

# ── Proc interface ─────────────────────────────────────
proc:
	@cat /proc/powerkernel

# ── Device test ────────────────────────────────────────
test:
	@echo "--- Writing to /dev/powerkernel ---"
	@echo "Hello from userspace! PID=$$$$" | sudo tee /dev/powerkernel
	@echo ""
	@echo "--- Reading from /dev/powerkernel ---"
	@cat /dev/powerkernel

# ── Sysctl tunables ────────────────────────────────────
sysctl-show:
	@sysctl kernel.powerkernel_debug kernel.powerkernel_heartbeat

sysctl-debug2:
	@sudo sysctl -w kernel.powerkernel_debug=2

# ── Module info ────────────────────────────────────────
info:
	@modinfo $(MOD).ko

# ── Live log watch ─────────────────────────────────────
watch:
	@sudo dmesg -w | grep --line-buffered powerkernel

.PHONY: all clean load unload reload status proc test info watch \
        sysctl-show sysctl-debug2
