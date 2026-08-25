# HomeUI / Debug Settings 固件兼容列表

本表仅覆盖 `Sony Dumps` 中存在可用 `NPXS40002` HomeUI dump 的固件。
`Nav patch` 与 `Debug Settings` 的“通过”表示对应原始 dump 已通过离线二进制
profile 匹配、定点 patch、不变式及幂等性验证，不代表已在每个固件上完成真机
测试。

`2.30`、`2.50` archive 内的 `NPXS40008.bin` 均为 0 字节，没有可分析的
Settings dump，因此按“无可用 dump 可跳过”记为不适用。

| 固件 | Nav patch | Debug Settings | Settings route | Dump 验证 |
|------|-----------|----------------|----------------|-----------|
| 2.30 | 通过 | 不适用（dump 为空） | 不适用 | HomeUI 通过 |
| 2.50 | 通过 | 不适用（dump 为空） | 不适用 | HomeUI 通过 |
| 3.00 | 通过 | 通过 | `debug_settings` | 通过 |
| 3.10 | 通过 | 通过 | `debug_settings` | 通过 |
| 3.20 | 通过 | 通过 | `debug_settings` | 通过 |
| 3.21 | 通过 | 通过 | `debug_settings` | 通过 |
| 4.00 | 通过 | 通过 | `debug_settings` | 通过 |
| 4.02 | 通过 | 通过 | `debug_settings` | 通过 |
| 4.03 | 通过 | 通过 | `debug_settings` | 通过 |
| 4.50 | 通过 | 通过 | `debug_settings` | 通过 |
| 4.51 | 通过 | 通过 | `debug_settings` | 通过 |
| 5.00 | 通过 | 通过 | `debug_settings` | 通过 |
| 5.02 | 通过 | 通过 | `debug_settings` | 通过 |
| 5.10 | 通过 | 通过 | `debug_settings` | 通过 |
| 5.50 | 通过 | 通过 | `debug_settings` | 通过 |
| 6.00 | 通过 | 通过 | `debug_settings` | 通过 |
| 6.02 | 通过 | 通过 | `debug_settings` | 通过 |
| 6.50 | 通过 | 通过 | `debug_settings` | 通过 |
| 7.00 | 通过 | 通过 | `debug_settings` | 通过 |
| 7.01 | 通过 | 通过 | `debug_settings` | 通过 |
| 7.01.01 | 通过 | 通过 | `debug_settings` | 通过 |
| 7.20 | 通过 | 通过 | `debug_settings` | 通过 |
| 7.40 | 通过 | 通过 | `debug_settings` | 通过 |
| 7.60 | 通过 | 通过 | `debug_settings` | 通过 |
| 7.61 | 通过 | 通过 | `debug_settings` | 通过 |
| 8.00 | 通过 | 通过 | `debug_settings` | 通过 |
| 8.20 | 通过 | 通过 | `debug_settings` | 通过 |
| 8.20.02 | 通过 | 通过 | `debug_settings` | 通过 |
| 8.40 | 通过 | 通过 | `debug_settings` | 通过 |
| 8.60 | 通过 | 通过 | `debug_settings` | 通过 |
| 9.00 | 通过 | 通过 | `debug_settings` | 通过 |
| 9.40 | 通过 | 通过 | `debug_settings` | 通过 |
| 9.60 | 通过 | 通过 | `debug_settings` | 通过 |
| 10.00 | 通过 | 通过 | `debug_settings` | 通过 |
| 10.01 | 通过 | 通过 | `debug_settings` | 通过 |
| 10.20 | 通过 | 通过 | `debug_settings` | 通过 |
| 10.40 | 通过 | 通过 | `debug_settings` | 通过 |
| 10.60 | 通过 | 通过 | `debug_settings` | 通过 |
| 11.00 | 通过 | 通过 | `debug_settings_old` | 通过 |
| 11.20 | 通过 | 通过 | `debug_settings_old` | 通过 |
| 11.40 | 通过 | 通过 | `debug_settings_old` | 通过 |
| 11.60 | 通过 | 通过 | `debug_settings_old` | 通过 |
| 12.00 | 通过 | 通过 | `debug_settings_old` | 通过 |
| 12.02 | 通过 | 通过 | `debug_settings_old` | 通过 |
| 12.20 | 通过 | 通过 | `debug_settings_old` | 通过 |
| 12.40 | 通过 | 通过 | `debug_settings_old` | 通过 |
| 12.60 | 通过 | 通过 | `debug_settings_old` | 通过 |
| 12.70 | 通过 | 通过 | `debug_settings_old` | 通过 |

汇总：HomeUI `48/48` 通过；非空 Settings dump `46/46` 通过；空 Settings
dump `2/2` 跳过。
