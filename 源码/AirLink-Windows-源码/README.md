# AirLink 电脑版（Windows）源码

单文件 Win32 程序，C++17 + GDI+，无运行时依赖。

## 目录

```
src/app.h        公共结构体与所有模块的声明
src/main.cpp     程序入口、窗口、消息循环
src/ui.cpp       全部自绘界面（GDI+）、页面、交互
src/net.cpp      网络层：发现、传输、会话、同步、IPv6、鉴权
src/util.cpp     配置文件、记录、设备日志、编解码工具
src/wan.cpp      UPnP 端口映射 + 公网地址探测
src/qr.cpp       二维码编码器（自己实现，无第三方库）
build.ps1        构建脚本
make_icon.ps1    图标生成
```

## 构建依赖

需要 **MinGW-w64 (w64devkit)** 提供 `g++`、`windres`，并放在 PATH 里。
程序静态链接，产物不依赖任何 DLL。

```
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

产物在 `out\AirLink.exe`。

## 主要设计

- **传输**：TCP 45678，自定义二进制协议，所有整数小端；UDP 45679 做局域网广播发现
- **双栈监听**：一个 socket 同时收 IPv4/IPv6，IPv4 对端会以 `::ffff:a.b.c.d` 出现，取地址时已折叠回普通 IPv4
- **鉴权**：非内网来源（10./192.168./172.16-31./127. 以外）必须先发送正确的 6 位配对码，通过后该 IP 在 10 分钟内免检
- **记录**：传输开始即写入一条「传输中」记录，结束时原地更新状态，记录页按 id 匹配活动传输显示进度条
- **WAN**：优先 IPv6 直连（无需端口映射），IPv6 不可用时退回 UPnP 自动映射

## 配置文件

`%APPDATA%\AirLink\` 下：`settings.ini`、`history.txt`、`devices.txt`
