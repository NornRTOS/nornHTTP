CC ?= cc
AR ?= ar
CFLAGS ?= -Os
WARN = -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
SRC = src/norn_http.c src/norn_http_request.c src/norn_http_parser.c src/norn_http_stream.c
OBJ = $(SRC:src/%.c=build/%.o)
SANITIZE ?= -fsanitize=address,undefined -fno-omit-frame-pointer
.PHONY: all test host-test fuzz analyze size
all: build/libnornHTTP.a
build:
	mkdir -p $@
build/%.o: src/%.c include/*.h src/internal.h | build
	$(CC) $(WARN) $(CFLAGS) -ffunction-sections -fdata-sections -fstack-usage -Iinclude -c $< -o $@
build/libnornHTTP.a: $(OBJ)
	$(AR) rcs $@ $^
build/test_http: tests/test_http.c $(SRC) include/*.h src/internal.h | build
	$(CC) $(WARN) -g -O1 $(SANITIZE) -Iinclude tests/test_http.c $(SRC) -o $@
test host-test: build/test_http
	./build/test_http
fuzz: | build
	clang $(WARN) -g -O1 -fsanitize=fuzzer,address,undefined -Iinclude tests/fuzz_parser.c $(SRC) -o build/fuzz_parser
analyze: | build
	@for source in $(SRC); do $(CC) $(WARN) -fanalyzer -Iinclude -c $$source -o build/analyze_$$(basename $$source .c).o || exit 1; done
size: all
	size $(OBJ)
.PHONY: integration fuzz-smoke adapter-check
build/posix_client: examples/posix_client.c build/libnornHTTP.a
	$(CC) $(WARN) -O2 -Iinclude $< build/libnornHTTP.a -o $@
integration: build/posix_client
	python3 tests/integration.py
build/fuzz_smoke: tests/fuzz_parser.c tests/fuzz_smoke.c $(SRC) include/*.h src/internal.h | build
	$(CC) $(WARN) -g -O1 $(SANITIZE) -Iinclude tests/fuzz_parser.c tests/fuzz_smoke.c $(SRC) -o $@
fuzz-smoke: build/fuzz_smoke
	./build/fuzz_smoke
NORN_NET ?= ../nornNET
adapter-check: | build
	$(CC) $(WARN) -DNN_OS_POSIX -DNN_CFG_TASK_PRIO=1 -Iinclude -I$(NORN_NET)/nn -I$(NORN_NET)/os -c adapters/norn_net_transport.c -o build/norn_net_transport.o
.PHONY: adapter-test esp32-check
adapter-test: | build
	$(CC) $(WARN) -DNN_OS_POSIX -DNN_CFG_TASK_PRIO=1 -Iinclude -Iadapters -I$(NORN_NET)/nn -I$(NORN_NET)/os tests/test_adapter.c adapters/norn_net_transport.c -o build/test_adapter
	./build/test_adapter
ESP_CC ?= xtensa-esp32-elf-gcc
ESP_SIZE ?= xtensa-esp32-elf-size
esp32-check: | build
	mkdir -p build/esp32
	@for source in $(SRC); do $(ESP_CC) $(WARN) -Os -ffunction-sections -fdata-sections -fstack-usage -Iinclude -c $$source -o build/esp32/$$(basename $$source .c).o || exit 1; done
	$(ESP_SIZE) build/esp32/*.o
