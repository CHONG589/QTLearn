# OpenSSL 加密数据库凭证设计方案

## 背景

数据库连接配置（`db_config.yml`）中用户名和密码以明文存储，会被 git 提交到仓库，存在凭证泄露风险。目标是使用 OpenSSL 对敏感字段加密，让仓库中只存密文，密钥不入库。

## 整体架构

```
启动时（自动，无交互）              一次性配置（--genkey / --encrypt）
─────────────────────────          ──────────────────────────
config/db.key ──→ 加载密钥         config/db.key ←── --genkey 生成
                        │          YAML中密文    ←── --encrypt 交互式加密
db_config.yml ──→ Config 加载
                        │
                        ↓
             DBPool::createConnection()
                  │
                  ├── Crypto::decrypt(user密文) → 明文用户名
                  └── Crypto::decrypt(pwd密文)  → 明文密码
```

## 第一步：获取 OpenSSL 库文件

### 方式一：使用预编译包（推荐）

从 [slproweb.com](https://slproweb.com/products/Win32OpenSSL.html) 下载 **Win64 OpenSSL v3.x** 完整版（不要选 "Light" 版本，Light 版不含 .lib）：

- 文件名类似 `Win64OpenSSL-3.x.x.exe`
- 安装后实际目录结构（以 `E:\Code\env_build\OpenSSL-Win64` 为例）：

```
E:\Code\env_build\OpenSSL-Win64\
├── include\openssl\               ← 头文件（几十个 .h）
├── bin\
│   ├── libcrypto-3-x64.dll        ← 运行时 DLL
│   └── libssl-3-x64.dll
├── libcrypto-3-x64.dll            ← DLL 在根目录也有一份
├── libssl-3-x64.dll
└── lib\
    └── VC\
        └── x64\
            ├── MD\                ← /MD  动态CRT Release → 项目用
            │   ├── libcrypto.lib
            │   ├── libssl.lib
            │   └── ...
            ├── MDd\               ← /MDd 动态CRT Debug  → 项目用
            │   ├── libcrypto.lib
            │   └── libssl.lib
            ├── MT\                ← /MT  静态CRT Release（不需要）
            └── MTd\               ← /MTd 静态CRT Debug （不需要）
```

**CRT 链接方式说明**：VS 项目默认 `Debug|x64` 用 `/MDd`，`Release|x64` 用 `/MD`（配置在 vcxproj 的 `UseDebugLibraries` 和 Qt 相关设置中）。必须选择匹配的 `.lib`，否则链接报错。

### 提取到项目

将安装目录中的文件复制到项目对应位置：

```bash
# 在项目根目录下执行，SRC 替换为你的实际 OpenSSL 安装路径
SRC="E:/Code/env_build/OpenSSL-Win64"

# 1. 头文件：整个 openssl 目录拷贝
cp -r "$SRC/include/openssl" include/

# 2. Release 导入库（/MD）
cp "$SRC/lib/VC/x64/MD/libcrypto.lib" lib/Release/
cp "$SRC/lib/VC/x64/MD/libssl.lib"     lib/Release/

# 3. Debug 导入库（/MDd）
cp "$SRC/lib/VC/x64/MDd/libcrypto.lib" lib/Debug/
cp "$SRC/lib/VC/x64/MDd/libssl.lib"     lib/Debug/

# 4. 运行时 DLL（根目录或 bin/ 下都有）
cp "$SRC/libcrypto-3-x64.dll" lib/Release/
cp "$SRC/libssl-3-x64.dll"     lib/Release/
cp "$SRC/libcrypto-3-x64.dll" lib/Debug/
cp "$SRC/libssl-3-x64.dll"     lib/Debug/
```

> **注意**：`libcrypto-3-x64.dll` 约 4MB，`libssl-3-x64.dll` 约 0.5MB。这些 DLL 需要提交到仓库（放在 `lib/` 下，不在 `.gitignore` 范围内）。

### 方式二：从源码编译（git clone）

如果你不想用 exe 安装包，也可以从 GitHub 源码编译。需要先装两个工具：

- **Strawberry Perl**：https://strawberryperl.com/ （Windows 上的 Perl 环境）
- **NASM**：https://www.nasm.us/pub/nasm/releasebuilds/ （汇编器，OpenSSL 汇编优化需要）

确保两者都在 `PATH` 中，然后在 **VS Developer Command Prompt** 中执行：

```bash
git clone https://github.com/openssl/openssl.git
cd openssl

# 配置：x64 动态链接，输出到指定目录
perl Configure VC-WIN64A --prefix=E:/Code/env_build/OpenSSL-Win64

# 编译与安装（耗时 10~20 分钟）
nmake
nmake install
```

编译产物的目录结构和 exe 安装版**完全一样**（`lib/VC/x64/MD*/` 布局），你的 `.lib` 在 `E:/Code/env_build/OpenSSL-Win64/lib/VC/x64/MD/libcrypto.lib` 和 `MDd/libcrypto.lib`。

> `VC-WIN64A` 中的 **A** 表示 x64（AMD64），不要用 `VC-WIN64`（带不带 A 差别很大）。

### 头文件清单（参考）

复制后 `include/openssl/` 目录应包含这些关键头文件：

```
include/openssl/
├── evp.h              ← AES 加解密入口
├── rand.h             ← 随机数生成
├── err.h              ← 错误信息
├── aes.h              ← AES 底层（EVP 内部会用到）
├── bn.h               ← 大数运算
├── crypto.h           ← 通用定义
├── ssl.h
├── pem.h
├── bio.h
├── ...
```

---

## 项目文件结构

```
QTLearn/
├── include/openssl/            ← OpenSSL 头文件（入仓库）
├── lib/
│   ├── Debug/
│   │   ├── yaml-cppd.dll
│   │   ├── yaml-cppd.lib
│   │   ├── libcrypto-3-x64.dll    ← OpenSSL DLL
│   │   ├── libcrypto.lib          ← OpenSSL 导入库 (/MDd)
│   │   ├── libssl-3-x64.dll
│   │   └── libssl.lib             ← OpenSSL 导入库 (/MDd)
│   └── Release/
│       ├── yaml-cpp.dll
│       ├── yaml-cpp.lib
│       ├── libcrypto-3-x64.dll
│       ├── libcrypto.lib          ← OpenSSL 导入库 (/MD)
│       ├── libssl-3-x64.dll
│       └── libssl.lib             ← OpenSSL 导入库 (/MD)
├── src/crypto/
│   ├── Crypto.h                ← 解密模块头文件（入仓库）
│   └── Crypto.cpp              ← 解密模块实现（入仓库）
├── tools/
│   └── crypto_tool.cpp         ← 独立加密工具（入仓库）
├── config/
│   ├── db_config.example.yml   ← 模板文件（入仓库，占位符）
│   ├── db_config.yml           ← 真实配置（gitignore，不入库）
│   └── db.key                  ← 本地密钥（gitignore，不入库）
└── doc/
    └── openssl_crypto_design.md
```

### .gitignore 追加

```gitignore
# 本地密钥和真实配置（勿入库）
config/db.key
config/db_config.yml
```

---

## 加密算法

| 决策点 | 选择 | 原因 |
|--------|------|------|
| 算法 | AES-256-GCM | 认证加密，同时对密文做完整性校验，防篡改 |
| API 层级 | EVP（OpenSSL 高层 API） | 比直接调 `AES_*` 更安全，自动处理边界、长度等细节 |
| IV 长度 | 12 字节 | GCM 推荐值，每次加密随机生成 |
| 标签长度 | 16 字节 | GCM 最大标签，防止伪造密文 |
| 输出编码 | Base64 | 密文含二进制字节，Base64 编码后才可存入 YAML |

### 密文二进制结构

```
┌──────────────┬──────────────────┬──────────────┐
│  IV (12字节)  │   密文 (变长)     │  Tag (16字节) │
└──────────────┴──────────────────┴──────────────┘
                     ↓ Base64 编码 ↓
           YAML 中存储的字符串，如 "A8jF3kL9xZ..."
```

---

## 密钥分发的核心问题（重要）

> **问**：`db.key` 在 `.gitignore` 中不入库，别人 clone 项目没有这个文件怎么解密？

### 方案：模板 + 各自本地密钥

不设默认密钥，每个开发者自己生成自己的 `db.key`，用自己的密钥加密自己的数据库凭证。

**核心认知**：每个开发者连的是自己本机的数据库，本来就不应该共享凭证。clone 项目的人应该配置自己的数据库连接，而不是用别人的密码。

### db_config.example.yml（模板，入仓库）

```yaml
database:
  driver: QMYSQL
  ip: 127.0.0.1
  port: 3306
  user: <运行 crypto_tool --encrypt 生成加密用户名后替换此行>
  pwd:  <运行 crypto_tool --encrypt 生成加密密码后替换此行>
  db: learn
  minsize: 5
  maxsize: 20
  timeout: 3000
```

---

## 新成员初始化流程

```bash
# 1. 从模板复制
copy config\db_config.example.yml config\db_config.yml

# 2. 生成自己的随机密钥
crypto_tool --genkey
# → 密钥已生成: config/db.key

# 3. 加密自己的数据库用户名和密码（输入时不回显）
crypto_tool --encrypt
# → 请输入用户名: ******
# → 请输入密码:   ******
# → user: A8jF3kL9...
# → pwd:  Z9xQ7mW...

# 4. 将输出的密文复制到 config/db_config.yml 对应字段

# 5. 正常运行 QT_Learn，无需任何交互
```

---

## 正常运行时流程（无交互）

```
main.cpp 启动
    │
    ├─ Crypto::loadKey("config/db.key")
    │     └─ 文件不存在？ → 报错退出："未找到密钥文件，请先运行 crypto_tool --genkey"
    │
    ├─ Config::loadFromConfDir("config")
    │     └─ 加载 YAML，密文字段以普通字符串存入 ConfigVar
    │
    └─ DBPool::createConnection()
          ├─ g_db_user->getValue() → 密文字符串 → Crypto::decrypt() → 明文用户名
          └─ g_db_pwd->getValue()  → 密文字符串 → Crypto::decrypt() → 明文密码
```

---

## 代码实现

### 1. Crypto.h — 解密模块头文件

```cpp
/**
 * @file Crypto.h
 * @brief AES-256-GCM 加密解密工具类
 * @author zch
 * @date 2026-05-06
 */

#ifndef CRYPTO_H__
#define CRYPTO_H__

#include <QString>
#include <vector>

/**
 * @brief 加密解密工具类（基于 OpenSSL EVP API）
 *
 * @details 使用 AES-256-GCM 认证加密，密钥从外部文件加载。
 *          加密后的格式：Base64(IV 12字节 + 密文 + GCM Tag 16字节)
 *          线程安全：所有方法均为静态方法，内部无共享状态。
 */
class Crypto
{
public:
    /**
     * @brief 从文件加载 AES-256 密钥
     * @param[in] keyPath 密钥文件路径，文件内容为 64 个十六进制字符
     * @return true 加载成功
     */
    static bool loadKey(const QString &keyPath);

    /**
     * @brief AES-256-GCM 加密
     * @param[in] plaintext 明文字符串
     * @return Base64 编码的密文（含 IV、密文、GCM 标签）
     */
    static QString encrypt(const QString &plaintext);

    /**
     * @brief AES-256-GCM 解密
     * @param[in] b64Cipher Base64 编码的密文
     * @return 解密后的明文
     */
    static QString decrypt(const QString &b64Cipher);

private:
    static const int AES_KEY_SIZE = 32;   // AES-256 密钥长度（字节）
    static const int GCM_IV_SIZE  = 12;   // GCM 推荐 IV 长度
    static const int GCM_TAG_SIZE = 16;   // GCM 认证标签长度

    static std::vector<unsigned char> s_key;  /// 已加载的密钥
    static bool s_loaded;                     /// 密钥是否已加载
};

#endif // CRYPTO_H__
```

### 2. Crypto.cpp — 解密模块实现

```cpp
/**
 * @file Crypto.cpp
 * @brief AES-256-GCM 加密解密工具类实现
 * @author zch
 * @date 2026-05-06
 */

#include "Crypto.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>

#include <QFile>
#include <QTextStream>

#include "../log/QLog.h"

// 静态成员初始化
std::vector<unsigned char> Crypto::s_key;
bool Crypto::s_loaded = false;

// ============================================================
// 辅助：十六进制编解码
// ============================================================

static std::string bin2hex(const unsigned char *data, int len)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out(len * 2, '\0');
    for (int i = 0; i < len; ++i) {
        out[i * 2]     = hex[data[i] >> 4];
        out[i * 2 + 1] = hex[data[i] & 0x0F];
    }
    return out;
}

static std::vector<unsigned char> hex2bin(const char *hex, int len)
{
    // len 是 hex 字符串的字符数，结果是 len/2 字节
    std::vector<unsigned char> out(len / 2);
    for (int i = 0; i < len; i += 2) {
        char buf[] = {hex[i], hex[i + 1], '\0'};
        out[i / 2] = static_cast<unsigned char>(std::stoi(buf, nullptr, 16));
    }
    return out;
}

// ============================================================
// 密钥管理
// ============================================================

/**
 * @brief 从文件加载密钥
 *
 * @details 读取 hex 格式的密钥文件（64 个字符 → 32 字节），
 *          文件不存在或格式错误时返回 false。
 *
 * @param[in] keyPath 密钥文件路径
 * @return true 加载成功
 */
bool Crypto::loadKey(const QString &keyPath)
{
    QFile file(keyPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LOG_ERROR() << "Crypto::loadKey: cannot open key file" << keyPath;
        return false;
    }

    QTextStream in(&file);
    QString hex = in.readAll().trimmed();
    file.close();

    if (hex.size() != AES_KEY_SIZE * 2) {
        LOG_ERROR() << "Crypto::loadKey: key file must contain 64 hex chars, got"
                     << hex.size();
        return false;
    }

    QByteArray hexBytes = hex.toLatin1();
    s_key   = hex2bin(hexBytes.constData(), hexBytes.size());
    s_loaded = true;

    LOG_INFO() << "Crypto::loadKey: key loaded from" << keyPath;
    return true;
}

// ============================================================
// AES-256-GCM 加密
// ============================================================

/**
 * @brief 加密明文字符串
 *
 * @details 流程：
 *          1. 检查密钥是否已加载
 *          2. 生成 12 字节随机 IV
 *          3. EVP_EncryptInit_ex 初始化 AES-256-GCM
 *          4. EVP_EncryptUpdate 加密数据
 *          5. EVP_EncryptFinal_ex 结束加密
 *          6. EVP_CIPHER_CTX_ctrl 获取 GCM 认证标签
 *          7. 组装 IV + 密文 + Tag，Base64 编码返回
 *
 * @param[in] plaintext 明文字符串
 * @return Base64 密文
 */
QString Crypto::encrypt(const QString &plaintext)
{
    if (!s_loaded) {
        LOG_ERROR() << "Crypto::encrypt: key not loaded";
        return QString();
    }

    QByteArray plainBytes = plaintext.toUtf8();

    // 1. 生成随机 IV
    unsigned char iv[GCM_IV_SIZE];
    if (!RAND_bytes(iv, GCM_IV_SIZE)) {
        LOG_ERROR() << "Crypto::encrypt: failed to generate IV";
        return QString();
    }

    // 2. 创建加密上下文
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        LOG_ERROR() << "Crypto::encrypt: failed to create cipher ctx";
        return QString();
    }

    // 3. 初始化加密
    if (!EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                            s_key.data(), iv)) {
        EVP_CIPHER_CTX_free(ctx);
        LOG_ERROR() << "Crypto::encrypt: init failed";
        return QString();
    }

    // 4. 加密数据
    std::vector<unsigned char> cipher(plainBytes.size() + 16);
    int len = 0;
    if (!EVP_EncryptUpdate(ctx, cipher.data(), &len,
                           reinterpret_cast<const unsigned char *>(plainBytes.constData()),
                           plainBytes.size())) {
        EVP_CIPHER_CTX_free(ctx);
        LOG_ERROR() << "Crypto::encrypt: update failed";
        return QString();
    }
    int cipherLen = len;

    // 5. 结束加密
    if (!EVP_EncryptFinal_ex(ctx, cipher.data() + len, &len)) {
        EVP_CIPHER_CTX_free(ctx);
        LOG_ERROR() << "Crypto::encrypt: final failed";
        return QString();
    }
    cipherLen += len;

    // 6. 获取 GCM 标签
    unsigned char tag[GCM_TAG_SIZE];
    if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, GCM_TAG_SIZE, tag)) {
        EVP_CIPHER_CTX_free(ctx);
        LOG_ERROR() << "Crypto::encrypt: get tag failed";
        return QString();
    }

    EVP_CIPHER_CTX_free(ctx);

    // 7. 组装：IV(12) + 密文 + Tag(16)
    std::vector<unsigned char> combined;
    combined.insert(combined.end(), iv, iv + GCM_IV_SIZE);
    combined.insert(combined.end(), cipher.begin(), cipher.begin() + cipherLen);
    combined.insert(combined.end(), tag, tag + GCM_TAG_SIZE);

    // 8. Base64 编码
    int b64Len = ((combined.size() + 2) / 3) * 4 + 1;
    std::vector<char> b64(b64Len);
    int b64Actual = EVP_EncodeBlock(
        reinterpret_cast<unsigned char *>(b64.data()),
        combined.data(),
        static_cast<int>(combined.size()));
    b64.resize(b64Actual);

    return QString::fromLatin1(b64.data(), b64Actual);
}

// ============================================================
// AES-256-GCM 解密
// ============================================================

/**
 * @brief 解密密文字符串
 *
 * @details 流程：
 *          1. Base64 解码
 *          2. 拆解 IV(前12字节) + 密文(中间) + Tag(最后16字节)
 *          3. EVP_DecryptInit_ex 初始化解密
 *          4. EVP_DecryptUpdate 解密数据
 *          5. EVP_CIPHER_CTX_ctrl 设置 GCM 标签
 *          6. EVP_DecryptFinal_ex 验证标签并结束解密
 *          7. 返回明文
 *
 * @param[in] b64Cipher Base64 编码的密文
 * @return 解密后的明文，失败返回空字符串
 */
QString Crypto::decrypt(const QString &b64Cipher)
{
    if (!s_loaded) {
        LOG_ERROR() << "Crypto::decrypt: key not loaded";
        return QString();
    }

    QByteArray b64Bytes = b64Cipher.toLatin1();
    int b64Len = b64Bytes.size();

    // 1. Base64 解码
    std::vector<unsigned char> combined(b64Len);
    int combinedLen = EVP_DecodeBlock(
        combined.data(),
        reinterpret_cast<const unsigned char *>(b64Bytes.constData()),
        b64Len);

    // EVP_DecodeBlock 在末尾有 = 填充时会多算 1 字节，需要修正
    // 检查最后几个字节是否为零（Base64 填充不会产生零字节）
    while (combinedLen > 0 && combined[combinedLen - 1] == 0) {
        combinedLen--;
    }

    // 2. 拆解：IV(12) + 密文 + Tag(16)
    if (combinedLen < GCM_IV_SIZE + GCM_TAG_SIZE + 1) {
        LOG_ERROR() << "Crypto::decrypt: ciphertext too short, len=" << combinedLen;
        return QString();
    }

    const unsigned char *iv     = combined.data();
    const unsigned char *cipher = combined.data() + GCM_IV_SIZE;
    int cipherLen               = combinedLen - GCM_IV_SIZE - GCM_TAG_SIZE;
    const unsigned char *tag    = combined.data() + GCM_IV_SIZE + cipherLen;

    // 3. 创建解密上下文
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        LOG_ERROR() << "Crypto::decrypt: failed to create cipher ctx";
        return QString();
    }

    // 4. 初始化解密
    if (!EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                            s_key.data(), iv)) {
        EVP_CIPHER_CTX_free(ctx);
        LOG_ERROR() << "Crypto::decrypt: init failed";
        return QString();
    }

    // 5. 解密数据
    std::vector<unsigned char> plain(cipherLen + 16);
    int len = 0;
    if (!EVP_DecryptUpdate(ctx, plain.data(), &len, cipher, cipherLen)) {
        EVP_CIPHER_CTX_free(ctx);
        LOG_ERROR() << "Crypto::decrypt: update failed";
        return QString();
    }
    int plainLen = len;

    // 6. 设置 GCM 标签（用于验证完整性）
    if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, GCM_TAG_SIZE,
                             const_cast<unsigned char *>(tag))) {
        EVP_CIPHER_CTX_free(ctx);
        LOG_ERROR() << "Crypto::decrypt: set tag failed";
        return QString();
    }

    // 7. 验证 Tag 并结束解密
    if (!EVP_DecryptFinal_ex(ctx, plain.data() + len, &len)) {
        EVP_CIPHER_CTX_free(ctx);
        LOG_ERROR() << "Crypto::decrypt: tag verification failed (tampered?)";
        return QString();
    }
    plainLen += len;

    EVP_CIPHER_CTX_free(ctx);

    return QString::fromUtf8(
        reinterpret_cast<const char *>(plain.data()), plainLen);
}
```

### 3. crypto_tool.cpp — 独立加密工具

`tools/crypto_tool.cpp` 是一个不依赖 Qt 的独立命令行工具，用纯 C++ + Windows API + OpenSSL 编写。

```cpp
/**
 * @file crypto_tool.cpp
 * @brief 独立加密工具 —— 生成密钥 / 交互式加密
 * @author zch
 * @date 2026-05-06
 *
 * 用法：
 *   crypto_tool --genkey          生成本地密钥文件 config/db.key
 *   crypto_tool --encrypt         从 config/db.key 加载密钥，交互式输入明文，输出 Base64 密文
 *
 * 编译（在 VS Developer Command Prompt 中执行，从项目根目录）：
 *   cl /EHsc /std:c++17 /Fe:tools\crypto_tool.exe tools\crypto_tool.cpp ^
 *      /I include lib\Release\libcrypto.lib
 *
 * 注意：编译产物 tools\crypto_tool.exe 运行时依赖 libcrypto-3-x64.dll，
 * 需要把 lib\Release\libcrypto-3-x64.dll 复制到 tools\ 或系统路径中，
 * 也可以在编译后直接在 lib\Release 目录下运行。
 */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <stdexcept>

#include <windows.h>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>

// ============================================================
// 常量
// ============================================================

static const int  AES_KEY_SIZE = 32;   // AES-256 密钥长度（字节）
static const int  GCM_IV_SIZE  = 12;   // GCM 推荐 IV 长度
static const int  GCM_TAG_SIZE = 16;   // GCM 认证标签长度
static const char KEY_FILE[]   = "config/db.key";

// ============================================================
// 辅助函数：十六进制编解码
// ============================================================

static std::string bin2hex(const unsigned char *data, int len)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out(len * 2, '\0');
    for (int i = 0; i < len; ++i) {
        out[i * 2]     = hex[data[i] >> 4];
        out[i * 2 + 1] = hex[data[i] & 0x0F];
    }
    return out;
}

static std::vector<unsigned char> hex2bin(const std::string &hex)
{
    std::vector<unsigned char> out(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        out[i / 2] = static_cast<unsigned char>(
            std::stoi(hex.substr(i, 2), nullptr, 16));
    }
    return out;
}

// ============================================================
// 辅助函数：交互式输入（不回显）
// ============================================================

/**
 * @brief 从控制台读取密码，屏幕显示 * 号而非实际字符
 *
 * @details 实现流程：
 *          1. 获取标准输入句柄
 *          2. 关闭 ENABLE_ECHO_INPUT 和 ENABLE_LINE_INPUT（禁止回显和行缓冲）
 *          3. 循环 ReadConsoleW 逐宽字符读取
 *          4. 遇到退格键(0x08/0x7F)时删除缓冲区最后一个字符
 *          5. 遇到回车时结束输入
 *          6. 恢复原始控制台模式
 *
 * @param[in] prompt 提示文字
 * @return 用户输入的明文字符串
 */
static std::string readPassword(const char *prompt)
{
    std::cout << prompt << std::flush;

    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD oldMode;
    GetConsoleMode(hStdin, &oldMode);

    // 关闭回显和行缓冲
    SetConsoleMode(hStdin, oldMode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT));

    std::string input;
    wchar_t wch;
    DWORD   read;
    while (ReadConsoleW(hStdin, &wch, 1, &read, nullptr)
           && wch != L'\r' && wch != L'\n') {
        // 处理退格键
        if (wch == L'\b' || wch == 0x7F) {
            if (!input.empty()) {
                input.pop_back();
                std::cout << "\b \b" << std::flush;
            }
        } else {
            char ch = static_cast<char>(wch);
            input.push_back(ch);
            std::cout << '*' << std::flush;
        }
    }

    // 恢复原始控制台模式
    SetConsoleMode(hStdin, oldMode);
    std::cout << std::endl;
    return input;
}

// ============================================================
// 密钥管理
// ============================================================

/**
 * @brief 生成随机 AES-256 密钥，以 hex 格式写入文件
 *
 * @details 使用 OpenSSL RAND_bytes 生成密码学安全的随机密钥。
 *          每个开发者独立生成自己的密钥，不与其他人共享。
 */
static void generateKey()
{
    unsigned char key[AES_KEY_SIZE];
    if (!RAND_bytes(key, AES_KEY_SIZE)) {
        throw std::runtime_error("生成随机密钥失败");
    }

    std::ofstream ofs(KEY_FILE);
    if (!ofs) {
        throw std::runtime_error("无法创建密钥文件: " + std::string(KEY_FILE));
    }
    ofs << bin2hex(key, AES_KEY_SIZE);
    ofs.close();

    std::cout << "密钥已生成: " << KEY_FILE << std::endl;
}

/**
 * @brief 从文件加载密钥
 *
 * @details 读取 hex 格式的密钥文件（64 个十六进制字符），转为 32 字节二进制。
 *
 * @return 32 字节的密钥
 */
static std::vector<unsigned char> loadKey()
{
    std::ifstream ifs(KEY_FILE);
    if (!ifs) {
        throw std::runtime_error(
            "未找到密钥文件 " + std::string(KEY_FILE) +
            "\n请先运行: crypto_tool --genkey");
    }
    std::string hex;
    ifs >> hex;
    ifs.close();

    if (hex.size() != AES_KEY_SIZE * 2) {
        throw std::runtime_error("密钥文件格式错误：应为64个十六进制字符");
    }
    return hex2bin(hex);
}

// ============================================================
// AES-256-GCM 加密
// ============================================================

/**
 * @brief AES-256-GCM 加密，返回 Base64 密文
 *
 * @details 加密流程：
 *          1. 生成 12 字节随机 IV
 *          2. 创建 EVP 加密上下文，初始化 AES-256-GCM
 *          3. EVP_EncryptUpdate 加密数据
 *          4. EVP_EncryptFinal_ex 结束加密
 *          5. 获取 16 字节 GCM 认证标签
 *          6. 组装 IV(12) + 密文 + Tag(16)，Base64 编码返回
 *
 * @param[in] plaintext 明文字符串
 * @param[in] key       32 字节 AES 密钥
 * @return Base64 编码的密文字符串
 */
static std::string encrypt(const std::string &plaintext,
                           const std::vector<unsigned char> &key)
{
    // 1. 生成随机 IV
    unsigned char iv[GCM_IV_SIZE];
    if (!RAND_bytes(iv, GCM_IV_SIZE)) {
        throw std::runtime_error("生成 IV 失败");
    }

    // 2. 创建 EVP 加密上下文
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("创建加密上下文失败");
    }

    int len = 0;
    std::vector<unsigned char> cipher(plaintext.size() + 16);

    // 3. 初始化加密
    if (!EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key.data(), iv)) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("加密初始化失败");
    }

    // 4. 加密数据
    if (!EVP_EncryptUpdate(ctx, cipher.data(), &len,
                           reinterpret_cast<const unsigned char *>(plaintext.data()),
                           static_cast<int>(plaintext.size()))) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("加密数据失败");
    }
    int cipherLen = len;

    // 5. 结束加密
    if (!EVP_EncryptFinal_ex(ctx, cipher.data() + len, &len)) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("加密收尾失败");
    }
    cipherLen += len;

    // 6. 获取 GCM 认证标签
    unsigned char tag[GCM_TAG_SIZE];
    if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, GCM_TAG_SIZE, tag)) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("获取 GCM 标签失败");
    }

    EVP_CIPHER_CTX_free(ctx);

    // 7. 组装: IV(12) + 密文 + Tag(16)
    std::vector<unsigned char> combined;
    combined.insert(combined.end(), iv, iv + GCM_IV_SIZE);
    combined.insert(combined.end(), cipher.begin(), cipher.begin() + cipherLen);
    combined.insert(combined.end(), tag, tag + GCM_TAG_SIZE);

    // 8. Base64 编码
    int b64Len = ((combined.size() + 2) / 3) * 4 + 1;
    std::vector<char> b64(b64Len);
    int b64Actual = EVP_EncodeBlock(
        reinterpret_cast<unsigned char *>(b64.data()),
        combined.data(),
        static_cast<int>(combined.size()));
    b64.resize(b64Actual);

    return std::string(b64.data(), b64.size());
}

// ============================================================
// 入口
// ============================================================

/**
 * @brief 主函数
 *
 * @details 用法：
 *          crypto_tool --genkey   生成随机密钥文件
 *          crypto_tool --encrypt  交互式加密，输出 Base64 密文
 */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        std::cerr << "用法:\n"
                  << "  crypto_tool --genkey   生成本地密钥文件 config/db.key\n"
                  << "  crypto_tool --encrypt  从 config/db.key 加载密钥，交互式加密文本\n";
        return 1;
    }

    try {
        if (std::strcmp(argv[1], "--genkey") == 0) {
            generateKey();
        } else if (std::strcmp(argv[1], "--encrypt") == 0) {
            auto key = loadKey();

            std::string username = readPassword("请输入用户名: ");
            std::string password = readPassword("请输入密码:   ");

            std::cout << "\n--- 复制下面的密文到 config/db_config.yml ---\n\n";
            std::cout << "user: " << encrypt(username, key) << "\n";
            std::cout << "pwd:  " << encrypt(password, key) << "\n\n";
        } else {
            std::cerr << "未知参数: " << argv[1] << "\n";
            return 1;
        }
    } catch (const std::exception &e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
```

#### 编译 crypto_tool

在 **VS Developer Command Prompt** 中，于项目根目录执行：

```bat
cl /EHsc /std:c++17 /Fe:tools\crypto_tool.exe tools\crypto_tool.cpp ^
   /I include lib\Release\libcrypto.lib
```

编译产物 `tools\crypto_tool.exe` 运行时依赖 `libcrypto-3-x64.dll`。运行前把 DLL 复制到同目录或系统路径：

```bat
copy lib\Release\libcrypto-3-x64.dll tools\
```

---

## crypto_tool 使用参考

| 命令 | 作用 |
|------|------|
| `crypto_tool --genkey` | 生成 32 字节随机 AES-256 密钥，写入 `config/db.key`（hex 格式，64 个字符） |
| `crypto_tool --encrypt` | 从 `config/db.key` 加载密钥，交互式输入明文（输入时屏幕显示 `*`），输出 Base64 密文 |

### 关键实现细节

- **密码输入不回显**：使用 Windows `SetConsoleMode` 关闭 `ENABLE_ECHO_INPUT`，通过 `ReadConsoleW` 逐字符读取，每输入一个字符屏幕显示 `*`
- **退格键处理**：支持 Backspace 键删除已输入字符（`\b` 或 `0x7F`）
- **随机数**：IV 和密钥均使用 OpenSSL 的 `RAND_bytes()` 生成（Windows 上底层调用系统 CSPRNG）

---

## VCXPROJ 配置变更

在 `QT_Learn.vcxproj` 中追加 OpenSSL 库依赖，每个配置的 `<Link>` 块中 `<AdditionalDependencies>` 追加：

```xml
<!-- Debug (/MDd) -->
<AdditionalDependencies>
  yaml-cppd.lib;libcrypto.lib;libssl.lib;%(AdditionalDependencies)
</AdditionalDependencies>

<!-- Release (/MD) -->
<AdditionalDependencies>
  yaml-cpp.lib;libcrypto.lib;libssl.lib;%(AdditionalDependencies)
</AdditionalDependencies>
```

> **为什么 Debug 和 Release 的 .lib 名字一样？** 因为 OpenSSL 用 `lib/VC/x64/MD/` 和 `lib/VC/x64/MDd/` 两个不同目录来区分 CRT 链接方式，而不是用文件名区分。`AdditionalLibraryDirectories` 中的 `$(ProjectDir)lib\$(Configuration)` 会自动指向 `lib\Debug` 或 `lib\Release`，各目录下放对应版本的 `libcrypto.lib`/`libssl.lib` 即可。

---

## 常见疑问

### Q1：`--encrypt` 不还是需要输入明文吗？

`--encrypt` 是**一次性**的配置操作。输入明文后，程序输出密文，你把密文贴到 YAML 里，之后明文就不存在了。与 YAML 中直接存明文的关键区别：

- YAML 明文 → 永远躺在文件里 → `git commit` 推到仓库 → 所有人可见
- `--encrypt` 明文 → 仅在你内存中出现一次 → 转为密文 → 明文消失

### Q2：每次启动程序都要输入用户名密码吗？

不需要。`--genkey` 和 `--encrypt` 是一次性配置工具。配置完成后，正常启动 QT_Learn 完全自动运行，没有任何交互。

### Q3：别人没有 `db.key` 怎么解密？

别人**不需要**解密你的密文。正确做法：

1. 仓库里提交 `db_config.example.yml` 模板（占位符）
2. 新成员 clone 后，自己生成自己的 `db.key`，用自己的数据库凭证
3. 每个人连接的是自己本机的 MySQL，凭证各不相同，互不相关
