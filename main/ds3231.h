#pragma once
/*
 * DS3231 高精度 RTC 驱动（ESP-IDF 5.x，新 I2C 主机 API）
 * 芯片：DS3231MZ / DS3231SN，7 位地址 0x68
 * 特点：内置 TCXO + 温度补偿，±2ppm（一年约 1 分钟）
 * copyright @学 ARM Keil 的初中生
 */

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#define DS3231_ADDR 0x68      


typedef struct {
    i2c_master_dev_handle_t dev;
} ds3231_t;


esp_err_t ds3231_init(ds3231_t *rtc, i2c_master_bus_handle_t bus);

esp_err_t ds3231_read_time(ds3231_t *rtc, struct tm *t);

esp_err_t ds3231_write_time(ds3231_t *rtc, const struct tm *t);

esp_err_t ds3231_read_temp(ds3231_t *rtc, float *celsius);

esp_err_t ds3231_check_osf(ds3231_t *rtc, bool *lost);
esp_err_t ds3231_clear_osf(ds3231_t *rtc);
/* 用编译时刻自动对时（__DATE__ / __TIME__），适合开发调试 */
esp_err_t ds3231_set_from_build(ds3231_t *rtc);
esp_err_t ds3231_set_string(ds3231_t *rtc, const char *str, const char *format);