#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ds3231.h"

#define PIN_SDA 18
#define PIN_SCL 8

static ds3231_t s_rtc;

/* 上电初始化：只在"时间不可信"时才重新对时 */
static void rtc_bringup(i2c_master_bus_handle_t bus)
{
    ESP_ERROR_CHECK(ds3231_init(&s_rtc, bus));

    bool lost = false;
    ds3231_check_osf(&s_rtc, &lost);            // 芯片掉过电会把 OSF 置 1

    struct tm now = {0};
    bool readable = (ds3231_read_time(&s_rtc, &now) == ESP_OK);
    bool plausible = readable && now.tm_year >= 100;   // 年份 < 2000 视为没设过

    if (lost || !plausible) {
        printf("RTC 未设时/掉过电，用编译时间对时\n");
        ESP_ERROR_CHECK(ds3231_set_from_build(&s_rtc));
        ds3231_clear_osf(&s_rtc);               // 设完再清标志
    }
}

void app_main(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,  // 模块上有 4.7k，别开内部弱上拉
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    esp_err_t err = i2c_master_probe(bus, 0x68, 100);
    if (err != ESP_OK) {
        printf("DS3231 没应答（%s）：查接线/上拉/引脚\n", esp_err_to_name(err));
        return;
    }

    rtc_bringup(bus);

    /* 主循环：200ms 轮询，只在"秒"变化时刷新
     *   为什么不是 1000ms：固定 1s 间隔会和 RTC 的秒边界错开，
     *   显示永远慢 0~1 秒；200ms 轮询能在 200ms 内捕捉到秒跳变。*/
    int last_sec = -1;
    TickType_t next = xTaskGetTickCount();

    for (;;) {
        struct tm now = {0};
        esp_err_t e = ds3231_read_time(&s_rtc, &now);

        if (e != ESP_OK) {
            printf("read error: %s\n", esp_err_to_name(e));
        } else if (now.tm_sec != last_sec) {
            last_sec = now.tm_sec;
            printf("%04d-%02d-%02d %02d:%02d:%02d\n",
                   1900 + now.tm_year, now.tm_mon + 1, now.tm_mday,
                   now.tm_hour, now.tm_min, now.tm_sec);
        }

        /* vTaskDelayUntil 保证周期稳定：任务执行耗时会自动补偿，
         * 而 vTaskDelay 是"从现在起再等"，误差会累积 */
        vTaskDelayUntil(&next, pdMS_TO_TICKS(200));
    }
}