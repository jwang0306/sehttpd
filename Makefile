.PHONY: all check clean
TARGET = sehttpd
GIT_HOOKS := .git/hooks/applied
all: $(GIT_HOOKS) $(TARGET) htstress

$(GIT_HOOKS):
	@scripts/install-git-hooks
	@echo

include common.mk

THREADSANITIZE := 0
ENABLE_SO_REUSEPORT := 0
ENABLE_THPOOL := 0

THPOOLFLAG = LF_THPOOL

CFLAGS += -I./src
CFLAGS += -O2
CFLAGS += -std=gnu99 -Wall -W
CFLAGS += -DUNUSED="__attribute__((unused))"
CFLAGS += -DNDEBUG
LDFLAGS =

ifeq ($(THREADSANITIZE), 1)
	CFLAGS += -fsanitize=thread
	LDFLAGS += -fsanitize=thread
else
	LDFLAGS += -lpthread
endif

ifeq ($(ENABLE_SO_REUSEPORT), 1)
	CFLAGS += -D ENABLE_SO_REUSEPORT
endif

CFLAG_HTSTRESS += -std=gnu99 -Wall -Werror -Wextra -lpthread

# standard build rules
.SUFFIXES: .o .c
.c.o:
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) -c -MMD -MF $@.d $<

OBJS = \
    src/http.o \
    src/http_parser.o \
    src/http_request.o \
    src/timer.o \
    src/mainloop.o

ifeq ($(ENABLE_THPOOL), 1)
ifeq ($(THPOOLFLAG), THPOOL)
	OBJS += src/thpool.o
endif
ifeq ($(THPOOLFLAG), LF_THPOOL)
	OBJS += src/lf_thpool.o
endif
	CFLAGS += -D ENABLE_THPOOL
	CFLAGS += -D $(THPOOLFLAG)
endif

deps += $(OBJS:%.o=%.o.d)

$(TARGET): $(OBJS)
	$(VECHO) "  LD\t$@\n"
	$(Q)$(CC) -o $@ $^ $(LDFLAGS)

htstress: htstress.c
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $< $(CFLAG_HTSTRESS)

check: all
	@scripts/test.sh

clean:
	$(VECHO) "  Cleaning...\n"
	$(Q)$(RM) $(TARGET) $(OBJS) $(deps) htstress

-include $(deps)
