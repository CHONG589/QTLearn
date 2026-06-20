/**
 * @file AppPaths.h
 * @brief 项目统一路径与加密常量定义
 * @author zch
 * @date 2026-06-20
 *
 * @details 所有文件路径和加密常量的唯一数据源，主程序和 crypto_tool 共用。
 *          路径均为相对于项目根目录的相对路径。
 *          运行时工作目录必须为项目根目录（VS 调试时自动设置）。
 */

#pragma once

namespace AppPaths {

// ==================== 加密常量 ====================

constexpr int AES_KEY_SIZE = 32;   ///< AES-256 密钥长度（字节）
constexpr int GCM_IV_SIZE  = 12;   ///< GCM 推荐 IV 长度（字节）
constexpr int GCM_TAG_SIZE = 16;   ///< GCM 认证标签长度（字节）

// ==================== 项目文件路径 ====================

/// YAML 配置目录，包含 db_config.yml、logs.yml 等
constexpr const char* CONFIG_DIR = "config";

/// AES-256 密钥文件（64 位十六进制），由 crypto_tool --genkey 生成
constexpr const char* KEY_FILE = "config/db.key";

/// 数据库配置文件，包含加密后的 user/pwd 字段
constexpr const char* DB_CONFIG = "config/db_config.yml";

/// 日志配置文件
constexpr const char* LOGS_CONFIG = "config/logs.yml";

// ==================== 启动引导说明 ====================

/**
 * @brief 首次运行前的密钥生成步骤
 *
 * 1. 在项目根目录运行：crypto_tool\x64\Debug\crypto_tool.exe --genkey
 *    这会生成 config/db.key（已加入 .gitignore，每个开发者独立生成）
 *
 * 2. 生成加密凭证：crypto_tool\x64\Debug\crypto_tool.exe --encrypt
 *    将输出的 user: / pwd: 值填入 config/db_config.yml
 *
 * 3. 参考 config/db_config_example.yml 补充其他数据库字段
 *
 * 所有路径均以此为唯一定义源，若调整目录结构只需修改此文件。
 */

} // namespace AppPaths
