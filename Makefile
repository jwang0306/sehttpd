.PHONY: all check clean
TARGET = sehttpd htstress
GIT_HOOKS := .git/hooks/applied
all: $(GIT_HOOKS) $(TARGET)

$(GIT_HOOKS):
	@scripts/install-git-hooks
	@echo

include common.mk

THREADSANITIZE := 0
THPOOLFLAG := No
HAVE_SO_REUSEPORT := 1
SOCKETFLAG =
ifeq ($(HAVE_SO_REUSEPORT), 1)
SOCKETFLAG += HAVE_SO_REUSEPORT
else
SOCKETFLAG += No
endif
 

CFLAGS = -I./src
CFLAGS += -O2
CFLAGS += -std=gnu99 -Wall -W
CFLAGS += -DUNUSED="__attribute__((unused))"
CFLAGS += -DNDEBUG
LDFLAGS_user = -lpthread
LDFLAGS =

ifeq ($(THREADSANITIZE), 1)
CFLAGS += -fsanitize=thread
LDFLAGS += -fsanitize=thread
else
LDFLAGS += -lpthread
endif

# standard build rules
.SUFFIXES: .o .c
.c.o:
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) -c -MMD -MF $@.d $< -D $(THPOOLFLAG) -D $(SOCKETFLAG)

OBJS = \
    src/http.o \
    src/http_parser.o \
    src/http_request.o \
    src/timer.o \
    src/thpool.o \
    src/lf_thpool.o \
    src/mainloop.o
deps += $(OBJS:%.o=%.o.d)

$(TARGET): $(OBJS)
	$(VECHO) "  LD\t$@\n"
	$(Q)$(CC) -o $@ $^ $(LDFLAGS)

htstress: htstress.c
	$(CC) $(CFLAGS_user) -o $@ $< $(LDFLAGS_user)

check: all
	@scripts/test.sh

clean:
	$(VECHO) "  Cleaning...\n"
	$(Q)$(RM) $(TARGET) $(OBJS) $(deps)

-include $(deps)
