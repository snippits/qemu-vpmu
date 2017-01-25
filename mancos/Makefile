-include ../../config-host.mak
-include Makefile.objs
VPATH=$(SRC_PATH):$(SRC_PATH)/mancos

# Compiler specific flags
ifeq ($(CC),icc)
MANCOS_CFLAGS+=-xHost
endif
ifeq ($(CXX),icpc)
MANCOS_CXXFLAGS+=-xHost
endif


#MANCOS_FLAGS=-Wall -Wno-sign-compare -g \
#			-I../../ -I../ \
#			-I$(SRC_PATH) -I$(SRC_PATH)/include \
#			-I$(SRC_PATH)/vpmu -I$(SRC_PATH)/vpmu/include -I$(SRC_PATH)/vpmu/libs \
#			-I$(SRC_PATH)/vpmu/include/packet -I$(SRC_PATH)/vpmu/include/simulator \
#			-I$(SRC_PATH)/vpmu/include/arch -I$(SRC_PATH)/vpmu/include/stream \
#			-I$(SRC_PATH)/vpmu/include/event-tracing \
#			-I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include
#


MANCOS_CFLAGS = -I../../ -I../ -I$(SRC_PATH) -I$(SRC_PATH)/target-i386 -I$(SRC_PATH)/include \
			         -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include \
			         -std=gnu11 -O3 \
                     -I/opt/mancos/include -L/opt/mancos/lib -lmancos-scatter 
.PHONY: all clean

all	: libmancos.a 


libmancos.a	:	$(MANCOS_OBJS)
	@echo "  BUILD MANCOS for $(TARGET_NAME)"
	@echo "  LINK  $@"
	@ar -rcs $@ $(MANCOS_OBJS)
%.o	:	%.c
	@echo "  CC      $@"
	@$(CC) -c -fPIC  $(MANCOS_CFLAGS) $< -o $@

#This ifdef checks could help to emiliminate the annoying errors from 
#QEMU's config-host.h and config-target.h when doing `make clean`
ifdef TARGET_NAME
%.d	:	%.c
	@$(CC) -MM $(MANCOS_CFLAGS) $^ -o $@
endif


#This clean is for standalone runnable
clean  :	
	rm -f *.d *.o *.a

# Include automatically generated dependency files
-include $(MANCOS_DEPS)
