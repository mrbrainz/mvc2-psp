# Debug-only kernel exception reporter (see exc.c).  make -C port/psp/exc
TARGET = mvc2exc
OBJS = exc.o
BUILD_PRX = 1
USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1
CFLAGS = -O2 -G0 -Wall
LIBS = -lpspdebug -lpspdisplay_driver -lpspge_driver
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak
