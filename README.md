# esp32clock

ESP32-S3 桌面时钟固件 —— **手写 DS3231 驱动，不依赖第三方库**。

重点不是时钟，是从数据手册到能跑通的驱动这一整套方法：I2C 传输层、BCD 编解码、掩码处理、掉电标志、以及"断电还走时"必须的两个寄存器位。

## 硬件

| 器件 | 说明 |
|---|---|
| 主控 | ESP32-S3（开发板即可，本仓库用最小系统板验证） |
| RTC | DS3231 模块（内置 TCXO，±2ppm） |
| 总线 | I2C，SDA=GPIO18，SCL=GPIO8，400 kHz |

接线（模块 → ESP32-S3）：

```
VCC → 3V3
GND → GND
SDA → GPIO18
SCL → GPIO8
```

模块自带 4.7 kΩ 上拉；若用裸芯片，务必外加上拉到 3.3 V。

## 目录

```
main/
  ds3231.c / ds3231.h   驱动本体（寄存器读写、BCD、时间、温度、OSF）
  main.c                示例：建总线 → 初始化 → 每秒打印
```

## 编译烧录

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COM7 flash monitor
```

基于 ESP-IDF v6.1，使用新的 I2C 主机 API（`driver/i2c_master.h`）。

## 驱动要点

### 1. 时间寄存器是 BCD

芯片存的是 `0x23`（=23），不是 `0x17`（=23 的二进制）。

```c
static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
```

判断错了不报错，只是时间悄悄不对——这是 BCD 最坑的地方。

### 2. 每个字节都有藏起来的位

| 寄存器 | 掩码 | 原因 |
|---|---|---|
| 秒 `0x00` | `& 0x7F` | bit7 保留 |
| 时 `0x02` | `& 0x3F` | bit6 12/24 模式、bit5 AM/PM |
| 月 `0x05` | `& 0x1F` | bit7 世纪位 |

漏了掩码，13 点会读成 83 点。

### 3. 断电走时靠两个位

```c
/* 控制寄存器 0x0E bit7 = EOSC：0 = 电池供电时振荡器继续跑 */
ctrl &= ~0x80;

/* 状态寄存器 0x0F bit7 = OSF：1 = 掉过电，时间不可信，写 0 清除 */
```

- **EOSC** 是开关，**OSF** 是账本；
- 初始化顺序必须是：查 OSF → 需要时对时 → 再清 OSF。反了就把证据扔了。

### 4. 连读 7 个字节

```c
reg_read(rtc, 0x00, r, 7);   /* 秒 分 时 星期 日 月 年，一次事务 */
```

地址自增保证这 7 个字节是同一瞬间的快照，分 7 次读可能读到"59 秒 + 下一分"。

### 5. 温度

```c
*c = (int8_t)r[0] + (float)(r[1] >> 6) * 0.25f;
```

`0x11` 是有符号整数部分（必须用 `int8_t`），`0x12` 的高两位是 0.25 °C 的小数。

## API

```c
esp_err_t ds3231_init(ds3231_t *rtc, i2c_master_bus_handle_t bus);
esp_err_t ds3231_read_time(ds3231_t *rtc, struct tm *t);
esp_err_t ds3231_write_time(ds3231_t *rtc, const struct tm *t);
esp_err_t ds3231_read_temp(ds3231_t *rtc, float *celsius);
esp_err_t ds3231_check_osf(ds3231_t *rtc, bool *lost);
esp_err_t ds3231_clear_osf(ds3231_t *rtc);
esp_err_t ds3231_set_string(ds3231_t *rtc, const char *str, const char *format);
esp_err_t ds3231_set_from_build(ds3231_t *rtc);   /* 调试用，量产请删掉 */
```

## 自检清单

1. `i2c_master_probe(bus, 0x68, 100)` 返回 `ESP_OK`；
2. 读 `0x0F`，`OSF=1` 说明是空片或掉过电；
3. 写时间后立即读回，秒差 ≤ 1；
4. 拔电源 10 秒再上电，时间没归零；
5. 手摸芯片，温度读数上升。

第 4 条不过 → 查电池、查 `EOSC`；第 3 条不过 → 查 BCD 和掩码。

## License

MIT
