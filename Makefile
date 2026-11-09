# tome_xad - XAD Client for Apple installer "tome" archives
#
# This builds a shared library (.library) for AmigaOS / MorphOS.
# An XAD client does NOT link against xadmaster - it opens it at runtime.
#
# Requirements:
#   - Amiga cross-compiler (m68k-amigaos-gcc, ppc-amigaos-gcc, or MorphOS gcc)
#
# Build targets:
#   make                  - build Tome (m68k AmigaOS 3.x)
#   make ARCH=os4         - build for AmigaOS 4.x (PPC)
#   make ARCH=morphos     - build for MorphOS (PPC)
#   make clean            - remove build artifacts
#   make install          - install to LIBS:xad/

CC      = m68k-amigaos-gcc
CFLAGS  = -std=c89 -Wall -Wextra -pedantic -O2 -fbaserel
LDFLAGS =
PREFIX  = LIBS:xad
TARGET  = Tome

SRCS    = tome_xad.c
OBJS    = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -shared -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c xadClient.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	copy $(TARGET) $(PREFIX)/$(TARGET)

uninstall:
	delete $(PREFIX)/$(TARGET)

ifeq ($(ARCH),os4)
    CC      = ppc-amigaos-gcc
    CFLAGS  = -std=c89 -Wall -Wextra -pedantic -O2 -D__AMIGAOS4__
else ifeq ($(ARCH),morphos)
    CC      = ppc-morphos-gcc
    CFLAGS  = -std=c89 -Wall -Wextra -pedantic -O2 -D__MORPHOS__
endif

.PHONY: all clean install uninstall
