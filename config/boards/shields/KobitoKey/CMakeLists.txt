# These sources run on the central side only, where the keymap layer
# state and keycode events live. The peripheral side never sees them.
if(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  zephyr_library_sources(src/idle_layer_return.c)
  zephyr_library_sources(src/behavior_arcane.c)
  zephyr_library_sources(src/jis_translate.c)
endif()
