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

$(OBJ): $(wildcard src/*.h)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(FRAMEWORKS) -o $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET) $(TEST_DIR)/rtl_core.o $(TEST_DIR)/rtl_tally.o $(TEST_DIR)/test_regressions

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

.PHONY: all clean install uninstall

# Mock only discovery and watch sleep; all CLI/register/feature code is real.
TEST_DIR = build/tests
TEST_FLAGS = $(CFLAGS) -Werror -g -fsanitize=address,undefined
TEST_SRC = $(filter-out src/main.c src/rtl_core.c src/rtl_tally.c,$(SRC))

$(TEST_DIR):
	mkdir -p $@

$(TEST_DIR)/rtl_core.o: src/rtl_core.c $(wildcard src/*.h) | $(TEST_DIR)
	$(CC) $(TEST_FLAGS) -Drtl_find_devices=rtl_find_devices_real -Drtl_free_devices=rtl_free_devices_real -c $< -o $@

$(TEST_DIR)/rtl_tally.o: src/rtl_tally.c $(wildcard src/*.h) | $(TEST_DIR)
	$(CC) $(TEST_FLAGS) -Dnanosleep=rtl_test_nanosleep -c $< -o $@

$(TEST_DIR)/test_regressions: tests/test_regressions.c src/main.c $(TEST_SRC) $(wildcard src/*.h) $(TEST_DIR)/rtl_core.o $(TEST_DIR)/rtl_tally.o
	$(CC) $(TEST_FLAGS) $(FRAMEWORKS) tests/test_regressions.c $(TEST_SRC) $(TEST_DIR)/rtl_core.o $(TEST_DIR)/rtl_tally.o -o $@

test: $(TEST_DIR)/test_regressions
	./$(TEST_DIR)/test_regressions

.PHONY: test
