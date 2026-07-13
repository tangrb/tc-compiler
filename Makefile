BUILD_DIR := build
CMAKE := cmake

.PHONY: all vm aot test test-vm test-unit test-aot test-coverage test-valgrind test-leaks memcheck-macos bench clean configure hooks ci ci-coverage build-asan build-ubsan

all vm: configure
	$(CMAKE) --build $(BUILD_DIR)

configure:
	$(CMAKE) -S . -B $(BUILD_DIR)

aot: configure
	$(CMAKE) --build $(BUILD_DIR) --target tc-aot

build-asan:
	$(CMAKE) -S . -B build-asan -DCMAKE_C_FLAGS="-fsanitize=address -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
	$(CMAKE) --build build-asan

build-ubsan:
	$(CMAKE) -S . -B build-ubsan -DCMAKE_C_FLAGS="-fsanitize=undefined -fno-sanitize-recover=undefined -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=undefined"
	$(CMAKE) --build build-ubsan

test: test-vm test-unit test-aot

test-vm: configure
	$(CMAKE) --build $(BUILD_DIR) --target check-vm

test-unit: configure
	$(CMAKE) --build $(BUILD_DIR) --target check-unit

test-aot: configure
	$(CMAKE) --build $(BUILD_DIR) --target check-aot

test-coverage:
	sh scripts/ci.sh --coverage

test-valgrind: vm
	bash scripts/run_tests.sh --valgrind

test-leaks: vm
	bash scripts/run_tests.sh --leaks

memcheck-macos: vm
	bash scripts/run_memcheck_macos.sh

ci:
	sh scripts/ci.sh

ci-coverage:
	sh scripts/ci.sh --coverage

bench:
	sh scripts/vm/gen_bench_baseline.sh
	sh scripts/vm/bench.sh

clean:
	rm -rf $(BUILD_DIR)

hooks:
	bash scripts/install-git-hooks.sh
