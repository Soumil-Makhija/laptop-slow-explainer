CC = cc
CFLAGS = -O2 -std=c11 -Wall -Wextra -Werror -pedantic -D_POSIX_C_SOURCE=200809L
CPPFLAGS = -Isrc
SOURCES = src/main.c src/analyze.c src/collect_macos.c src/units.c

.PHONY: all test sanitize clean
all: build/slow-explain

build:
	mkdir -p build

build/slow-explain: $(SOURCES) src/slow.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDFLAGS) -o $@

build/test-analyze: tests/test_analyze.c src/analyze.c src/slow.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_analyze.c src/analyze.c $(LDFLAGS) -o $@

build/test-units: tests/test_units.c src/units.c src/slow.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_units.c src/units.c $(LDFLAGS) -o $@

test: all build/test-analyze build/test-units
	./build/test-analyze
	./build/test-units
	python3 tests/test_cli.py

sanitize: | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined tests/test_analyze.c src/analyze.c -o build/test-sanitize
	./build/test-sanitize
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined tests/test_units.c src/units.c -o build/test-units-sanitize
	./build/test-units-sanitize

clean:
	rm -rf build
