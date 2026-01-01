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

// 설정값 구조체
struct behavior_sniper_mmv_config {
    int slow_layer;   // 느린 레이어 번호
    int fast_layer;   // 빠른 레이어 번호
    int base_speed;   // 기본 속도
    int delay_ms;     // 반응 속도 (낮을수록 부드러움)
};

// 데이터 구조체
struct behavior_sniper_mmv_data {
    struct k_timer timer;
    uint16_t active_axis; // 현재 눌린 축 (X 또는 Y)
    int direction;        // 방향 (1 또는 -1)
    bool active;
};

// 마우스 신호 보내는 함수
static void send_mouse_report(const struct device *dev) {
    const struct behavior_sniper_mmv_config *cfg = dev->config;
    struct behavior_sniper_mmv_data *data = dev->data;
    
    // 1. 현재 레이어 확인 및 속도 배율 결정
    int speed = cfg->base_speed;

    if (zmk_keymap_layer_active(cfg->slow_layer)) {
        speed /= 4; // 느린 레이어면 속도 1/4 토막
        if (speed < 1) speed = 1; // 최소 속도 보장
    } 
    else if (zmk_keymap_layer_active(cfg->fast_layer)) {
        speed *= 2; // 빠른 레이어면 속도 2배 뻥튀기
    }

    // 2. 이동 값 계산
    int val = speed * data->direction;

    // 3. 축에 맞춰 신호 전송
    // active_axis가 0이면 X축, 1이면 Y축 (INPUT_REL_X / INPUT_REL_Y)
    if (data->active_axis == 0) { // X축
        zmk_hid_mouse_movement_set(0, (int16_t)val);
        zmk_hid_mouse_movement_set(1, 0);
    } else { // Y축
        zmk_hid_mouse_movement_set(0, 0);
        zmk_hid_mouse_movement_set(1, (int16_t)val);
    }
    
    zmk_endpoints_send_mouse_report();
}

// 타이머 핸들러 (계속 호출됨)
static void timer_handler(struct k_timer *timer) {
    struct behavior_sniper_mmv_data *data = CONTAINER_OF(timer, struct behavior_sniper_mmv_data, timer);
    const struct device *dev = device_from_handle(timer->user_data);
    send_mouse_report(dev);
}

// 키를 눌렀을 때
static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_sniper_mmv_data *data = dev->data;
    const struct behavior_sniper_mmv_config *cfg = dev->config;

    // param1: 축 (MOVE_UP 등은 자동으로 축 정보를 줌)
    // 그러나 매크로에 따라 값이 다를 수 있으므로 단순화:
    // 보통 MOVE_UP은 Y축 음수, MOVE_DOWN은 Y축 양수 등을 의미함.
    // 하지만 여기서는 간단하게 param1(축)만 받아서 처리.
    
    // ZMK 표준 매크로(MOVE_UP 등)는 param1에 축 코드를 보냄 (X=0, Y=1)
    // 방향은 어떻게 알까? -> ZMK 표준은 보통 behavior 설정에서 다루지만,
    // 여기서는 형님이 편하게 쓰도록 "MOVE_UP"을 흉내내야 함.
    
    // 편의상: 
    // binding->param1 이 0(X) 또는 1(Y) 이라고 가정.
    // 문제는 방향인데, 형님이 키맵에서 &sniper_mmv MOVE_UP 처럼 쓰려면
    // MOVE_UP 매크로가 값을 2개 보내는 걸 받아야 함. 
    // 하지만 우리는 #binding-cells = <1> 로 할 것임.
    
    // 💡 해결책: 
    // 키맵 에디터 호환을 위해 #binding-cells=<1> 유지.
    // 대신 키맵에서 직접 값을 넣는 게 아니라, 
    // MOVE_UP / MOVE_DOWN / MOVE_LEFT / MOVE_RIGHT 각각을 위한 4개의 행동을 만드는 게 낫지만
    // 형님 요청대로 "복사" 느낌을 내려면 파라미터를 받아야 함.

    // 일단 기본 ZMK 값 수신:
    data->active_axis = binding->param1; // 0=X, 1=Y, 2=Wheel...
    
    // 방향 판별이 어려우니, 그냥 사용자가 키맵에서
    // &sniper_mmv MOVE_DOWN (양수)
    // &sniper_mmv MOVE_UP (음수...가 안됨 1개만 받으면)
    
    // 🔥 형님을 위한 특단 조치:
    // param1 값에 따라 방향을 추측하는 건 위험함.
    // 가장 확실한 건 키맵에서 `&sniper_mmv UP` 처럼 쓰는 것인데
    // 그냥 값을 통으로 받겠습니다.
    
    // param1이 짝수(0)면 X, 홀수(1)면 Y라고 가정하고 
    // binding 자체에 방향성을 넣을 순 없음.
    
    // 따라서, 코드를 수정하여 **방향키 4개용 비헤이비어를 따로 안 만들고**,
    // 그냥 **키맵에서 파라미터 1개로 방향까지 제어**하게 만듭니다.
    // 예: 0 = 상, 1 = 하, 2 = 좌, 3 = 우
    
    if (binding->param1 == 0) { data->active_axis = 1; data->direction = -1; } // 상 (Y -)
    else if (binding->param1 == 1) { data->active_axis = 1; data->direction = 1; } // 하 (Y +)
    else if (binding->param1 == 2) { data->active_axis = 0; data->direction = -1; } // 좌 (X -)
    else if (binding->param1 == 3) { data->active_axis = 0; data->direction = 1; } // 우 (X +)

    if (!data->active) {
        data->active = true;
        k_timer_user_data_set(&data->timer, (void *)dev->handle);
        k_timer_start(&data->timer, K_NO_WAIT, K_MSEC(cfg->delay_ms));
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

// 키를 뗐을 때
static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_sniper_mmv_data *data = dev->data;

    // 타이머 중지
    data->active = false;
    k_timer_stop(&data->timer);
    
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_sniper_mmv_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
};

// 인스턴스 생성 매크로
#define SNIPER_MMV_INST(n)                                                          \
    static struct behavior_sniper_mmv_data behavior_sniper_mmv_data_##n;            \
    static const struct behavior_sniper_mmv_config behavior_sniper_mmv_config_##n = { \
        .slow_layer = DT_INST_PROP(n, slow_layer),                                  \
        .fast_layer = DT_INST_PROP(n, fast_layer),                                  \
        .base_speed = DT_INST_PROP(n, base_speed),                                  \
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
