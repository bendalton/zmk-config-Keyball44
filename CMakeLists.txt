# Custom nice!view status screen for the Keyball44.
#  - central half (RIGHT): solid black, drawn once, never redraws
#  - peripheral half (LEFT): the portrait dashboard
# Only one zmk_display_status_screen() compiles per build (per role), so no
# duplicate symbol. Requires CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM=y and
# CONFIG_NICE_VIEW_WIDGET_STATUS=n (see config/keyball44.conf).
if(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM)
  zephyr_library()
  if(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    zephyr_library_sources(src/black_screen.c)
  else()
    zephyr_library_sources(src/dashboard.c)
  endif()
endif()
