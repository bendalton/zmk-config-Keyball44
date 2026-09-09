# Custom nice!view dashboard for the Keyball44. One source: content is chosen by
# split role, geometry by side. Requires CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM=y
# and CONFIG_NICE_VIEW_WIDGET_STATUS=n (set in the two half shields' confs).
if(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM)
  zephyr_library()
  zephyr_library_sources(src/kb_display.c)
endif()

# The pinned PMW3610 driver calls central-only ZMK functions unconditionally, so
# it will not link on a peripheral. In the dongle setup the sensor's half IS a
# peripheral -- supply weak stubs. See src/pmw3610_peripheral_stubs.c.
if(CONFIG_PMW3610 AND CONFIG_ZMK_SPLIT AND NOT CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  zephyr_library_named(keyball_pmw3610_shim)
  zephyr_library_sources(src/pmw3610_peripheral_stubs.c)
endif()
