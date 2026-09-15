SDK := $(shell xcrun --show-sdk-path)
SYSROOT := /Users/admin/panthera/userland/libsystem/build/sysroot
SRC := /Users/admin/panthera/src/libmalloc-474.0.13
XNU := /Users/admin/panthera/src/xnu-10002.41.9
OBJDIR := /Users/admin/panthera/userland/libsystem/build/obj/libsystem_malloc
OUTDIR := /Users/admin/panthera/userland/libsystem/build/sysroot/usr/lib/system
SHIMS := $(OBJDIR)/panthera_malloc_shims.h
STUB_BINDER := /Users/admin/panthera/userland/libsystem/build/obj/dyld_stub_binder.o

CC := clang
TARGET := x86_64-apple-darwin23.0

CFLAGS := -target $(TARGET) -std=gnu11 -c -fPIC -O2 \
  -include $(SHIMS) \
  -I$(SRC)/src \
  -I$(SRC)/include \
  -I$(SRC)/private \
  -I$(SRC)/resolver \
  -I$(SYSROOT)/usr/include \
  -I$(XNU)/osfmk \
  -I$(XNU)/EXTERNAL_HEADERS \
  -I$(SDK)/usr/include \
  -DPRIVATE -D__DARWIN_UNIX03=1 \
  -Wno-everything

SRCS := malloc.c malloc_common.c malloc_printf.c malloc_type.c \
  magazine_malloc.c magazine_rack.c magazine_large.c \
  magazine_small.c magazine_tiny.c magazine_medium.c \
  nano_malloc_common.c nanov2_malloc.c purgeable_malloc.c \
  frozen_malloc.c legacy_malloc.c vm.c bitarray.c \
  pgm_malloc.c sanitizer_malloc.c stack_trace.c \
  early_malloc.c has_section.c msl_lite_support.c

OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))

DYLIB := $(OUTDIR)/libsystem_malloc.dylib

.PHONY: all clean report

all: $(DYLIB) report

$(OBJDIR)/%.o: $(SRC)/src/%.c $(SHIMS)
	@echo "  CC $<"
	@$(CC) $(CFLAGS) $< -o $@ 2>&1 || (echo "  FAILED: $<" && touch $@.failed && true)

$(DYLIB): $(OBJS)
	@echo "=== Linking libsystem_malloc.dylib ==="
	@GOOD_OBJS=""; for o in $(OBJS); do if [ ! -f "$$o.failed" ]; then GOOD_OBJS="$$GOOD_OBJS $$o"; fi; done; \
	ld -arch x86_64 -dylib \
	  -install_name /usr/lib/system/libsystem_malloc.dylib \
	  -no_uuid \
	  -undefined dynamic_lookup \
	  -not_for_dyld_shared_cache \
	  -L$(OUTDIR) \
	  -platform_version macos 14.0 14.0 \
	  $(STUB_BINDER) \
	  $$GOOD_OBJS \
	  -o $@

report:
	@echo "=== Results ==="
	@file $(DYLIB)
	@ls -la $(DYLIB)
	@echo ""
	@echo "Exported symbol count:"
	@nm -gU $(DYLIB) | wc -l
	@echo ""
	@echo "First 40 exported symbols:"
	@nm -gU $(DYLIB) | head -40
	@echo ""
	@echo "Size breakdown:"
	@size $(DYLIB)

clean:
	rm -f $(OBJS) $(DYLIB) $(OBJDIR)/*.failed
