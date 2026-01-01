/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_sniper_mmv

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/event_manager.h>
#include <zmk/keymap.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct behavior_sniper_mmv_config {
    int slow_layer;
    int fast_layer;
    int delay_ms;
};

struct behavior_sniper_mmv_data {
    struct k_timer timer;
    uint16_t axis; // 변경됨: 표준 규격대로 축 저장
    int val;       // 변경됨: 표준 규격대로 값 저장
    bool active;
};

static void send_mouse_report(const struct device *dev) {
    const struct behavior_sniper_mmv_config *cfg = dev->config;
    struct behavior_sniper_mmv_data *data = dev->data;
    
    // 1. 레이어 확인 (스나이퍼 로직)
    int scale = 100;
    if (zmk_keymap_layer_active(cfg->slow_layer)) scale = 25; // 25%
    else if (zmk_keymap_layer_active(cfg->fast_layer)) scale = 200; // 200%

    // 2. 값 계산
    int final_val = (data->val * scale) / 100;

    // 0이 되지 않게 보정
    if (data->val != 0 && final_val == 0) final_val = (data->val > 0) ? 1 : -1;

    // 3. 전송 (축 정보 그대로 사용)
    zmk_hid_mouse_movement_set(data->axis, (int16_t)final_val);
    zmk_endpoints_send_mouse_report();
}

static void timer_handler(struct k_timer *timer) {
    struct behavior_sniper_mmv_data *data = CONTAINER_OF(timer, struct behavior_sniper_mmv_data, timer);
    const struct device *dev = device_from_handle(timer->user_data);
    send_mouse_report(dev);
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_sniper_mmv_data *data = dev->data;
    const struct behavior_sniper_mmv_config *cfg = dev->config;

    // 🔥 표준 규격 수용 (MOVE_UP 등은 값 2개를 줍니다)
    data->axis = binding->param1; // 첫 번째 값: 축 (X=0, Y=1)
    data->val  = binding->param2; // 두 번째 값: 이동량 (예: 1500)

    if (!data->active) {
        data->active = true;
        k_timer_user_data_set(&data->timer, (void *)dev->handle);
        k_timer_start(&data->timer, K_NO_WAIT, K_MSEC(cfg->delay_ms));
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_sniper_mmv_data *data = dev->data;

    // 멈춤
    data->active = false;
    k_timer_stop(&data->timer);
    
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_sniper_mmv_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
};

#define SNIPER_MMV_INST(n)                                                          \
    static struct behavior_sniper_mmv_data behavior_sniper_mmv_data_##n;            \
    static const struct behavior_sniper_mmv_config behavior_sniper_mmv_config_##n = { \
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
