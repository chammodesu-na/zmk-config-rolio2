/* behavior_sniper_mmv.c - 표준 규격(2-axis) 호환 버전 */

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
    int x_mv;
    int y_mv;
    bool active;
};

static void send_mouse_report(const struct device *dev) {
    const struct behavior_sniper_mmv_config *cfg = dev->config;
    struct behavior_sniper_mmv_data *data = dev->data;
    
    // 1. 레이어 감지 및 속도 조절
    int scale_percent = 100;

    if (zmk_keymap_layer_active(cfg->slow_layer)) {
        scale_percent = 25; // 25% (1/4 속도)
    } else if (zmk_keymap_layer_active(cfg->fast_layer)) {
        scale_percent = 200; // 200% (2배 속도)
    }

    // 2. 값 계산 (입력값 * 배율 / 100)
    int x = (data->x_mv * scale_percent) / 100;
    int y = (data->y_mv * scale_percent) / 100;

    // 너무 느려서 0이 되는 것 방지 (최소 1 유지)
    if (data->x_mv != 0 && x == 0) x = (data->x_mv > 0) ? 1 : -1;
    if (data->y_mv != 0 && y == 0) y = (data->y_mv > 0) ? 1 : -1;

    zmk_hid_mouse_movement_set(0, (int16_t)x);
    zmk_hid_mouse_movement_set(1, (int16_t)y);
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

    // 🔥 핵심: 표준 MOVE_UP 매크로는 값 2개를 보냅니다.
    // param1: 축 (0=X, 1=Y)
    // param2: 값 (이동량)
    
    if (binding->param1 == 0) { // X축
        data->x_mv = binding->param2;
    } else if (binding->param1 == 1) { // Y축
        data->y_mv = binding->param2;
    }

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

    // 손 떼면 해당 축 멈춤
    if (binding->param1 == 0) data->x_mv = 0;
    else if (binding->param1 == 1) data->y_mv = 0;

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
