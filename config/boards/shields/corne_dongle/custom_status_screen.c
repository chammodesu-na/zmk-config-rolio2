/* boards/shields/corne_dongle/custom_status_screen.c */

#include <zmk/display/widgets/output_status.h>
#include <zmk/display/widgets/battery_status.h>
#include <zmk/display/widgets/layer_status.h>
#include <zmk/display/status_screen.h>
#include <zmk/split/bluetooth/central.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static lv_obj_t *battery_label;

/* 배터리 정보 갱신 함수 */
static void battery_status_update_cb(struct k_work *work) {
    uint8_t left = 0;
    uint8_t right = 0;
    
    // 왼쪽(0), 오른쪽(1) 배터리 가져오기
    struct zmk_peripheral_battery_level_event *left_ev = zmk_split_bt_get_peripheral_battery_level(0);
    struct zmk_peripheral_battery_level_event *right_ev = zmk_split_bt_get_peripheral_battery_level(1);

    if (left_ev != NULL) { left = left_ev->level; }
    if (right_ev != NULL) { right = right_ev->level; }

    // 화면에 L/R 배터리 크게 표시
    if (battery_label != NULL) {
        lv_label_set_text_fmt(battery_label, "L: %d%%  R: %d%%", left, right);
    }
}

K_WORK_DEFINE(battery_status_update_work, battery_status_update_cb);

void battery_status_event_listener(const zmk_event_t *eh) {
    k_work_submit(&battery_status_update_work);
}

// 배터리 변경 감지
ZMK_LISTENER(widget_battery_status, battery_status_event_listener);
ZMK_SUBSCRIPTION(widget_battery_status, zmk_peripheral_battery_level_changed);

/* 화면 초기화 */
lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);

    // 1. 맨 위 제목 (USB 연결 표시 대신 간단히 텍스트)
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Corne Dongle");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    // 2. 배터리 정보 (중앙에 크게)
    battery_label = lv_label_create(screen);
    lv_label_set_text(battery_label, "WAITING...");
    lv_obj_align(battery_label, LV_ALIGN_CENTER, 0, 5);

    // 초기 업데이트 실행
    k_work_submit(&battery_status_update_work);

    return screen;
}
