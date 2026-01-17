#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/keymap.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
#include <zmk/hid_indicators.h>
#include <zmk/events/hid_indicators_changed.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
#include <zmk/events/battery_state_changed.h>
#include <zmk/battery.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_WPM)
#include <zmk/events/wpm_state_changed.h>
#endif

LOG_MODULE_REGISTER(dongle_serial, LOG_LEVEL_INF);

static const struct device *uart_dev;
static bool usb_ready = false;

struct status_state {
    uint8_t layer;
    uint8_t battery;
    uint8_t wpm;
    uint8_t caps;
    uint8_t num;
    uint8_t scroll;
    uint8_t output_usb;
    char layer_name[32];
};

static struct status_state status = {0};

static void send_status(void) {
    if (!uart_dev || !usb_ready) {
        return;
    }

    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "{\"l\":%d,\"n\":\"%s\",\"b\":%d,\"w\":%d,\"c\":%d,\"nm\":%d,\"s\":%d,\"u\":%d}\n",
        status.layer,
        status.layer_name,
        status.battery,
        status.wpm,
        status.caps,
        status.num,
        status.scroll,
        status.output_usb
    );

    if (len > 0 && len < sizeof(buf)) {
        for (int i = 0; i < len; i++) {
            uart_poll_out(uart_dev, buf[i]);
        }
    }
}

static void update_layer(void) {
    status.layer = zmk_keymap_highest_layer_active();
    const char *label = zmk_keymap_layer_name(status.layer);
    if (label && label[0] != '\0') {
        strncpy(status.layer_name, label, sizeof(status.layer_name) - 1);
        status.layer_name[sizeof(status.layer_name) - 1] = '\0';
    } else {
        snprintf(status.layer_name, sizeof(status.layer_name), "L%d", status.layer);
    }
}

static void update_battery(void) {
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    status.battery = zmk_battery_state_of_charge();
#else
    status.battery = 0;
#endif
}

static void update_output(void) {
    struct zmk_endpoint_instance ep = zmk_endpoints_selected();
    status.output_usb = (ep.transport == ZMK_TRANSPORT_USB) ? 1 : 0;
}

static void update_indicators(void) {
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
    zmk_hid_indicators_t indicators = zmk_hid_indicators_get_current_profile();
    status.caps = (indicators & (1 << 1)) ? 1 : 0;   // Caps Lock bit
    status.num = (indicators & (1 << 0)) ? 1 : 0;    // Num Lock bit
    status.scroll = (indicators & (1 << 2)) ? 1 : 0; // Scroll Lock bit
#else
    status.caps = 0;
    status.num = 0;
    status.scroll = 0;
#endif
}

static int layer_changed(const zmk_event_t *eh) {
    update_layer();
    send_status();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(dongle_serial_layer, layer_changed);
ZMK_SUBSCRIPTION(dongle_serial_layer, zmk_layer_state_changed);

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
static int battery_changed(const zmk_event_t *eh) {
    update_battery();
    send_status();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(dongle_serial_bat, battery_changed);
ZMK_SUBSCRIPTION(dongle_serial_bat, zmk_battery_state_changed);
#endif

#if IS_ENABLED(CONFIG_ZMK_WPM)
static int wpm_changed(const zmk_event_t *eh) {
    const struct zmk_wpm_state_changed *ev = as_zmk_wpm_state_changed(eh);
    if (ev) {
        status.wpm = ev->state;
        send_status();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(dongle_serial_wpm, wpm_changed);
ZMK_SUBSCRIPTION(dongle_serial_wpm, zmk_wpm_state_changed);
#endif

static int endpoint_changed(const zmk_event_t *eh) {
    update_output();
    send_status();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(dongle_serial_ep, endpoint_changed);
ZMK_SUBSCRIPTION(dongle_serial_ep, zmk_endpoint_changed);

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
static int hid_indicators_changed(const zmk_event_t *eh) {
    update_indicators();
    send_status();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(dongle_serial_ind, hid_indicators_changed);
ZMK_SUBSCRIPTION(dongle_serial_ind, zmk_hid_indicators_changed);
#endif

static void status_work_handler(struct k_work *work) {
    update_indicators();
    send_status();
}

K_WORK_DEFINE(status_work, status_work_handler);

static void status_timer_handler(struct k_timer *timer) {
    k_work_submit(&status_work);
}

K_TIMER_DEFINE(status_timer, status_timer_handler, NULL);

static int dongle_serial_init(void) {
    LOG_INF("Init dongle serial");

    int ret = usb_enable(NULL);
    if (ret != 0 && ret != -EALREADY) {
        LOG_ERR("USB enable failed: %d", ret);
        return ret;
    }

    k_sleep(K_MSEC(1000));
    
    uart_dev = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));
    if (!uart_dev) {
        LOG_ERR("CDC ACM not found");
        return -ENODEV;
    }

    if (!device_is_ready(uart_dev)) {
        LOG_ERR("CDC ACM not ready");
        return -ENODEV;
    }

    usb_ready = true;

    update_layer();
    update_battery();
    update_output();
    update_indicators();
    
    send_status();

    k_timer_start(&status_timer, K_MSEC(1000), K_MSEC(1000));

    LOG_INF("Dongle serial OK");
    return 0;
}

SYS_INIT(dongle_serial_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
