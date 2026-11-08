ROOT := $(abspath ..)
PS5_PAYLOAD_SDK ?= $(ROOT)/.deps/ps5-payload-sdk
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
CFLAGS := -Wall -Wextra -Werror -O2 -g -std=gnu11 -fPIC -fno-stack-protector -MMD -MP -D__PROSPERO__
LDFLAGS := -lpthread
SRC := main.c log.c proc.c target.c upgrade.c hook.c kstuff.c stub.c inject.c settings.c web.c
OBJ := $(SRC:%.c=build/%.o)
VERSION := $(shell sed -n 's/^\#define TURBO_VERSION "\(.*\)"/\1/p' version.h)
RELEASE := ps5-turbo-v$(VERSION).elf
.PHONY: all clean check-stub
all: ps5-turbo.elf check-stub $(RELEASE)
$(RELEASE): ps5-turbo.elf version.h
	cp ps5-turbo.elf $@
ps5-turbo.elf: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)
build/web.o: web_page.h
web_page.h: web/portal.html version.h $(ROOT)/tools/gen_page.py
	python3 $(ROOT)/tools/gen_page.py $< $@
build/stub.o: stub.c stub.h | build
	$(CC) $(CFLAGS) -fno-vectorize -fno-slp-vectorize -c -o $@ $<
build/%.o: %.c | build
	$(CC) $(CFLAGS) -c -o $@ $<
build:
	mkdir -p build
check-stub: build/stub.o
	python3 $(ROOT)/tools/check_stub.py $<
clean:
	rm -rf build ps5-turbo.elf
-include $(OBJ:.o=.d)
