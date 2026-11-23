# Idle layer return runs on the central side only, where the keymap
# layer state lives. The peripheral side never sees default-layer changes.
if(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  zephyr_library_sources(src/idle_layer_return.c)
endif()
