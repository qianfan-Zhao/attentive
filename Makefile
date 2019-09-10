# Copyright © 2014 Kosma Moczek <kosma@cloudyourcar.com>
# This program is free software. It comes without any warranty, to the extent
# permitted by applicable law. You can redistribute it and/or modify it under
# the terms of the Do What The Fuck You Want To Public License, Version 2, as
# published by Sam Hocevar. See the COPYING file for more details.

CFLAGS = $(shell pkg-config --cflags $(LIBRARIES)) -std=gnu99 -g -Wall -Wextra -Wno-unused-parameter -Iinclude -fPIC
LDLIBS = $(shell pkg-config --libs $(LIBRARIES)) -L. -lattentive

LIBRARIES = check glib-2.0

all: libattentive.so example
	@echo "+++ All good."""

libattentive.so:
	$(CC) -shared $^ -o $@

test: tests/test-parser
	@echo "+++ Running parser test suite."
	tests/test-parser

example: example/basic

clean:
	$(RM) src/example-basic tests/test-parser
	$(RM) src/*.o example/*.o tests/*.o libattentive.so

PARSER = include/parser.h
AT = include/at.h include/at-unix.h $(PARSER)

src/parser.o: src/parser.c $(PARSER)
src/at-unix.o: src/at-unix.c $(AT)
tests/test-parser.o: tests/test-parser.c $(AT)
example/basic.o: example/basic.c $(AT)

libattentive.so: src/parser.o src/at-unix.o
tests/test-parser: tests/test-parser.o
example/basic: example/basic.o

.PHONY: all example test clean
