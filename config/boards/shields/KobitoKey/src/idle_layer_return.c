/*
 * Copyright (c) 2026 kiyo
 *
 * SPDX-License-Identifier: MIT
 *
 * Idle layer return: while the typing layer (ALPHA) is the default layer,
 * every key press restarts a timer. When the timer expires, the default
 * layer is switched back to the home layer (NAV). Trackball activity does
 * not restart the timer; only key presses (and layer changes) do.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

#define WATCH_LAYER CONFIG_KOBITOKEY_IDLE_RETURN_WATCH_LAYER
#define HOME_LAYER CONFIG_KOBITOKEY_IDLE_RETURN_HOME_LAYER
#define TIMEOUT_MS CONFIG_KOBITOKEY_IDLE_RETURN_TIMEOUT_MS

static void idle_return_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (zmk_keymap_layer_default() == WATCH_LAYER) {
        LOG_INF("idle %d ms in layer %d, returning to layer %d", TIMEOUT_MS, WATCH_LAYER,
                HOME_LAYER);
        zmk_keymap_layer_to(HOME_LAYER, false); /* ZMK main: locking arg */
    }
}

K_WORK_DELAYABLE_DEFINE(idle_return_work, idle_return_work_handler);

static void arm_if_watching(void) {
    if (zmk_keymap_layer_default() == WATCH_LAYER) {
        k_work_reschedule(&idle_return_work, K_MSEC(TIMEOUT_MS));
    } else {
        k_work_cancel_delayable(&idle_return_work);
    }
}

static int idle_layer_return_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *key_ev = as_zmk_keycode_state_changed(eh);

    if (key_ev != NULL) {
        if (key_ev->state) {
            arm_if_watching();
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (as_zmk_layer_state_changed(eh) != NULL) {
        arm_if_watching();
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(idle_layer_return, idle_layer_return_listener);
ZMK_SUBSCRIPTION(idle_layer_return, zmk_keycode_state_changed);
ZMK_SUBSCRIPTION(idle_layer_return, zmk_layer_state_changed);
