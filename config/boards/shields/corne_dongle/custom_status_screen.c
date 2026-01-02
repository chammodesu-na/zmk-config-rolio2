/*
 * boards/shields/corne_dongle/custom_status_screen.c
 */

#include <zmk/display/widgets/output_status.h>
#include <zmk/display/widgets/battery_status.h>
#include <zmk/display/widgets/layer_status.h>
#include <zmk/display/status_screen.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_selection_changed.h>
#include <zmk/split/bluetooth/central.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>

#include <zephyr/logging/log.h>
#include <lvgl.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* UI 요소들 */
static lv_obj_t *output_label;
static lv_obj_t *layer_label;
static lv_obj_t *battery_label;

/* -------------------------------------------------------------------------
 * 1. 출력 상태 업데이트 (USB / BT 1 / BT 2 ...)
 * ------------------------------------------------------------------------- */
static void output_status_update_cb(struct k_work *work) {
    char text[10] = "";
    
    // 현재 선택된 엔드포인트 확인
    enum zmk_endpoint selected_endpoint = zmk_endpoints_selected();

    if (selected_endpoint == ZMK_ENDPOINT_USB) {
        snprintf(text, sizeof(text), "USB");
    } else {
        // 블루투스 프로필 인덱스 가져오기 (0부터 시작하므로 +1)
        uint8_t profile = zmk_ble_active_profile_index() + 1;
        snprintf(text, sizeof(text), "BT %d", profile);
    }

    if (output_label != NULL) {
        lv_label_set_text(output_label, text);
    }
}

K_WORK_DEFINE(output_status_update_work, output_status_update_cb);

void output_status_listener(const zmk_event_t *eh) {
    k_work_submit(&output_status_update_work);
}

// 연결 변경 시 실행
ZMK_LISTENER(widget_output_status, output_status_listener);
ZMK_SUBSCRIPTION(widget_output_status, zmk_endpoint_selection_changed);
ZMK_SUBSCRIPTION(widget_output_status, zmk_ble_active_profile_changed);


/* -------------------------------------------------------------------------
 * 2. 레이어 상태 업데이트 (숫자로 표시)
 * ------------------------------------------------------------------------- */
static void layer_status_update_cb(struct k_work *work) {
    uint8_t layer_index = 0;
    
    // 현재 활성화된 레이어 상태 가져오기 (비트마스크)
    zmk_layer_state_t state = zmk_layer_state_get();

    // 활성화된 가장 높은 레이어 찾기
    for (int i = 31; i >= 0; i--) {
        if (state & (1U << i)) {
            layer_index = i;
            break;
        }
    }

    if (layer_label != NULL) {
        lv_label_set_text_fmt(layer_label, "Lay: %d", layer_index);
    }
}

K_WORK_DEFINE(layer_status_update_work, layer_status_update_cb);

void layer_status_listener(const zmk_event_t *eh) {
    k_work_submit(&layer_status_update_work);
}

// 레이어 변경 시 실행
ZMK_LISTENER(widget_layer_status, layer_status_listener);
ZMK_SUBSCRIPTION(widget_layer_status, zmk_layer_state_changed);


/* -------------------------------------------------------------------------
 * 3. 배터리 상태 업데이트 (좌/우 분리)
 * ------------------------------------------------------------------------- */
static void battery_status_update_cb(struct k_work *work) {
    uint8_t left = 0;
    uint8_t right = 0;
    
    // 0번: 왼쪽, 1번: 오른쪽 (Peripheral 인덱스)
    struct zmk_peripheral_battery_level_event *left_ev = zmk_split_bt_get_peripheral_battery_level(0);
    struct zmk_peripheral_battery_level_event *right_ev = zmk_split_bt_get_peripheral_battery_level(1);

    if (left_ev != NULL) { left = left_ev->level; }
    if (right_ev != NULL) { right = right_ev->level; }

    if (battery_label != NULL) {
        // 배터리가 0%로 뜨면 아직 연결 안 된 것
        lv_label_set_text_fmt(battery_label, "L:%d%%  R:%d%%", left, right);
    }
}

K_WORK_DEFINE(battery_status_update_work, battery_status_update_cb);

void battery_status_listener(const zmk_event_t *eh) {
    k_work_submit(&battery_status_update_work);
}

// 주변기기 배터리 변경 시 실행
ZMK_LISTENER(widget_battery_status, battery_status_listener);
ZMK_SUBSCRIPTION(widget_battery_status, zmk_peripheral_battery_level_changed);


/* -------------------------------------------------------------------------
 * 메인 화면 초기화 (레이아웃 잡기)
 * ------------------------------------------------------------------------- */
lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);

    /* 1. 왼쪽 위: 연결 상태 (USB / BT) */
    output_label = lv_label_create(screen);
    lv_label_set_text(output_label, "USB");
    lv_obj_align(output_label, LV_ALIGN_TOP_LEFT, 0, 0);

    /* 2. 오른쪽 위: 레이어 (숫자) */
    layer_label = lv_label_create(screen);
    lv_label_set_text(layer_label, "Lay: 0");
    lv_obj_align(layer_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    /* 3. 아래 중앙: 좌우 배터리 */
    battery_label = lv_label_create(screen);
    lv_label_set_text(battery_label, "L: --%  R: --%");
    lv_obj_align(battery_label, LV_ALIGN_BOTTOM_MID, 0, -2); // 바닥에서 살짝 위로

    /* 초기 상태 한번 업데이트 */
    k_work_submit(&output_status_update_work);
    k_work_submit(&layer_status_update_work);
    k_work_submit(&battery_status_update_work);

    return screen;
}
