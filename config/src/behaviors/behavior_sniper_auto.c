/* behavior_sniper_auto.c */
#define DT_DRV_COMPAT zmk_behavior_sniper_auto

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/event_manager.h>
#include <zmk/keymap.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct behavior_sniper_auto_config {
    int slow_layer;
    int fast_layer;
    int x_val;  /* 고정된 X 이동값 */
    int y_val;  /* 고정된 Y 이동값 */
    int delay_ms;
};

struct behavior_sniper_auto_data {
    struct k_timer timer;
    bool active;
};

static void send_mouse_report(const struct device *dev) {
    const struct behavior_sniper_auto_config *cfg = dev->config;
    
    // 1. 레이어 확인 & 속도 조절
    int scale = 100;
    if (zmk_keymap_layer_active(cfg->slow_layer)) scale = 25; 
    else if (zmk_keymap_layer_active(cfg->fast_layer)) scale = 200; 

    // 2. 미리 설정된 값(x_val, y_val)에 배율 적용
    int x = (cfg->x_val * scale) / 100;
    int y = (cfg->y_val * scale) / 100;

    // 0이 안 되게 최소값 보정
    if (cfg->x_val != 0 && x == 0) x = (cfg->x_val > 0) ? 1 : -1;
    if (cfg->y_val != 0 && y == 0) y = (cfg->y_val > 0) ? 1 : -1;

    zmk_hid_mouse_movement_set(0, (int16_t)x);
    zmk_hid_mouse_movement_set(1, (int16_t)y);
    zmk_endpoints_send_mouse_report();
}

static void timer_handler(struct k_timer *timer) {
    const struct device *dev = device_from_handle(timer->user_data);
    send_mouse_report(dev);
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_sniper_auto_data *data = dev->data;
    const struct behavior_sniper_auto_config *cfg = dev->config;

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
    struct behavior_sniper_auto_data *data = dev->data;

    data->active = false;
    k_timer_stop(&data->timer);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_sniper_auto_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
};

#define SNIPER_AUTO_INST(n)                                                         \
    static struct behavior_sniper_auto_data behavior_sniper_auto_data_##n;          \
    static const struct behavior_sniper_auto_config behavior_sniper_auto_config_##n = { \
        .slow_layer = DT_INST_PROP(n, slow_layer),                                  \
        .fast_layer = DT_INST_PROP(n, fast_layer),                                  \
        .x_val = DT_INST_PROP(n, x_val),                                            \
        .y_val = DT_INST_PROP(n, y_val),                                            \
        .delay_ms = DT_INST_PROP(n, delay_ms),                                      \
    };                                                                              \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_sniper_auto_driver_api,                     \
                            &behavior_sniper_auto_data_##n,                         \
                            &behavior_sniper_auto_config_##n,                       \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,       \
                            &behavior_sniper_auto_driver_api);                      \
    static void behavior_sniper_auto_init_##n(void) {                               \
         k_timer_init(&behavior_sniper_auto_data_##n.timer, timer_handler, NULL);   \
    }                                                                               \
    SYS_INIT(behavior_sniper_auto_init_##n, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

DT_INST_FOREACH_STATUS_OKAY(SNIPER_AUTO_INST)
