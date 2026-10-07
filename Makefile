CC = clang
CFLAGS = -std=c11 -Wall -Wextra -Werror -O2
CPPFLAGS = -Iinclude
FRAMEWORKS = -framework IOKit -framework CoreFoundation
OBJECTS = lib/dock.o lib/hpm.o lib/watch.o lib/power.o

all: dockctl libdock.a libdock.dylib examples/info examples/watch

lib/hpm.o: research/macvdmtool/AppleHPMLib.h

lib/%.o: lib/%.c include/dock.h lib/internal.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c $< -o $@

libdock.a: $(OBJECTS)
	ar rcs $@ $(OBJECTS)

libdock.dylib: $(OBJECTS)
	$(CC) -dynamiclib -install_name @rpath/libdock.dylib $^ $(FRAMEWORKS) -o $@

dockctl: dockctl.c libdock.a
	$(CC) $(CPPFLAGS) $(CFLAGS) dockctl.c libdock.a $(FRAMEWORKS) -o $@

examples/%: examples/%.c libdock.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< libdock.a $(FRAMEWORKS) -o $@

research/dockctl-probe: research/dockctl-probe.c
	$(CC) $(CFLAGS) $< $(FRAMEWORKS) -o $@

tests/check: tests/check.c libdock.a
	$(CC) $(CPPFLAGS) -Ilib $(CFLAGS) $< libdock.a $(FRAMEWORKS) -o $@

research/hpm-probe: research/hpm-probe.c research/macvdmtool/AppleHPMLib.h
	$(CC) $(CFLAGS) research/hpm-probe.c -framework IOKit -framework CoreFoundation -o $@

check: all tests/check research/dockctl-probe research/hpm-probe
	./tests/check
	./research/dockctl-probe self-test
	./research/hpm-probe self-test
	python3 tests/cli.py
	python3 research/unpack-ti-pd.py --self-test

.PHONY: all check
