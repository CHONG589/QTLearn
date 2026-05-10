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
#include <direct.h>

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
static const char KEY_FILE[]   = "../config/db.key";

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

std::string current_working_directory()
{
    char buff[250];
    _getcwd(buff, 250); 
    std::string current_working_directory(buff);
    return current_working_directory;
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
static std::string encrypt(const std::string &plaintext, const std::vector<unsigned char> &key)
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
    SetConsoleOutputCP(CP_UTF8);
    std::cout << "当前所在目录为：" << current_working_directory() << std::endl;

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
