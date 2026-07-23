# 温控欺骗 快捷开关

AAaTempSpoof 模块的 Android 控制中心（Quick Settings Tile）控制器。

## 功能

| 功能 | 说明 |
|------|------|
| QS Tile | 从控制中心一键开关温控欺骗 |
| Root 检测 | 自动检测 Root 权限，无权限时给出提示 |
| 平滑动画 | 按钮颜色过渡 + 点击缩放反馈 |
| 状态同步 | App 与 Tile 双向同步当前状态 |

## 控制路径

```
/data/adb/modules/AAaTempSpoof/AaTempSpoof/总开关.txt
```
- 开 → 写入 `1`
- 关 → 写入 `0`

---

## 构建方法

### 方法一：Android Studio（本机开发）

1. 打开 Android Studio
2. `File → Open` 选择本项目根目录（含 `settings.gradle` 的目录）
3. 等待 Gradle Sync 完成（自动下载依赖）
4. `Build → Build Bundle(s) / APK(s) → Build APK(s)`
5. APK 输出路径：`app/build/outputs/apk/debug/app-debug.apk`

### 方法二：命令行（需已安装 Android SDK）

```bash
# macOS / Linux
export ANDROID_HOME=/path/to/your/android-sdk
./gradlew assembleDebug

# Windows
set ANDROID_HOME=C:\Users\你的用户名\AppData\Local\Android\Sdk
gradlew.bat assembleDebug
```

### 方法三：Termux（在 Android 上直接构建）

```bash
pkg install openjdk-17
# 配置好 ANDROID_HOME 后：
./gradlew assembleDebug
```

以上命令生成的 Debug APK 只用于开发测试。正式发布必须使用长期保存的独立签名密钥：

```bash
export ANDROID_KEYSTORE_PATH=/absolute/path/to/release.jks
export ANDROID_KEYSTORE_PASSWORD='your-store-password'
export ANDROID_KEY_ALIAS='your-key-alias'
export ANDROID_KEY_PASSWORD='your-key-password'

./gradlew :app:lintRelease :app:assembleRelease
```

签名后的产物为 `app/build/outputs/apk/release/app-release.apk`。不要发布 `app-debug.apk`，也不要使用 Android Debug 证书签名 Release 产物。

---

## 安装

```bash
# 通过 adb 安装
adb install app/build/outputs/apk/debug/app-debug.apk

# 或直接拷贝到设备安装
```

## 使用

1. 安装 APK 后打开 App，授予 Root 权限
2. 下拉通知栏 → 展开快捷设置 → 点击编辑
3. 找到**「温控欺骗」**并拖入面板
4. 点击 Tile 即可开关

---

## 兼容性

- Android 8.0+（API 26+）
- 需要 Root（Magisk / KernelSU / APatch）
- 需已安装 AAaTempSpoof 模块
