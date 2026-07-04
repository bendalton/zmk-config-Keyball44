# Custom nice!view dual dashboard for the Keyball44. One source, role-branched
# internally (#if CONFIG_ZMK_SPLIT_ROLE_CENTRAL): central=primary, peripheral=
# companion. Requires CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM=y and
# CONFIG_NICE_VIEW_WIDGET_STATUS=n (see config/keyball44.conf).
if(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM)
  zephyr_library()
  zephyr_library_sources(src/kb_display.c)
endif()
