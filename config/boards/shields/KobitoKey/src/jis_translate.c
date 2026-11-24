/*
 * Copyright (c) 2026 kiyo
 *
 * SPDX-License-Identifier: MIT
 *
 * JIS/US output toggle + ANSI->JIS keycode translation, ported from
 * kiyo's Keyball44 QMK keymap (features/translate_ansi_to_jis.c).
 *
 * The keymap is written with US (ANSI) keycodes. When JIS mode is on,
 * symbol keycode events are rewritten to the keycode + shift combination
 * that produces the same character on a host configured for a Japanese
 * (JIS) layout. The mode is persisted with the Zephyr settings subsystem
 * (NVS flash), the ZMK equivalent of QMK's EEPROM user config.
 *
 * Toggle behavior parameter: 0 = toggle, 1 = JIS on, 2 = US on.
 * The toggle key sits on the FUNC layer, home row, leftmost position.
 */

#define DT_DRV_COMPAT zmk_behavior_jis_toggle

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zmk/behavior.h>

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* USB HID modifier bits, as carried in keycode state changed events. */
#define JIS_MOD_LSHIFT 0x02
#define JIS_MOD_RSHIFT 0x20
#define JIS_MOD_SHIFT_MASK (JIS_MOD_LSHIFT | JIS_MOD_RSHIFT)

/* ---- Mode flag + persistence (Zephyr settings, like QMK's EEPROM) ---- */

static bool jis_mode = false;

#if IS_ENABLED(CONFIG_SETTINGS)

static int jis_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    const char *next;

    if (settings_name_steq(name, "mode", &next) && !next) {
        if (len != sizeof(jis_mode)) {
            return -EINVAL;
        }
        return read_cb(cb_arg, &jis_mode, sizeof(jis_mode));
    }

    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(jis_translate, "kobitokey", NULL, jis_settings_set, NULL, NULL);

static void jis_mode_save(void) {
    settings_save_one("kobitokey/mode", &jis_mode, sizeof(jis_mode));
}

#else

static void jis_mode_save(void) {}

#endif /* IS_ENABLED(CONFIG_SETTINGS) */

static void jis_mode_set(bool mode) {
    if (jis_mode != mode) {
        jis_mode = mode;
        jis_mode_save();
        LOG_INF("JIS mode %s", mode ? "on" : "off");
    }
}

/* ---- Translation table ---- */

/* (source keycode, source shift) -> (target keycode, target shift).
 * Keycodes are HID keyboard-page usage IDs. Ported 1:1 from the QMK
 * translate_map in translate_ansi_to_jis.c. */
struct jis_map_entry {
    uint32_t src;
    bool src_shift;
    uint32_t dst;
    bool dst_shift;
};

static const struct jis_map_entry jis_map[] = {
    {0x2E, false, 0x2D, true},  /* =  -> Shift+- */
    {0x2F, false, 0x30, false}, /* [  -> ] position key */
    {0x31, false, 0x89, false}, /* \  -> INT3 (yen key) */
    {0x30, false, 0x32, false}, /* ]  -> NUHS */
    {0x34, false, 0x24, true},  /* '  -> Shift+7 */
    {0x35, false, 0x2F, true},  /* `  -> Shift+[ */
    {0x27, true, 0x26, true},   /* )  -> Shift+9 */
    {0x1F, true, 0x2F, false},  /* @  -> [ position key */
    {0x23, true, 0x2E, false},  /* ^  -> = position key */
    {0x24, true, 0x23, true},   /* &  -> Shift+6 */
    {0x25, true, 0x34, true},   /* *  -> Shift+' */
    {0x26, true, 0x25, true},   /* (  -> Shift+8 */
    {0x2E, true, 0x33, true},   /* +  -> Shift+; */
    {0x2D, true, 0x87, true},   /* _  -> Shift+INT1 (ro key) */
    {0x2F, true, 0x30, true},   /* {  -> Shift+] */
    {0x31, true, 0x89, true},   /* |  -> Shift+INT3 */
    {0x30, true, 0x32, false},  /* }  -> NUHS */
    {0x33, true, 0x34, false},  /* :  -> ' position key */
    {0x34, true, 0x1F, true},   /* "  -> Shift+2 */
    {0x35, true, 0x2E, true},   /* ~  -> Shift+= */
};

static const struct jis_map_entry *jis_find(uint32_t keycode, bool shift) {
    for (int i = 0; i < ARRAY_SIZE(jis_map); i++) {
        if (jis_map[i].src == keycode && jis_map[i].src_shift == shift) {
            return &jis_map[i];
        }
    }
    return NULL;
}

/* ---- Translation listener ---- */

#define JIS_MAX_ACTIVE 4

struct jis_active {
    bool used;
    uint32_t src_keycode;
    struct zmk_keycode_state_changed out;
};

static struct jis_active jis_actives[JIS_MAX_ACTIVE];
static bool jis_raising; /* Guard: ignore events raised by this listener. */

static int jis_translate_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);

    if (ev == NULL || jis_raising || ev->usage_page != 0x07) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->state) {
        bool shift =
            ((ev->implicit_modifiers | ev->explicit_modifiers) & JIS_MOD_SHIFT_MASK) != 0;
        const struct jis_map_entry *entry;

        if (!jis_mode) {
            return ZMK_EV_EVENT_BUBBLE;
        }

        entry = jis_find(ev->keycode, shift);
        if (entry == NULL) {
            return ZMK_EV_EVENT_BUBBLE;
        }

        struct jis_active *slot = NULL;
        for (int i = 0; i < JIS_MAX_ACTIVE; i++) {
            if (!jis_actives[i].used) {
                slot = &jis_actives[i];
                break;
            }
        }
        if (slot == NULL) {
            return ZMK_EV_EVENT_BUBBLE;
        }

        slot->used = true;
        slot->src_keycode = ev->keycode;
        slot->out = *ev;
        slot->out.keycode = entry->dst;
        slot->out.implicit_modifiers = entry->dst_shift ? JIS_MOD_LSHIFT : 0;
        slot->out.explicit_modifiers = 0;

        jis_raising = true;
        raise_zmk_keycode_state_changed(slot->out);
        jis_raising = false;

        return ZMK_EV_EVENT_HANDLED;
    }

    /* Release: replay the release of whatever the press was translated
     * to. Checked even when JIS mode was toggled off mid-hold, so a
     * translated key is never left stuck on the host. */
    for (int i = 0; i < JIS_MAX_ACTIVE; i++) {
        struct jis_active *slot = &jis_actives[i];
        if (slot->used && slot->src_keycode == ev->keycode) {
            struct zmk_keycode_state_changed out = slot->out;
            out.state = false;
            out.timestamp = ev->timestamp;
            slot->used = false;

            jis_raising = true;
            raise_zmk_keycode_state_changed(out);
            jis_raising = false;

            return ZMK_EV_EVENT_HANDLED;
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(jis_translate, jis_translate_listener);
ZMK_SUBSCRIPTION(jis_translate, zmk_keycode_state_changed);

/* ---- Toggle behavior ---- */

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_jis_toggle_binding_pressed(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);

    switch (binding->param1) {
    case 1:
        jis_mode_set(true);
        break;
    case 2:
        jis_mode_set(false);
        break;
    default:
        jis_mode_set(!jis_mode);
        break;
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_jis_toggle_driver_api = {
    .binding_pressed = on_jis_toggle_binding_pressed,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif /* IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA) */
};

#define JIS_TOG_INST(n)                                                                            \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_jis_toggle_driver_api);

DT_INST_FOREACH_STATUS_OKAY(JIS_TOG_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
