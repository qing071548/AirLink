# AirLink 手机版（Android）源码

纯 Java，不使用 AndroidX / Kotlin / Gradle，界面全部用自定义 View 的 Canvas 绘制。

## 目录

```
src/com/airlink/MainActivity.java    入口、页面调度、传输编排、权限
src/com/airlink/Ui.java              全部自绘界面与触摸处理
src/com/airlink/Net.java             网络层：发现、传输、会话、同步、鉴权
src/com/airlink/Store.java           配置持久化
src/com/airlink/Dlg.java             轻量圆角对话框
src/com/airlink/ScanActivity.java    相机扫码（ZXing 解码）
src/com/airlink/KeepAliveService.java 前台服务保活
src/com/airlink/Paths.java           存储路径解析
src/com/airlink/Skin.java            配色与字体常量
libs/zxing-core-3.5.3.jar            二维码识别库（唯一第三方依赖）
AndroidManifest.xml                  清单
build.ps1                            构建脚本
```

## 构建依赖

需要 **JDK**（javac 11+）和 **Android SDK**（`aapt2`、`d8`、`zipalign`、`apksigner`、`android.jar`）。
脚本会从环境变量或默认路径查找，这些工具未随包提供。

```
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

产物在 `out\AirLink.apk`（已签名）。

## 主要设计

- **传输**：TCP 45678，协议与电脑版逐字节一致；整数一律小端，不使用 `DataOutputStream`
- **WAN**：支持 `[IPv6]:端口`、裸 IPv6、`IPv4:端口` 三种地址写法；连接前先发送配对码
- **扫码**：预览帧旋转后送入 ZXing；失败时再对中心区域做 2 倍放大重试；优先微距对焦并每 0.9 秒重新对焦
- **保活**：前台服务 + 定时唤醒锁 + Wi-Fi 锁（逐级降级），锁屏广播触发重新武装
- **记录**：与电脑版一致，传输开始即写记录，按 id 同步状态

## 权限说明

- `MANAGE_EXTERNAL_STORAGE`：读写整个存储（接收目录、浏览文件）
- 相机：扫码
- 前台服务 / 唤醒锁 / 忽略电池优化：熄屏后保持连接

## 说明

界面里仍保留少量已下线功能（屏幕镜像）的辅助方法，它们不被任何入口调用，不影响运行；
如需精简可直接删除 `Ui` 中 `mirror*` 开头的字段与方法、`Net` 中 `screenFrame/streamFrames/sendInput`。
