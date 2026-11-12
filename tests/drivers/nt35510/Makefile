BOARD ?= stm32f769i-disco
include ../Makefile.drivers_common

DISABLE_MODULE += test_utils_interactive_sync

USEMODULE += nt35510
USEMODULE += ztimer
USEMODULE += ztimer_msec

# add include path for riot_logo.h
INCLUDES += -I$(RIOTBASE)/tests

include $(RIOTBASE)/Makefile.include
