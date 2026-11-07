/*
=======DS3231 Driver==== copyright @学 ARM Keil 的初中生
基于ESP-IDF 6.1;
寄存器分配: 
0x00 : 秒
0x01 : 分
0x02 : 时
0x03 : 星期 ! 不是日哦
0x04 : 日
0x05 : 月
0x06 : 年
*/
#include "ds3231.h"
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "esp_err.h"
#include <stdint.h>
#include <string.h>
#include <stdbool.h>    
#include <stddef.h>  
/*
BCD 编解码
    DS3231 用 BCD 存时间：一个字节里，高 4 位放十位，低 4 位放个位
    例：23 点 → 0x23（不是 0x17）
*/

//BCD -> 十进制

static uint8_t bcd2bin(uint8_t v) 
{ 
    return (v >> 4) * 10 + (v & 0x0F); 
}

//十进制 -> BCD
static uint8_t bin2bcd(uint8_t v) 
{ 
    return (uint8_t)(((v / 10) << 4) | (v % 10)); 
}
/*
读寄存器
*/
static esp_err_t reg_read(ds3231_t *rtc,uint8_t reg,uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(
        rtc->dev,
        &reg ,1, //写寄存器 1b
        buf, n, //读寄存器 n byte
        100    // 100ms超时
    );
}
/*
写寄存器
*/
static esp_err_t reg_write(ds3231_t *rtc,uint8_t reg,uint8_t *buf, size_t n)
{
    uint8_t  tmp[8];
    if(n+1>sizeof(tmp))       //最多7字节时间+1字节地址，超过返回ESP_ERR_INVALID_SIZE
    {
        return ESP_ERR_INVALID_SIZE;
    }
    tmp[0] = reg;      //第一字节永远是寄存器地址
    memcpy(tmp + 1, buf, n); //后面是数据
    return i2c_master_transmit(rtc->dev, tmp, n + 1, 100); 
}

/*
公开API
*/

esp_err_t ds3231_init(ds3231_t *rtc, i2c_master_bus_handle_t bus)
{
    /* 设备配置：7 位地址 + 400kHz
     * 400kHz 是 DS3231 支持的最高速率（手册 Fast Mode） */
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = DS3231_ADDR,
        .scl_speed_hz    = 400000,
    };
    /* 把设备挂到总线上，拿到句柄存进 rtc->dev */
    esp_err_t err = i2c_master_bus_add_device(
        bus, &cfg, &rtc->dev);
    if (err != ESP_OK) return err;   /* 挂不上就直接返回，别往下跑 */
    bool lost=false;
    if (ds3231_check_osf(rtc, &lost) == ESP_OK && lost) {
        ds3231_clear_osf(rtc);                   /* 写 0 清除，表示"我知道时间需要重设" */
    }
    /*
    清 EOSC：控制寄存器 0x0E 的 bit7
         EOSC=1 时，一旦主电源断开，芯片会关掉内部振荡器省电 —— 时间就停了！
        我们做的是"断电也要走时"的时钟，必须把它清成 0 
    */
    uint8_t ctrl;
    if(reg_read(rtc, 0x0E, &ctrl, 1)==ESP_OK){
        ctrl &= ~0x80; /* bit7 清 0，其它位保持原样 */
        reg_write(rtc, 0x0E, &ctrl, 1);
    }
    return ESP_OK;


}
esp_err_t ds3231_read_time(ds3231_t *rtc, struct tm *t)
{
    uint8_t r[7];
     /* 从 0x00 开始连读 7 个字节：秒 分 时 星期 日 月 年
     连读的好处：这 7 个字节是同一瞬间的快照，
     分开读可能在秒进位时读到 59 秒 + 下一秒的分，出现"时间倒流" 
     */
        esp_err_t err = reg_read(rtc, 0x00, r, 7);
    if (err != ESP_OK) return err;

    /* 秒：bit7 保留，& 0x7F 抹掉 */
    t->tm_sec = bcd2bin(r[0] & 0x7F);

    /* 分：bit7 保留 */
    t->tm_min = bcd2bin(r[1] & 0x7F);

    /* 时：bit6 = 12/24 小时模式，bit5 = am/pm
     * 不过滤的话，13 点读出来是 0x53（含模式位），会变成 83 点 */
    t->tm_hour = bcd2bin(r[2] & 0x3F);

    /* 星期：0x03 存的不是日期，是星期几（1~7） */
    t->tm_wday = bcd2bin(r[3] & 0x07) - 1;       /* -1 转成 tm 的 0~6 */

    /* 日：bit6/7 保留 */
    t->tm_mday = bcd2bin(r[4] & 0x3F);

    /* 月：bit7 是世纪位（21xx 年用），所以只取低 5 位
     * tm_mon 是 0~11，硬件是 1~12，读出来要 -1 */
    t->tm_mon = bcd2bin(r[5] & 0x1F) - 1;

    /* 年：硬件存 00~99，tm_year 是"距 1900 年"，所以要 +100（2000~2099） */
    t->tm_year = bcd2bin(r[6]) + 100;

    return ESP_OK;





}
esp_err_t ds3231_write_time(ds3231_t *rtc, const struct tm *t)
{
    /* 按寄存器顺序组装 7 个字节，全部转成 BCD */
    uint8_t r[7] = {
        bin2bcd(t->tm_sec),
        bin2bcd(t->tm_min),
        bin2bcd(t->tm_hour),                     /* 24 小时模式，bit6=0 自然满足 */
        bin2bcd(t->tm_wday + 1),                 /* tm_wday 0~6 → 硬件 1~7 */
        bin2bcd(t->tm_mday),
        bin2bcd(t->tm_mon + 1),                  /* tm_mon 0~11 → 硬件 1~12 */
        bin2bcd(t->tm_year % 100),               /* 只取后两位，2026 → 26 */
    };

    /* 从 0x00 连续写 7 字节，芯片内部地址自动递增 */
    return reg_write(rtc, 0x00, r, 7);


}
esp_err_t ds3231_read_temp(ds3231_t *rtc, float *c)
{
    uint8_t r[2];

    /* 0x11 = 温度整数部分（有符号，-40~+85）
     * 0x12 的高 2 位 = 小数部分，每档 0.25°C（00=0, 01=0.25, 10=0.5, 11=0.75） */
    esp_err_t err = reg_read(rtc, 0x11, r, 2);
    if (err != ESP_OK) return err;

    /* 必须用 int8_t：零下温度在 0x11 里是负数补码，
     * 用 uint8_t 会把 -5°C 读成 251°C */
    *c = (int8_t)r[0] + (float)(r[1] >> 6) * 0.25f;

    return ESP_OK;
}
esp_err_t ds3231_check_osf(ds3231_t *rtc, bool *lost)
{
    uint8_t st;

    /* 状态寄存器 0x0F 的 bit7 = OSF（Oscillator Stop Flag）
     * 芯片检测到振荡器停过（掉电、电池没电）就会硬件置 1，
     * 且只有软件写 0 才能清除 */
    esp_err_t err = reg_read(rtc, 0x0F, &st, 1);
    if (err != ESP_OK) return err;

    *lost = (st & 0x80) != 0;
    return ESP_OK;
}

esp_err_t ds3231_clear_osf(ds3231_t *rtc)
{
    uint8_t st;

    /* 读-改-写：只清 bit7，其它标志位（A1F/A2F/EN32kHz）不能被误伤 */
    esp_err_t err = reg_read(rtc, 0x0F, &st, 1);
    if (err != ESP_OK) return err;

    st &= ~0x80;
    return reg_write(rtc, 0x0F, &st, 1);
}



esp_err_t ds3231_set_string(ds3231_t *rtc, const char *str, const char *format)
{
    if (!rtc || !str || !format) return ESP_ERR_INVALID_ARG;
    struct tm t = {0};

    /* strptime 只解析它认识的字段，返回指向"没解析完"的位置；
     * 返回 NULL 说明格式和字符串对不上 */
    char *end = strptime(str, format, &t);
    if (!end) return ESP_ERR_INVALID_ARG;

    /* 关键：mktime 会补全 tm_wday / tm_yday，并把非法日期归一化
     *（比如 2 月 30 日会变成 3 月 2 日）。
     * 不调用它，tm_wday 就是 0，星期会永远写成"一" */
    t.tm_isdst = 0;
    if (mktime(&t) == (time_t)-1) return ESP_ERR_INVALID_ARG;

    return ds3231_write_time(rtc, &t);
}

esp_err_t ds3231_set_from_build(ds3231_t *rtc)
{
    char buf[40];
    snprintf(buf, sizeof(buf), "%s %s", __DATE__, __TIME__);
    return ds3231_set_string(rtc, buf, "%b %d %Y %H:%M:%S");






}
