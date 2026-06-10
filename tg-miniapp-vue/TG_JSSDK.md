# Telegram Mini App JSSDK 源码整理

来源说明：当前网络直连 `https://telegram.org/js/telegram-web-app.js` 超时，所以本次源码阅读使用 `miku-web-app@6.7.0/src/esm/telegram/webapp.mjs` 里的官方脚本镜像实现；较新的 API 名称按 Telegram 官方 Bot Web Apps 文档补充。以实际加载的官方脚本为最终准。

## 加载与环境

官方脚本挂载在：

```js
window.Telegram.WebApp
```

Mini App 启动时，Telegram 会把参数放在 URL search/hash：

- `tgWebAppData`
- `tgWebAppVersion`
- `tgWebAppPlatform`
- `tgWebAppThemeParams`

本项目先判断这些参数，只在 Mini App 环境异步加载 JSSDK。

## 核心属性

源码里通过 `Object.defineProperty(WebApp, ...)` 暴露：

| 属性 | 说明 |
| --- | --- |
| `initData` | 原始签名字符串，发后端校验用 |
| `initDataUnsafe` | 前端解析后的对象，只适合展示调试 |
| `version` | WebApp API 版本 |
| `platform` | 平台，例如 `ios`、`android`、`tdesktop`、`web` |
| `colorScheme` | `light` / `dark` |
| `themeParams` | Telegram 主题颜色 |
| `isExpanded` | 是否展开 |
| `viewportHeight` | 当前视口高度 |
| `viewportStableHeight` | 稳定视口高度 |
| `isClosingConfirmationEnabled` | 是否开启关闭确认 |
| `headerColor` | 顶部颜色 |
| `backgroundColor` | 背景颜色 |
| `BackButton` | 返回按钮对象 |
| `MainButton` | 主按钮对象 |
| `HapticFeedback` | 触感反馈对象 |

较新版本还会有 `isFullscreen`、`isOrientationLocked`、`bottomBarColor` 等属性。

## initDataUnsafe

常见字段：

- `query_id`
- `user`
- `receiver`
- `chat`
- `chat_type`
- `chat_instance`
- `start_param`
- `can_send_after`
- `auth_date`
- `hash`

`query_id` 是本次 Web App 查询 ID，主要给后端调用 Bot API `answerWebAppQuery` 用，把 inline 结果发回当前聊天。它不是用户 ID，不是长期 token，也不是每种启动方式都有。

## WebApp 方法

源码镜像里能看到这些方法：

| 方法 | 触发的底层事件 | 说明 |
| --- | --- | --- |
| `setHeaderColor(color)` | `web_app_set_header_color` | 设置顶部颜色 |
| `setBackgroundColor(color)` | `web_app_set_background_color` | 设置背景颜色 |
| `enableClosingConfirmation()` | `web_app_setup_closing_behavior` | 开启关闭确认 |
| `disableClosingConfirmation()` | `web_app_setup_closing_behavior` | 关闭确认 |
| `isVersionAtLeast(version)` | 本地判断 | 判断客户端 API 版本 |
| `onEvent(type, cb)` | WebView 事件订阅 | 监听事件 |
| `offEvent(type, cb)` | WebView 事件取消 | 移除监听 |
| `sendData(data)` | `web_app_data_send` | 向 bot 发送数据，最大约 4096 字符 |
| `switchInlineQuery(query, chatTypes?)` | `web_app_switch_inline_query` | 打开 inline 分享选择 |
| `openLink(url, options?)` | `web_app_open_link` | 外部浏览器打开普通 URL |
| `openTelegramLink(url)` | `web_app_open_tg_link` | 打开 `t.me` 链接 |
| `openInvoice(urlOrSlug, cb?)` | `web_app_open_invoice` | 打开 Invoice |
| `showPopup(params, cb?)` | `web_app_open_popup` | 弹出自定义弹窗 |
| `showAlert(message, cb?)` | `web_app_open_popup` | Alert |
| `showConfirm(message, cb?)` | `web_app_open_popup` | Confirm |
| `showScanQrPopup(params, cb?)` | `web_app_open_scan_qr_popup` | 扫码弹窗 |
| `closeScanQrPopup()` | `web_app_close_scan_qr_popup` | 关闭扫码 |
| `readTextFromClipboard(cb?)` | `web_app_read_text_from_clipboard` | 读取剪贴板文本 |
| `ready()` | `web_app_ready` | 通知页面就绪 |
| `expand()` | `web_app_expand` | 展开 Mini App |
| `close()` | `web_app_close` | 关闭 Mini App |

官方较新版本还包含：

| 方法/对象 | 说明 |
| --- | --- |
| `requestFullscreen()` / `exitFullscreen()` | 全屏控制 |
| `lockOrientation()` / `unlockOrientation()` | 方向锁 |
| `hideKeyboard()` | 隐藏键盘 |
| `requestContact()` | 请求联系人 |
| `requestLocation()` | 请求位置 |
| `requestWriteAccess()` | 请求 bot 写入权限 |
| `requestEmojiStatusAccess()` | 请求表情状态权限 |
| `requestChat()` | 请求选择聊天 |
| `shareToStory(mediaUrl, params?)` | 分享到 Story |
| `downloadFile(params, cb?)` | Telegram 原生文件下载，本项目未接入 |
| `CloudStorage` | 云存储 |
| `DeviceStorage` | 设备本地存储 |
| `SecureStorage` | 安全存储 |
| `BiometricManager` | 生物识别 |
| `LocationManager` | 位置管理 |
| `Accelerometer` / `Gyroscope` / `DeviceOrientation` | 传感器 |

## UI 对象

源码里暴露的对象：

### BackButton

- `BackButton.isVisible`
- `BackButton.show()`
- `BackButton.hide()`
- `BackButton.onClick(cb)`
- `BackButton.offClick(cb)`

### MainButton

- `MainButton.text`
- `MainButton.color`
- `MainButton.textColor`
- `MainButton.isVisible`
- `MainButton.isActive`
- `MainButton.isProgressVisible`
- `MainButton.setText(text)`
- `MainButton.onClick(cb)`
- `MainButton.offClick(cb)`
- `MainButton.show()`
- `MainButton.hide()`
- `MainButton.enable()`
- `MainButton.disable()`
- `MainButton.showProgress(leaveActive?)`
- `MainButton.hideProgress()`
- `MainButton.setParams(params)`

### SettingsButton

源码里有内部 `SettingsButton`，较新官方脚本对外也暴露：

- `SettingsButton.isVisible`
- `SettingsButton.show()`
- `SettingsButton.hide()`
- `SettingsButton.onClick(cb)`
- `SettingsButton.offClick(cb)`

### HapticFeedback

- `impactOccurred(style)`
- `notificationOccurred(type)`
- `selectionChanged()`

## 事件

源码里绑定的 WebView 事件：

| WebView 事件 | 对外事件 |
| --- | --- |
| `theme_changed` | `themeChanged` |
| `viewport_changed` | `viewportChanged` |
| `invoice_closed` | `invoiceClosed` |
| `popup_closed` | `popupClosed` |
| `qr_text_received` | `qrTextReceived` |
| `scan_qr_popup_closed` | `scanQrPopupClosed` |
| `clipboard_text_received` | `clipboardTextReceived` |
| `back_button_pressed` | `backButtonClicked` |
| `main_button_pressed` | `mainButtonClicked` |
| `settings_button_pressed` | `settingsButtonClicked` |

较新版本还包含：

- `secondaryButtonClicked`
- `fullscreenChanged`
- `fullscreenFailed`
- `homeScreenAdded`
- `homeScreenChecked`
- `safeAreaChanged`
- `contentSafeAreaChanged`
- `accelerometerStarted` / `accelerometerStopped` / `accelerometerChanged` / `accelerometerFailed`
- `gyroscopeStarted` / `gyroscopeStopped` / `gyroscopeChanged` / `gyroscopeFailed`
- `deviceOrientationStarted` / `deviceOrientationStopped` / `deviceOrientationChanged` / `deviceOrientationFailed`
- `locationManagerUpdated`
- `deviceStorage*`
- `secureStorage*`

## 链接与下载

- `openLink(url)` 是外部浏览器打开 URL。
- `openTelegramLink(url)` 只适合 Telegram 链接，例如 `https://t.me/...`。
- `downloadFile({ url, file_name })` 要求 Telegram 客户端能访问到真实文件 URL；本地 `localhost`、局域网 HTTP、`blob:`、`data:` 都可能失败。
- 当前项目已经移除下载按钮和下载接口。

## openLink 限制

源码里 `openLink` 最终发送：

```js
web_app_open_link
```

并携带：

```js
{ url, try_instant_view }
```

需要注意：

- `url` 必须是完整 `http://` 或 `https://` URL，不能传相对路径、`data:`、`blob:`。
- 它用于普通网页外部打开；Telegram 链接应使用 `openTelegramLink`。
- `try_browser` 不是官方 `openLink` 参数，传了可能报错或被忽略。
- `try_instant_view` 只在支持 Instant View 的页面和客户端版本下有意义。
- 文件下载要让外部浏览器能访问 URL；局域网地址只在手机和开发机同网且能访问时才行。
- iOS/Android/桌面客户端行为会不同，可能不是立刻下载，而是先预览或打开系统浏览器页面。
- 用户手势很重要，最好只在按钮点击里调用，不要自动调用。

## 当前项目保留的分享

当前项目只保留：

- `TG 分享链接`：通过 `openTelegramLink('https://t.me/share/url?...')`

已移除系统分享、Story 分享、聊天/群/频道 inline 分享按钮。

## 安全

- 后端鉴权必须校验原始 `initData` 的 hash。
- 不要信任前端 `initDataUnsafe`。
- 新 API 调用前先判断 `typeof WebApp.xxx === 'function'`。
- 涉及下载、Story 媒体、Invoice 的 URL，最好用公网 HTTPS 真机测试。
