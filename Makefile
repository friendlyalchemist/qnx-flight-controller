# Build for QNX Neutrino x86_64 with qcc (default) or for a Linux host (syntax check only).
#
#   QNX:   load the SDP environment first (qnxsdp-env.sh / qnxsdp-env.bat), then
#            make                       (uses qcc)
#            make VARIANT=<value>       (if the default variant is not valid for your SDK)
#          List valid variants for YOUR SDK with:  qcc -V
#          Typical x86_64: gcc_ntox86_64 (SDP 7.x), 12.2.0_x86_64 (SDP 8.0)
#
#   Host:  make host                    (gcc; for compile checks only, NOT a QNX result)

VARIANT ?= gcc_ntox86_64
TARGET  := priority_demo
SRCS    := src/priority_demo.c src/evlog.c src/timeutil.c
INCS    := -Iinclude
WARN    := -Wall -Wextra

all: $(TARGET)

$(TARGET): $(SRCS) include/evlog.h include/timeutil.h
	qcc -V$(VARIANT) $(WARN) -O1 $(INCS) -o $@ $(SRCS)

host: $(SRCS) include/evlog.h include/timeutil.h
	gcc $(WARN) -O1 -pthread $(INCS) -o $(TARGET)_host $(SRCS)

clean:
	rm -f $(TARGET) $(TARGET)_host

.PHONY: all host clean
