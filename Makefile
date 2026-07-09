BUILD_DIR := build
CMAKE := cmake

.PHONY: all vm aot test test-vm test-unit test-aot bench clean configure hooks ci ci-coverage

all vm: configure
	$(CMAKE) --build $(BUILD_DIR)

configure:
	$(CMAKE) -S . -B $(BUILD_DIR)

aot: configure
	$(CMAKE) --build $(BUILD_DIR) --target tc-aot

test: test-vm test-unit test-aot

test-vm: configure
	$(CMAKE) --build $(BUILD_DIR) --target check-vm

test-unit: configure
	$(CMAKE) --build $(BUILD_DIR) --target check-unit

test-aot: configure
	$(CMAKE) --build $(BUILD_DIR) --target check-aot

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
