/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_sniper_mmv

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h> // 레이어 상태 확인용

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct behavior_sniper_mmv_config {
    int x_code;
    int y_code;
    int slow_layer;
    int fast_layer;
    int delay_ms;
};

struct behavior_sniper_mmv_data {
    struct k_timer timer;
    int x_mv;
    int y_mv;
    bool active;
};

static void send_mouse_report(const struct device *dev) {
    const struct behavior_sniper_mmv_config *cfg = dev->config;
    struct behavior_sniper_mmv_data *data = dev->data;
    
    // 🔥 핵심 로직: 현재 레이어를 실시간으로 확인하여 배율 조정
    int scale = 100; // 기본 100%

    if (zmk_keymap_layer_active(cfg->slow_layer)) {
        scale = 25; // 5번(느린) 레이어 활성화 시 25% 속도 (1/4)
    } else if (zmk_keymap_layer_active(cfg->fast_layer)) {
        scale = 200; // 6번(빠른) 레이어 활성화 시 200% 속도 (2배)
    }

    // 이동값 계산
    int x = (data->x_mv * scale) / 100;
    int y = (data->y_mv * scale) / 100;

    // 0이 되어버리면 움직이지 않으므로 최소 1은 유지 (방향이 있다면)
    if (data->x_mv != 0 && x == 0) x = (data->x_mv > 0) ? 1 : -1;
    if (data->y_mv != 0 && y == 0) y = (data->y_mv > 0) ? 1 : -1;

    zmk_hid_mouse_movement_set(cfg->x_code, (int16_t)x);
    zmk_hid_mouse_movement_set(cfg->y_code, (int16_t)y);
    zmk_endpoints_send_mouse_report();
}

static void timer_handler(struct k_timer *timer) {
    struct behavior_sniper_mmv_data *data = CONTAINER_OF(timer, struct behavior_sniper_mmv_data, timer);
    const struct device *dev = device_from_handle(timer->user_data); // 타이머에서 디바이스 포인터 복구

    send_mouse_report(dev);
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_sniper_mmv_data *data = dev->data;
    const struct behavior_sniper_mmv_config *cfg = dev->config;

    if (binding->param1 == cfg->x_code) {
        data->x_mv = 10; // 기본 이동 단위 (원하는 만큼 수정 가능)
    } else if (binding->param1 == cfg->y_code) {
        data->y_mv = 10;
    }

    // 타이머 시작 (누르고 있는 동안 계속 실행)
    if (!data->active) {
        data->active = true;
        k_timer_user_data_set(&data->timer, (void *)dev->handle); // 디바이스 핸들 저장
        k_timer_start(&data->timer, K_NO_WAIT, K_MSEC(cfg->delay_ms));
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_sniper_mmv_data *data = dev->data;
    const struct behavior_sniper_mmv_config *cfg = dev->config;

    if (binding->param1 == cfg->x_code) {
        data->x_mv = 0;
    } else if (binding->param1 == cfg->y_code) {
        data->y_mv = 0;
    }

    if (data->x_mv == 0 && data->y_mv == 0) {
        data->active = false;
        k_timer_stop(&data->timer);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_sniper_mmv_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
};

#define SNIPER_MMV_INST(n)                                                          \
    static struct behavior_sniper_mmv_data behavior_sniper_mmv_data_##n;            \
    static const struct behavior_sniper_mmv_config behavior_sniper_mmv_config_##n = { \
        .x_code = DT_INST_PROP(n, x_input_code),                                    \
        .y_code = DT_INST_PROP(n, y_input_code),                                    \
        .slow_layer = DT_INST_PROP(n, slow_layer),                                  \
        .fast_layer = DT_INST_PROP(n, fast_layer),                                  \
        .delay_ms = DT_INST_PROP(n, delay_ms),                                      \
    };                                                                              \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_sniper_mmv_driver_api,                      \
                            &behavior_sniper_mmv_data_##n,                          \
                            &behavior_sniper_mmv_config_##n,                        \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,       \
                            &behavior_sniper_mmv_driver_api);                       \
    static void behavior_sniper_mmv_init_##n(void) {                                \
         k_timer_init(&behavior_sniper_mmv_data_##n.timer, timer_handler, NULL);    \
    }                                                                               \
    SYS_INIT(behavior_sniper_mmv_init_##n, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

DT_INST_FOREACH_STATUS_OKAY(SNIPER_MMV_INST)
