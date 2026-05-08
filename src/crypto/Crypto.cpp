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
