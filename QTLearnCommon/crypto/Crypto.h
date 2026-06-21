/**
 * @file Crypto.h
 * @brief AES-256-GCM 加密解密工具类
 * @author zch
 * @date 2026-05-08
 */

#ifndef CRYPTO_H__
#define CRYPTO_H__

#include <string>
#include <vector>
#include <stdexcept>

#include <openssl/evp.h>

#include "../common/AppPaths.h"

/**
 * @brief EVP_CIPHER_CTX RAII 包装类
 * @details 构造时创建上下文，析构时自动释放。
 *          不可拷贝，确保资源唯一所有权。
 */
class EvpCipherCtx {
public:
    EvpCipherCtx()
        : m_ctx(EVP_CIPHER_CTX_new()) {
        if (!m_ctx) {
            throw std::runtime_error("EVP_CIPHER_CTX_new failed");
        }
    }

    ~EvpCipherCtx() {
        if (m_ctx) {
            EVP_CIPHER_CTX_free(m_ctx);
        }
    }

    EVP_CIPHER_CTX *get() { return m_ctx; }

    EvpCipherCtx(const EvpCipherCtx &) = delete;
    EvpCipherCtx &operator=(const EvpCipherCtx &) = delete;

private:
    EVP_CIPHER_CTX *m_ctx = nullptr;
};

/**
 * @brief 加密解密工具类（基于 OpenSSL EVP API）
 *
 * @details 使用 AES-256-GCM 认证加密，密钥从外部文件加载。
 *          加密后的格式：Base64(IV 12字节 + 密文 + GCM Tag 16字节)
 *          线程安全：所有方法均为静态方法，内部无共享状态。
 *          encrypt() / decrypt() 失败时抛出 std::runtime_error。
 *          接口使用 std::string，不依赖 Qt。
 */
class Crypto {
public:
    /**
     * @brief 生成随机 AES-256 密钥并以 hex 格式写入文件
     * @param[in] keyPath 密钥文件路径
     * @return true 生成成功
     */
    static bool generateKey(const std::string &keyPath);

    /**
     * @brief 从文件加载 AES-256 密钥
     * @param[in] keyPath 密钥文件路径，文件内容为 64 个十六进制字符
     * @return true 加载成功
     */
    static bool loadKey(const std::string &keyPath);

    /**
     * @brief AES-256-GCM 加密
     * @param[in] plaintext 明文字符串
     * @return Base64 编码的密文（含 IV、密文、GCM 标签）
     * @exception std::runtime_error 密钥未加载或加密失败
     */
    static std::string encrypt(const std::string &plaintext);

    /**
     * @brief AES-256-GCM 解密
     * @param[in] b64Cipher Base64 编码的密文
     * @return 解密后的明文
     * @exception std::runtime_error 密钥未加载、格式错误或解密失败
     */
    static std::string decrypt(const std::string &b64Cipher);

private:
    static std::vector<unsigned char> s_key;  /// 已加载的密钥
    static bool s_loaded;                     /// 密钥是否已加载
};

#endif // CRYPTO_H__
