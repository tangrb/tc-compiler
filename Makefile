BUILD_DIR := build
CMAKE := cmake

.PHONY: all vm aot test test-vm test-aot clean configure

all vm: configure
	$(CMAKE) --build $(BUILD_DIR)

configure:
	$(CMAKE) -S . -B $(BUILD_DIR)

aot: configure
	$(CMAKE) --build $(BUILD_DIR) --target aot-not-implemented

test: test-vm

test-vm: configure
	$(CMAKE) --build $(BUILD_DIR) --target check-vm

test-aot: configure
	$(CMAKE) --build $(BUILD_DIR) --target check-aot

clean:
	rm -rf $(BUILD_DIR)
