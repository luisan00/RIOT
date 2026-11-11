BOARD ?= stm32f429i-disc1
include ../Makefile.pkg_common

# No interactive_sync
DISABLE_MODULE += test_utils_interactive_sync

USEPKG += lvgl
USEMODULE += lvgl_contrib
USEMODULE += lvgl_extra_widget_chart
USEMODULE += lvgl_extra_widget_win
USEMODULE += lvgl_extra_layout_flex
USEMODULE += lvgl_extra_theme_default
USEMODULE += lvgl_extra_theme_default_dark

include $(RIOTBASE)/Makefile.include

# SDL requires more stack
ifneq (,$(filter native native32 native64,$(BOARD)))
  CFLAGS += -DTHREAD_STACKSIZE_MAIN=64*1024
else ifneq (,$(filter esp%,$(CPU_FAM)))
  CFLAGS += -DTHREAD_STACKSIZE_MAIN=4*1024
else ifneq (,$(filter stm32f769i-disco,$(BOARD)))
  # LVGL at 800x480 needs more than the 2 KiB used for the other boards
  CFLAGS += -DTHREAD_STACKSIZE_MAIN=4*1024
else
  CFLAGS += -DTHREAD_STACKSIZE_MAIN=2*1024
endif
