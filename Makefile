# Makefile for Tharavu DataEngine
# Works on Windows (MinGW/MSYS2, PowerShell) and Linux/macOS

CC = gcc
HARDENING = -D_FORTIFY_SOURCE=2 -fstack-protector-strong -Wformat -Wformat-security -Werror=format-security
CFLAGS = -std=c99 -Wall -Wextra -O2 -I./src/include $(HARDENING)
AR = ar
ARFLAGS = rcs

# Detect platform
ifeq ($(OS),Windows_NT)
    MKDIR = powershell -Command "New-Item -ItemType Directory -Force 'build/obj','build' | Out-Null"
    RMDIR = powershell -Command "if (Test-Path build) { Remove-Item -Recurse -Force build }"
    RM = powershell -Command "Remove-Item -Force -ErrorAction SilentlyContinue"
    DLL         = build/tharavu.dll
    IMPLIB      = build/libtharavu_dll.a
    DLL_LDFLAGS = -shared -Wl,--out-implib,$(IMPLIB) src/tharavu.def
    VISIBILITY  =
else
    MKDIR = mkdir -p build/obj
    RMDIR = rm -rf build
    RM = rm -f
    DLL         = build/libtharavu.so
    IMPLIB      = build/libtharavu.a
    DLL_LDFLAGS = -shared -fPIC -Wl,--out-implib,$(IMPLIB)
    VISIBILITY  = -fvisibility=hidden
endif

SRCS     = src/data_engine.c src/platform.c
SRCS_DLL = $(SRCS) src/tharavu_dll.c
OBJS     = $(patsubst src/%.c,build/obj/%.o,$(SRCS))

LIB    = build/libtharavu.a
DUMP   = build/de_dump.exe
IMPORT = build/de_import.exe
CRUD   = build/de_crud.exe

TEST_SRCS = $(wildcard test/*.c)
TEST_BINS = $(patsubst test/%.c,build/%.exe,$(TEST_SRCS))

# Default target: build everything
all: dirs $(LIB) $(DLL) $(DUMP) $(IMPORT) $(CRUD)

dirs:
	$(MKDIR)

$(LIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

$(DLL): $(SRCS_DLL)
	$(CC) $(CFLAGS) -DTHARAVU_EXPORTS $(VISIBILITY) $(DLL_LDFLAGS) -o $@ $^

build/obj/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(DUMP): src/tools/de_dump.c
	$(CC) $(CFLAGS) -o $@ $<

$(CRUD): src/tools/de_crud.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(OBJS)

$(IMPORT): src/tools/de_import.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(OBJS)

# Build pattern for test binaries (test/*.c → build/*.exe)
build/%.exe: test/%.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(OBJS)

# Run all tests from test/
test: dirs $(LIB) $(TEST_BINS)
	@exit_code=0; \
	for t in $(TEST_BINS); do \
		echo "  RUN  $$t"; \
		$$t; rc=$$?; \
		if [ $$rc -eq 0 ]; then echo "  PASS $$t"; \
		elif [ $$rc -eq 77 ]; then echo "  SKIP $$t"; \
		else echo "  FAIL $$t"; exit_code=1; fi; \
	done; \
	exit $$exit_code

docs/sample/create_sample_db: docs/sample/create_sample_db.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(OBJS)

clean:
	-$(RMDIR)

.PHONY: all build clean dirs dll test

build: all

dll: dirs $(DLL)
