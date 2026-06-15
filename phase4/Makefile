RV_CXX   = riscv64-linux-gnu-g++
RV_FLAGS = -march=rv64gcv -static -I src
BUILD    = build_rv
SRC      = src/benchmark.cpp
QEMU     = qemu-riscv64 -cpu rv64

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/bench_O0: $(SRC) | $(BUILD)
	$(RV_CXX) -O0 $(RV_FLAGS) $< -o $@

$(BUILD)/bench_O1: $(SRC) | $(BUILD)
	$(RV_CXX) -O1 $(RV_FLAGS) $< -o $@

$(BUILD)/bench_O2: $(SRC) | $(BUILD)
	$(RV_CXX) -O2 $(RV_FLAGS) $< -o $@

$(BUILD)/bench_O3: $(SRC) | $(BUILD)
	$(RV_CXX) -O3 $(RV_FLAGS) $< -o $@

$(BUILD)/bench_O3_vec: $(SRC) | $(BUILD)
	$(RV_CXX) -O3 -ftree-vectorize $(RV_FLAGS) $< -o $@ 2> vec_report.txt

sweep: $(BUILD)/bench_O0 $(BUILD)/bench_O1 $(BUILD)/bench_O2 $(BUILD)/bench_O3 $(BUILD)/bench_O3_vec
	@echo "====== Timing Results ======"
	@$(QEMU) $(BUILD)/bench_O0
	@$(QEMU) $(BUILD)/bench_O1
	@$(QEMU) $(BUILD)/bench_O2
	@$(QEMU) $(BUILD)/bench_O3
	@$(QEMU) $(BUILD)/bench_O3_vec

sizes: $(BUILD)/bench_O0 $(BUILD)/bench_O1 $(BUILD)/bench_O2 $(BUILD)/bench_O3 $(BUILD)/bench_O3_vec
	@echo "====== Binary Sizes ======"
	@for f in O0 O1 O2 O3 O3_vec; do printf '%-15s %s bytes\n' $$f $$(stat -c%s $(BUILD)/bench_$$f); done

vec_count: $(BUILD)/bench_O3_vec
	@echo "====== Vector Instructions ======"
	@riscv64-linux-gnu-objdump -d $(BUILD)/bench_O3_vec | grep -c vset || true

clean:
	rm -rf $(BUILD) vec_report.txt
