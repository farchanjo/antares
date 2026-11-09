# Thin forwarder: the presets are the interface, this only shortens the typing.
.PHONY: all check asan leak lint clean

all:
	cmake --preset dev && cmake --build --preset dev

check: all
	ctest --preset dev

asan:
	cmake --build --preset dev --target asan

leak:
	cmake --build --preset dev --target leak

lint:
	cmake --build --preset dev --target lint

clean:
	rm -rf build build-asan
