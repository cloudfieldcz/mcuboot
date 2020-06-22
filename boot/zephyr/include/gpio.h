#pragma once

#include <device.h>

#define dt_gpio_pin(alias) DT_ALIAS_ ## alias ## _GPIOS_PIN
#define dt_gpio_label(alias) DT_ALIAS_ ## alias ## _LABEL

#define dt_gpio_init(alias, extra_flags) gpio_init(\
    DT_ALIAS_ ## alias ## _GPIOS_CONTROLLER,\
    DT_ALIAS_ ## alias ## _LABEL,\
    DT_ALIAS_ ## alias ## _GPIOS_PIN,\
    DT_ALIAS_ ## alias ## _GPIOS_FLAGS | extra_flags)

#define dt_gpio_set(dev, alias, value) gpio_pin_set(dev, DT_ALIAS_ ## alias ## _GPIOS_PIN, value)

struct device* gpio_init(const char* ctrl, const char* label, gpio_pin_t pin, gpio_flags_t flags);
