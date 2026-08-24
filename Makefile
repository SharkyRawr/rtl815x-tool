CC ?= clang
CFLAGS ?= -Wall -Wextra -O2 -I./src
FRAMEWORKS = -framework IOKit -framework CoreFoundation

SRC = src/rtl_core.c \
      src/rtl_diag.c \
      src/rtl_tally.c \
      src/rtl_eee.c \
      src/rtl_power.c \
      src/rtl_led.c \
      src/main.c

OBJ = $(SRC:.c=.o)
TARGET = rtl815x-tool

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(FRAMEWORKS) -o $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

.PHONY: all clean install uninstall
