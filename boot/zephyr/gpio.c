#include <drivers/gpio.h>
#include <syscalls/device.h>
#include "gpio.h"
#include "bootutil/bootutil_log.h"

MCUBOOT_LOG_MODULE_REGISTER(gpio);

struct device* gpio_init(const char* ctrl, const char* label, gpio_pin_t pin, gpio_flags_t flags)
{
    struct device *dev = NULL;
    int ret;

    BOOT_LOG_DBG("%s: %s (%u) <- 0x%08x", ctrl, label, pin, flags);
    dev = device_get_binding(ctrl);

    if (dev == NULL) {
        BOOT_LOG_ERR("unable bind %s", ctrl);
        return NULL;
    }

    ret = gpio_pin_configure(dev, pin, flags);
    if (ret < 0) {
        BOOT_LOG_ERR("unable gpio_pin_configure %s (%i)", label, ret);
        return NULL;
    }

    return dev;
}
