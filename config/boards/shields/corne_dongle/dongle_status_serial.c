#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/hid_indicators.h>
#include <zmk/keymap.h>
#include <zmk/battery.h>
#include <zmk/endpoints.h>

#if IS_ENABLED(CONFIG_ZMK_WPM)
#include <zmk/events/wpm_state_changed.h>
#endif

LOG_MODULE_REGISTER(dongle_status_serial, LOG_LEVEL_INF);

static const struct device *uart_dev;
static bool usb_ready = false;

struct status_state {
    uint8_t layer;
    uint8_t battery_central;
    uint8_t wpm;
    uint8_t caps_lock;
    uint8_t num_lock;
    uint8_t scroll_lock;
    uint8_t output_usb;
    char layer_name[32];
};

static struct status_state current_status = {0};

static void send_status_update(void) {
    if (!uart_dev || !usb_ready) {
        return;
    }

    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "{\"layer\":%d,\"name\":\"%s\",\"bat\":%d,\"wpm\":%d,\"caps\":%d,\"num\":%d,\"scrl\":%d,\"usb\":%d}\n",
        current_status.layer,
        current_status.layer_name,
        current_status.battery_central,
        current_status.wpm,
        current_status.caps_lock,
        current_status.num_lock,
        current_status.scroll_lock,
        current_status.output_usb
    );

    if (len > 0 && len < sizeof(buf)) {
        for (int i = 0; i < len; i++) {
            uart_poll_out(uart_dev, buf[i]);
        }
    }
}

static void update_layer_info(void) {
    current_status.layer = zmk_keymap_highest_layer_active();
    const char *label = zmk_keymap_layer_name(current_status.layer);
    if (label && label[0] != '\0') {
        strncpy(current_status.layer_name, label, sizeof(current_status.layer_name) - 1);
        current_status.layer_name[sizeof(current_status.layer_name) - 1] = '\0';
    } else {
        snprintf(current_status.layer_name, sizeof(current_status.layer_name), "L%d", current_status.layer);
    }
}

static void update_battery_info(void) {
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    current_status.battery_central = zmk_battery_state_of_charge();
#else
    current_status.battery_central = 0;
#endif
}

static void update_output_info(void) {
    struct zmk_endpoint_instance endpoint = zmk_endpoints_selected();
    current_status.output_usb = (endpoint.transport == ZMK_TRANSPORT_USB) ? 1 : 0;
}

static void update_hid_indicators(void) {
    zmk_hid_indicators_t indicators = zmk_hid_indicators_get_current_profile();
    current_status.caps_lock = (indicators & ZMK_HID_INDICATORS_CAPS_LOCK) ? 1 : 0;
    current_status.num_lock = (indicators & ZMK_HID_INDICATORS_NUM_LOCK) ? 1 : 0;
    current_status.scroll_lock = (indicators & ZMK_HID_INDICATORS_SCROLL_LOCK) ? 1 : 0;
}

static int layer_state_changed_listener(const zmk_event_t *eh) {
    update_layer_info();
    send_status_update();
    return ZMK_EV_EVENT_BUBBLE;
}

static int battery_state_changed_listener(const zmk_event_t *eh) {
    update_battery_info();
    send_status_update();
    return ZMK_EV_EVENT_BUBBLE;
}

#if IS_ENABLED(CONFIG_ZMK_WPM)
static int wpm_state_changed_listener(const zmk_event_t *eh) {
    const struct zmk_wpm_state_changed *ev = as_zmk_wpm_state_changed(eh);
    if (ev) {
        current_status.wpm = ev->state;
        send_status_update();
    }
    return ZMK_EV_EVENT_BUBBLE;
}
#endif

static int endpoint_changed_listener(const zmk_event_t *eh) {
    update_output_info();
    send_status_update();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(dongle_status_serial, layer_state_changed_listener);
ZMK_SUBSCRIPTION(dongle_status_serial, zmk_layer_state_changed);

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
ZMK_LISTENER(dongle_status_serial_battery, battery_state_changed_listener);
ZMK_SUBSCRIPTION(dongle_status_serial_battery, zmk_battery_state_changed);
#endif

#if IS_ENABLED(CONFIG_ZMK_WPM)
ZMK_LISTENER(dongle_status_serial_wpm, wpm_state_changed_listener);
ZMK_SUBSCRIPTION(dongle_status_serial_wpm, zmk_wpm_state_changed);
#endif

ZMK_LISTENER(dongle_status_serial_endpoint, endpoint_changed_listener);
ZMK_SUBSCRIPTION(dongle_status_serial_endpoint, zmk_endpoint_changed);

static void status_update_work_handler(struct k_work *work) {
    update_hid_indicators();
    send_status_update();
}

K_WORK_DEFINE(status_update_work, status_update_work_handler);

static void status_timer_handler(struct k_timer *timer) {
    k_work_submit(&status_update_work);
}

K_TIMER_DEFINE(status_timer, status_timer_handler, NULL);

static int dongle_status_serial_init(void) {
    LOG_INF("Initializing dongle status serial");

    // USB 초기화
    int ret = usb_enable(NULL);
    if (ret != 0 && ret != -EALREADY) {
        LOG_ERR("Failed to enable USB: %d", ret);
        return ret;
    }

    // USB 준비 대기
    k_sleep(K_MSEC(1000));
    
    // UART 장치 가져오기
    uart_dev = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));
    if (!uart_dev) {
        LOG_ERR("CDC ACM device not found");
        return -ENODEV;
    }

    if (!device_is_ready(uart_dev)) {
        LOG_ERR("CDC ACM device not ready");
        return -ENODEV;
    }

    usb_ready = true;

    // 초기 상태 업데이트
    update_layer_info();
    update_battery_info();
    update_output_info();
    update_hid_indicators();
    
    send_status_update();

    // 주기적 업데이트 시작 (1초마다)
    k_timer_start(&status_timer, K_MSEC(1000), K_MSEC(1000));

    LOG_INF("Dongle status serial initialized successfully");
    return 0;
}

SYS_INIT(dongle_status_serial_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
