/*
 * Copyright (c) 2026 kiyo
 *
 * SPDX-License-Identifier: MIT
 *
 * Arcane key: a hybrid of the Repeat Key and the (alternate repeat) Magic
 * Key, modelled on ZMK's built-in key repeat behavior. When pressed, the
 * previously pressed key is looked up in a per-instance table: if the table
 * has an entry, that key is sent instead; otherwise the previous key is
 * repeated as-is.
 *
 * The tables port the LMAGIC / RMAGIC definitions from kiyo's Keyball44
 * QMK keymap (keyball44/keymaps/aqua/aqua.c), where entries that return the
 * same letter act as plain repeats. Only the entries whose output differs
 * from the input are listed here; everything else falls back to repeat.
 * Table 0 = left arcane key (LMAGIC), table 1 = right arcane key (RMAGIC).
 */

#define DT_DRV_COMPAT zmk_behavior_arcane

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <zmk/hid.h>

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/* HID keyboard-page usage IDs: A = 0x04 ... Z = 0x1D. Index 0 = no entry. */
#define ARCANE_NO_ENTRY 0

/* Left arcane (LMAGIC): A->O, E->Q, F->Y, I->U, J->Y, N->Y, O->A, Q->E,
 * U->I, W->N, Y->N. */
static const uint8_t arcane_table_left[0x1E] = {
    [0x04] = 0x12, /* A -> O */
    [0x08] = 0x14, /* E -> Q */
    [0x09] = 0x1C, /* F -> Y */
    [0x0C] = 0x18, /* I -> U */
    [0x0D] = 0x1C, /* J -> Y */
    [0x11] = 0x1C, /* N -> Y */
    [0x12] = 0x04, /* O -> A */
    [0x14] = 0x08, /* Q -> E */
    [0x18] = 0x0C, /* U -> I */
    [0x1A] = 0x11, /* W -> N */
    [0x1C] = 0x11, /* Y -> N */
};

/* Right arcane (RMAGIC): B->M, C->K, D->R, G->Z, H->L, K->C, L->D, M->B,
 * P->X, R->D, S->K, T->B, V->B, X->P, Z->V. */
static const uint8_t arcane_table_right[0x1E] = {
    [0x05] = 0x10, /* B -> M */
    [0x06] = 0x0E, /* C -> K */
    [0x07] = 0x15, /* D -> R */
    [0x0A] = 0x1D, /* G -> Z */
    [0x0B] = 0x0F, /* H -> L */
    [0x0E] = 0x06, /* K -> C */
    [0x0F] = 0x07, /* L -> D */
    [0x10] = 0x05, /* M -> B */
    [0x13] = 0x1B, /* P -> X */
    [0x15] = 0x07, /* R -> D */
    [0x16] = 0x0E, /* S -> K */
    [0x17] = 0x05, /* T -> B */
    [0x19] = 0x05, /* V -> B */
    [0x1B] = 0x13, /* X -> P */
    [0x1D] = 0x19, /* Z -> V */
};

struct behavior_arcane_config {
    uint8_t table;
    uint8_t usage_pages_count;
    uint16_t usage_pages[];
};

struct behavior_arcane_data {
    struct zmk_keycode_state_changed last_keycode_pressed;
    struct zmk_keycode_state_changed current_keycode_pressed;
};

static int on_arcane_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_arcane_config *config = dev->config;
    struct behavior_arcane_data *data = dev->data;

    if (data->last_keycode_pressed.usage_page == 0) {
        return ZMK_BEHAVIOR_OPAQUE;
    }

    memcpy(&data->current_keycode_pressed, &data->last_keycode_pressed,
           sizeof(struct zmk_keycode_state_changed));

    /* Magic lookup: keyboard page letters A (0x04) .. Z (0x1D) only.
     * A table hit sends the mapped key with modifiers cleared, matching
     * the QMK implementation's plain send_string output. A miss repeats
     * the previous key exactly, modifiers included. */
    if (data->current_keycode_pressed.usage_page == 0x07 &&
        data->current_keycode_pressed.keycode >= 0x04 &&
        data->current_keycode_pressed.keycode <= 0x1D) {
        const uint8_t *table =
            (config->table == 0) ? arcane_table_left : arcane_table_right;
        uint8_t mapped = table[data->current_keycode_pressed.keycode];
        if (mapped != ARCANE_NO_ENTRY) {
            data->current_keycode_pressed.keycode = mapped;
            data->current_keycode_pressed.implicit_modifiers = 0;
            data->current_keycode_pressed.explicit_modifiers = 0;
        }
    }

    data->current_keycode_pressed.timestamp = k_uptime_get();

    raise_zmk_keycode_state_changed(data->current_keycode_pressed);

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_arcane_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_arcane_data *data = dev->data;

    if (data->current_keycode_pressed.usage_page == 0) {
        return ZMK_BEHAVIOR_OPAQUE;
    }

    data->current_keycode_pressed.timestamp = k_uptime_get();
    data->current_keycode_pressed.state = false;

    raise_zmk_keycode_state_changed(data->current_keycode_pressed);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_arcane_driver_api = {
    .binding_pressed = on_arcane_binding_pressed,
    .binding_released = on_arcane_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

static int arcane_keycode_state_changed_listener(const zmk_event_t *eh);

ZMK_LISTENER(behavior_arcane, arcane_keycode_state_changed_listener);
ZMK_SUBSCRIPTION(behavior_arcane, zmk_keycode_state_changed);

#define GET_DEV(inst) DEVICE_DT_INST_GET(inst),
static const struct device *devs[] = {DT_INST_FOREACH_STATUS_OKAY(GET_DEV)};

static int arcane_keycode_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    for (int i = 0; i < ARRAY_SIZE(devs); i++) {
        const struct device *dev = devs[i];

        struct behavior_arcane_data *data = dev->data;
        const struct behavior_arcane_config *config = dev->config;

        for (int u = 0; u < config->usage_pages_count; u++) {
            if (config->usage_pages[u] == ev->usage_page) {
                memcpy(&data->last_keycode_pressed, ev, sizeof(struct zmk_keycode_state_changed));
                data->last_keycode_pressed.implicit_modifiers |= zmk_hid_get_explicit_mods();
                break;
            }
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

#define ARCANE_INST(n)                                                                             \
    static struct behavior_arcane_data behavior_arcane_data_##n = {};                              \
    static struct behavior_arcane_config behavior_arcane_config_##n = {                            \
        .table = DT_INST_PROP(n, table),                                                           \
        .usage_pages = DT_INST_PROP(n, usage_pages),                                               \
        .usage_pages_count = DT_INST_PROP_LEN(n, usage_pages),                                     \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, &behavior_arcane_data_##n, &behavior_arcane_config_##n, \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                      \
                            &behavior_arcane_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ARCANE_INST)

#endif
