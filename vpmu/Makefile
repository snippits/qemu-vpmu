-include ../../config-host.mak
-include Makefile.objs
#'../../' and '../' are for config-host.h and config-target.h
VPMU_FLAGS=-Wall -Wno-sign-compare -g \
			-I../../ -I../ \
			-I$(SRC_PATH) -I$(SRC_PATH)/include \
			-I$(SRC_PATH)/vpmu -I$(SRC_PATH)/vpmu/include -I$(SRC_PATH)/vpmu/libs \
			-I$(SRC_PATH)/vpmu/include/packet -I$(SRC_PATH)/vpmu/include/simulator \
			-I$(SRC_PATH)/vpmu/include/arch -I$(SRC_PATH)/vpmu/include/stream \
			-I$(SRC_PATH)/vpmu/include/event-tracing \
			-I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include

ifeq ($(CONFIG_VPMU_SET),y)
VPMU_FLAGS+=-I$(SRC_PATH)/vpmu/libs/libelfin/elf -I$(SRC_PATH)/vpmu/libs/libelfin/dwarf
endif

# Target specific flags
ifeq ($(TARGET_NAME),arm)
VPMU_FLAGS+=-I$(SRC_PATH)/vpmu/include/arch/arm
VPMU_FLAGS+=-I$(SRC_PATH)/target-arm -I$(SRC_PATH)/tcg/arm/
VPMU_FLAGS+=-DCONFIG_VPMU_LONG_BITS=32
endif
ifeq ($(TARGET_NAME),x86_64)
VPMU_FLAGS+=-I$(SRC_PATH)/vpmu/include/arch/i386
VPMU_FLAGS+=-I$(SRC_PATH)/target-i386 -I$(SRC_PATH)/tcg/i386/
VPMU_FLAGS+=-DCONFIG_VPMU_LONG_BITS=64
endif

# Compiler specific flags
ifeq ($(CC),icc)
VPMU_CFLAGS+=-xHost
endif
ifeq ($(CXX),icpc)
VPMU_CXXFLAGS+=-xHost
endif

# C, CXX flags
VPMU_CFLAGS=$(VPMU_FLAGS) -O3 -std=gnu11
VPMU_CXXFLAGS=$(VPMU_FLAGS) -O3 -std=c++14

.PHONY: all clean

all	:	libvpmu_arm.a libvpmu_x86_64.a $(VPMU_EXTERNAL_LIBS)

libvpmu_arm.a	:	$(VPMU_OBJS)
	@echo "  BUILD VPMU for $(TARGET_NAME) with compiler: $(CC)"
	@echo "  LINK    $@"
	@ar -rcs $@ $^

libvpmu_x86_64.a libvpmu_i386.a	:	$(VPMU_OBJS)
	@echo "  BUILD VPMU for $(TARGET_NAME) with compiler: $(CC)"
	@echo "  LINK    $@"
	@ar -rcs $@ $^

ifeq ($(CONFIG_VPMU_SET),y)
vpmu/libs/libelfin/elf/libelf++.a	:
	@$(MAKE) CC=$(CC) CXX=$(CXX) \
		VPATH=$(SRC_PATH)/vpmu/libs/libelfin/elf \
		CXXFLAGS=-I$(SRC_PATH)/vpmu/libs/libelfin/elf \
		-C ./libs/libelfin/elf all

vpmu/libs/libelfin/dwarf/libdwarf++.a	:
	@$(MAKE) CC=$(CC) CXX=$(CXX) \
		VPATH=$(SRC_PATH)/vpmu/libs/libelfin/dwarf \
		CXXFLAGS=-I$(SRC_PATH)/vpmu/libs/libelfin/dwarf \
		-C ./libs/libelfin/dwarf all
endif

%.o	:	%.c
	@echo "  CC      $@"
	@$(CC) -c -fPIC $(VPMU_CFLAGS) $< -o $@

%.o	:	%.cc
	@echo "  CXX     $@"
	@$(CXX) -c -fPIC $(VPMU_CXXFLAGS) $< -o $@

#This ifdef checks could help to emiliminate the annoying errors from 
#QEMU's config-host.h and config-target.h when doing `make clean`
ifdef TARGET_NAME
%.d	:	%.c
	@$(CC) -MM $(VPMU_CFLAGS) $^ -o $@

%.d	:	%.cc
	@$(CXX) -MM $(VPMU_CXXFLAGS) $^ -o $@
endif

#This clean is for standalone runnable
clean  :	
	rm -f *.d *.o *.a
	@for d in $(VPMU_EXTERNAL_LIB_DIRS); do \
		if test -d ../$$d; then $(MAKE) -C ../$$d $@ || exit 1; fi; \
	done

# Include automatically generated dependency files
-include $(VPMU_DEPS)
