/**
 * @file Crypto.h
 * @brief AES-256-GCM 加密解密工具类
 * @author zch
 * @date 2026-05-08
 */

#ifndef CRYPTO_H__
#define CRYPTO_H__

#include <QString>
#include <vector>

#include "../log/log.h"

/**
 * @brief 加密解密工具类（基于 OpenSSL EVP API）
 *
 * @details 使用 AES-256-GCM 认证加密，密钥从外部文件加载。
 *          加密后的格式：Base64(IV 12字节 + 密文 + GCM Tag 16字节)
 *          线程安全：所有方法均为静态方法，内部无共享状态。
 */
class Crypto {
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
