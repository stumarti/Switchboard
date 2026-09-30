#pragma once
typedef int gpio_num_t;
enum { GPIO_NUM_0 = 0, GPIO_NUM_1 = 1, GPIO_NUM_3 = 3, GPIO_NUM_7 = 7 };
inline int gpio_hold_en(gpio_num_t) { return 0; }
inline int gpio_hold_dis(gpio_num_t) { return 0; }
inline void gpio_deep_sleep_hold_en() {}
inline void gpio_deep_sleep_hold_dis() {}
