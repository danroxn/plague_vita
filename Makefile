.PHONY: all build prepare send log test

-include .vita

PYTHON ?= python3
CMAKE ?= cmake
BUILD_DIR ?= build
JOBS ?= 2
FTP_PORT ?= 1337
COMMAND_PORT ?= 1338
APK_ARG = $(if $(APK),--apk "$(APK)",)
IP_ARG = $(if $(IP),--ip "$(IP)",)
VPK_ARG = $(if $(VPK),--vpk "$(VPK)",)

all: build

build:
	$(CMAKE) -S . -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build "$(BUILD_DIR)" --parallel "$(JOBS)"

prepare:
	$(PYTHON) tools/prepare_assets.py $(APK_ARG)

# Send via vitacompanion
# make send IP=XXX.XXX.XXX.XXX
send:
	@test -n "$(IP)" || { echo 'Set IP in .vita or use make send IP=XXX.XXX.XXX.XXX' >&2; exit 1; }
	$(PYTHON) tools/vita_send.py $(IP_ARG) \
		--ftp-port "$(FTP_PORT)" --command-port "$(COMMAND_PORT)" \
		--vpk "$(if $(VPK),$(VPK),$(BUILD_DIR)/plague-vita.vpk)"

# Run the automated hardware test against the installed wrapper.
# Requires log_level=2 in ux0:data/plague/port.cfg.
# make test IP=XXX.XXX.XXX.XXX
test:
	@test -n "$(IP)" || { echo 'Set IP in .vita or use make test IP=XXX.XXX.XXX.XXX' >&2; exit 1; }
	$(PYTHON) tools/vita_load_test.py $(IP_ARG) \
		--ftp-port "$(FTP_PORT)" --command-port "$(COMMAND_PORT)" $(VPK_ARG)

# Download the current log via VitaCompanion FTP.
# make log IP=XXX.XXX.XXX.XXX
log:
	@test -n "$(IP)" || { echo 'Set IP in .vita or use make log IP=XXX.XXX.XXX.XXX' >&2; exit 1; }
	mkdir -p work/vita-debug
	curl --fail --show-error --ftp-method nocwd \
		--output work/vita-debug/port.log \
		"ftp://$(IP):$(FTP_PORT)/ux0:/data/plague/port.log"
