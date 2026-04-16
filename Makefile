# Makefile for Tharavu DataEngine
# Works on Windows (MinGW/MSYS2, PowerShell) and Linux/macOS

CC = gcc
# Security hardening flags:
#   -D_FORTIFY_SOURCE=2  : enables buffer-overflow detection in glibc wrappers
#   -fstack-protector-strong : stack canaries on functions with buffers
#   -Wformat-security    : warn on format-string vulnerabilities
#   -Werror=format-security : make format-string issues a hard error
HARDENING = -D_FORTIFY_SOURCE=2 -fstack-protector-strong -Wformat -Wformat-security -Werror=format-security
CFLAGS = -std=c99 -Wall -Wextra -O2 -I./include $(HARDENING)
AR = ar
ARFLAGS = rcs

# Detect platform — all conditionals at the top level, never inside recipes.
# Shell is always bash (MinGW/MSYS2 on Windows, bash on POSIX), so we use
# bash-compatible commands (mkdir -p, rm -f) on all platforms.
# For PowerShell compatibility, we use PowerShell commands.
ifeq ($(OS),Windows_NT)
    MKDIR = powershell -Command "if (!(Test-Path obj)) { New-Item -ItemType Directory -Path obj }"
    RMDIR = powershell -Command "if (Test-Path obj) { Remove-Item -Recurse -Force obj }"
    RM = powershell -Command "Remove-Item -Force -ErrorAction SilentlyContinue"
    DLL         = tharavu.dll
    IMPLIB      = libtharavu_dll.a
    DLL_LDFLAGS = -shared -Wl,--out-implib,$(IMPLIB)
    VISIBILITY  =
else
    MKDIR = mkdir -p obj
    RMDIR = rm -rf obj
    RM = rm -f
    DLL         = libtharavu.so
    IMPLIB      =
    DLL_LDFLAGS = -shared -fPIC
    VISIBILITY  = -fvisibility=hidden
endif

# Define Source Files explicitly
SRCS     = src/data_engine.c src/platform.c
SRCS_DLL = $(SRCS) src/tharavu_dll.c
OBJS     = $(SRCS:.c=.o)

# Targets
LIB = libtharavu.a
DUMP = de_dump.exe
TEST = test_engine.exe
IMPORT = de_import.exe
# BENCH = bench_speed.exe
CRUD = de_crud.exe
TEST_LOGICAL = test_logical.exe

# Default target: Build everything (static lib + DLL + tools)
all: dirs $(LIB) $(DLL) $(DUMP) $(TEST) $(IMPORT) $(CRUD) $(TEST_LOGICAL)

# Create object directories if needed
dirs:
	$(MKDIR)

# Rule to build the static library
$(LIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

# Rule to build the shared library / DLL
# THARAVU_EXPORTS triggers __declspec(dllexport) on Windows.
# VISIBILITY (-fvisibility=hidden on POSIX) limits exports to THARAVU_API symbols.
$(DLL): $(SRCS_DLL)
	$(CC) $(CFLAGS) -DTHARAVU_EXPORTS $(VISIBILITY) $(DLL_LDFLAGS) -o $@ $^

# Generic rule to compile .c to .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Build de_dump tool
$(DUMP): tools/de_dump.c
	$(CC) $(CFLAGS) -o $@ $<

# Build test suite
$(TEST): tests/test_engine.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(OBJS)

# Add this rule at the bottom (or near other tool rules)
$(CRUD): tools/de_crud.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(OBJS)

# Build de_import tool (CSV -> ODAT)
# FIXED: Uses $(OBJS) instead of undefined variables
$(IMPORT): tools/de_import.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(OBJS)

$(BENCH): tools/bench_speed.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(OBJS)

$(TEST_LOGICAL): tools/test_logical.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(OBJS)

# Build sample generator
sample/create_sample_db: sample/create_sample_db.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(OBJS)

# Clean up
clean:
	-$(RM) src/*.o
	-$(RM) *.a
	-$(RM) *.dll
	-$(RM) *.so
	-$(RM) *.exe
	-$(RMDIR)

.PHONY: all clean dirs dll

# Convenience alias — build only the DLL/SO without tools
dll: dirs $(DLL)