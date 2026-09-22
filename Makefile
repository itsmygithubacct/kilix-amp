# kilix-amp - Winamp 2.x clone in C11 + SDL2
# Build:   make            (release)
#          make debug      (asan + debug info)
#          make test       (build + run unit tests)

CC      ?= gcc
PKGS     = sdl2 SDL2_image sndfile zlib fluidsynth
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -Wshadow -MMD -MP -D_GNU_SOURCE
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LDLIBS   = $(shell pkg-config --libs $(PKGS)) -lm -lpthread
ENCODEC ?= 0
ifeq ($(ENCODEC),1)
ENCODEC_CFLAGS ?= $(shell pkg-config --cflags kilix-encodec)
ENCODEC_LIBS ?= $(shell pkg-config --libs kilix-encodec)
CFLAGS += -DKA_WITH_ENCODEC $(ENCODEC_CFLAGS)
LDLIBS += $(ENCODEC_LIBS)
endif

BIN      = kilix-amp
SRCDIR   = src
OBJDIR   = $(if $(filter 1,$(ENCODEC)),build-encodec,build)
TESTDIR  = tests

SRCS     = $(wildcard $(SRCDIR)/*.c)
OBJS     = $(SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
# Objects shared with the test binaries (everything except main.c)
LIBOBJS  = $(filter-out $(OBJDIR)/main.o,$(OBJS))

TESTSRCS = $(wildcard $(TESTDIR)/test_*.c)
TESTBINS = $(TESTSRCS:$(TESTDIR)/%.c=$(OBJDIR)/%)

all: $(BIN)

$(BIN): $(OBJS) FORCE
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

debug: CFLAGS := $(filter-out -O2,$(CFLAGS)) -O0 -g -fsanitize=address,undefined
debug: LDLIBS += -fsanitize=address,undefined
debug: clean $(BIN)

$(OBJDIR)/test_%: $(TESTDIR)/test_%.c $(LIBOBJS)
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ $< $(LIBOBJS) $(LDLIBS)

$(OBJDIR)/native_encodec: $(TESTDIR)/native_encodec.c $(LIBOBJS)
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ $< $(LIBOBJS) $(LDLIBS)

$(OBJDIR)/native_live: $(TESTDIR)/native_live.c $(LIBOBJS)
	$(CC) $(CFLAGS) -I$(SRCDIR) -o $@ $< $(LIBOBJS) $(LDLIBS)

# Observation-only LD_PRELOAD probe for tests/headless_admission_stereo.py (ENCODEC=1).
$(OBJDIR)/admission_probe.so: $(TESTDIR)/admission_probe.c | $(OBJDIR)
	$(CC) $(CFLAGS) -shared -fPIC -o $@ $< -ldl

test: $(TESTBINS)
	@fail=0; for t in $(TESTBINS); do \
		echo "== $$t"; $$t || fail=1; \
	done; \
	if [ $$fail -eq 0 ]; then echo "ALL TESTS PASSED"; else echo "FAILURES"; exit 1; fi

clean:
	rm -rf build build-encodec $(BIN)

-include $(OBJS:.o=.d)

FORCE:
.PHONY: all debug test clean FORCE
