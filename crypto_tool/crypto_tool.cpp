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
 * 加密逻辑委托给 QTLearnCommon 中的 Crypto 类，本文件仅负责控制台 UI 和流程编排。
 */

#include <iostream>
#include <string>
#include <cstring>
#include <stdexcept>
#include <direct.h>

#include <windows.h>

#include "common/AppPaths.h"
#include "crypto/Crypto.h"

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

static std::string current_working_directory()
{
    char buff[250];
    _getcwd(buff, 250);
    return std::string(buff);
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
                  << "  crypto_tool --genkey   生成密钥文件 (" << AppPaths::KEY_FILE << ")\n"
                  << "  crypto_tool --encrypt  从 " << AppPaths::KEY_FILE << " 加载密钥，交互式加密文本\n";
        return 1;
    }

    try {
        if (std::strcmp(argv[1], "--genkey") == 0) {
            if (!Crypto::generateKey(AppPaths::KEY_FILE)) {
                std::cerr << "错误: 生成密钥失败" << std::endl;
                return 1;
            }
            std::cout << "密钥已生成: " << AppPaths::KEY_FILE << std::endl;
        } else if (std::strcmp(argv[1], "--encrypt") == 0) {
            if (!Crypto::loadKey(AppPaths::KEY_FILE)) {
                std::cerr << "错误: 未找到密钥文件 " << AppPaths::KEY_FILE
                          << "\n请先运行: crypto_tool --genkey" << std::endl;
                return 1;
            }

            std::string username = readPassword("请输入用户名: ");
            std::string password = readPassword("请输入密码:   ");

            std::cout << "\n--- 复制下面的密文到 " << AppPaths::DB_CONFIG << " ---\n\n";
            std::cout << "user: " << Crypto::encrypt(username) << "\n";
            std::cout << "pwd:  " << Crypto::encrypt(password) << "\n\n";
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
