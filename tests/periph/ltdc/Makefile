BOARD ?= stm32f746g-disco
include ../Makefile.periph_common

FEATURES_REQUIRED += periph_ltdc

# On boards whose LTDC feeds a DSI host, the panel stays dark until its driver
# has set it up over DSI. Use tests/drivers/disp_dev on those boards.
BOARDS_UNSUPPORTED += stm32f769i-disco

USEMODULE += ztimer
USEMODULE += ztimer_msec

# add include path for riot_logo.h
INCLUDES += -I$(RIOTBASE)/tests

include $(RIOTBASE)/Makefile.include
