.PHONY: all check clean
TARGET = sehttpd htstress
GIT_HOOKS := .git/hooks/applied
all: $(GIT_HOOKS) $(TARGET)

$(GIT_HOOKS):
	@scripts/install-git-hooks
	@echo

include common.mk

CFLAGS = -I./src
CFLAGS += -O2
CFLAGS += -std=gnu99 -Wall -W
CFLAGS += -DUNUSED="__attribute__((unused))"
CFLAGS += -DNDEBUG
CFLAGS += -fsanitize=thread
LDFLAGS_user = -lpthread
LDFLAGS = 
LDFLAGS += -fsanitize=thread

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
