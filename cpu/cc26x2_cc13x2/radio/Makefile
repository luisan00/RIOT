MODULE = cc26x2_cc13x2_rf

# One of the two drivers: the RF Core works in one band at a time
ifneq (,$(filter cc26x2_cc13x2_rf_24ghz,$(USEMODULE)))
  SRC := cc26x2_cc13x2_rf_ieee.c
else
  SRC := cc26x2_cc13x2_rf_internal.c cc26x2_cc13x2_rf_radio_ops.c
endif
SRC += cc26x2_cc13x2_rf_eui64.c

include $(RIOTBASE)/Makefile.base
