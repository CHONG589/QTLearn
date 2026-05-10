#pragma once

#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>
#include <QSqlError>
#include <QDebug>
#include <exception>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <QThreadStorage>
#include <QUuid>
#include <QSqlRecord>
#include <optional>

#include "../log/QLog.h"
#include "../config/config.h"
#include "../crypto/Crypto.h"

/**
 * @brief 数据库统一异常类
 *
 * @details 继承 std::exception，用于在 DBConn / DBPool 中统一抛出。
 *          所有数据库操作失败均以此异常报告，替代传统的错误码返回。
 */
class DBException : public std::exception {
public:
    /**
     * @brief 构造异常对象
     * @param[in] msg 异常描述消息（QString，内部转为 std::string）
     */
    explicit DBException(const QString &msg) noexcept
        : m_msg(msg.toStdString()) {
    }

    /**
     * @brief 获取异常描述
     * @return const char* C 风格字符串
     */
    const char *what() const noexcept override {
        return m_msg.c_str();
    }

private:
    std::string m_msg;  /// 异常消息副本
};

/**
 * @brief 数据库连接封装（最小执行单元）
 *
 * @details 对 QSqlDatabase 的薄封装，提供：
 *          - 写操作 exec() —— 失败抛 DBException
 *          - 安全查询 queryAll / queryOne / queryRow —— 数据已读入内存
 *          - 流式查询 query() —— 大数据集逐行处理
 *          - 预处理 prepare() + execPrepared() —— 防 SQL 注入 + 批量复用
 *          - 一步式预处理便捷方法 —— 单次使用更安全
 *          - 事务 begin / commit / rollback
 *          - 连接状态检测 isValid / isAlive
 *
 * @note 不负责连接生命周期管理，连接由 DBPool 分配和回收。
 */
class DBConn {
public:
    /**
     * @brief 构造 DBConn，接管一个已打开的 QSqlDatabase 连接
     * @param[in] db 已打开的 QSqlDatabase 对象（由 DBPool 创建）
     */
    explicit DBConn(QSqlDatabase db);

    // ==================== 写操作 ====================

    /**
     * @brief 执行一条写 SQL（INSERT / UPDATE / DELETE / DDL）
     *
     * @details 内部构造 QSqlQuery 并绑定当前连接；
     *          执行失败直接抛出 DBException，调用方无需检查返回值。
     *
     * @param[in] sql 待执行的 SQL 语句
     *
     * @throws DBException 当 SQL 执行失败时抛出
     */
    void exec(const QString &sql);

    // ==================== 查询（安全：数据已读入内存） ====================

    /**
     * @brief 查询全部结果集
     *
     * @details 执行 SELECT 后将所有行读入 QList<QVariantList>，
     *          无需担心连接生命周期问题。
     *
     * @param[in] sql SELECT 查询语句
     * @return QList<QVariantList> 二维结果集，外层每元素为一行
     *
     * @throws DBException 查询执行失败时抛出
     */
    QList<QVariantList> queryAll(const QString &sql);

    /**
     * @brief 查询第一行数据
     *
     * @details 执行 SELECT 后返回结果集的第一行，
     *          无结果时返回空的 QVariantList。
     *          如需区分"无结果"与"空行"，请使用 queryRow()。
     *
     * @param[in] sql SELECT 查询语句
     * @return QVariantList 第一行数据，无结果返回空列表
     *
     * @throws DBException 查询执行失败时抛出
     */
    QVariantList queryOne(const QString &sql);

    /**
     * @brief 查询至多一行（语义更明确）
     *
     * @details 与 queryOne 逻辑相同，但通过 std::optional 区分"有数据"和"无数据"：
     *          - 有结果 → std::optional<QVariantList> 含值
     *          - 无结果 → std::nullopt
     *
     * @param[in] sql SELECT 查询语句
     * @return std::optional<QVariantList> 有数据时含值，否则 std::nullopt
     *
     * @throws DBException 查询执行失败时抛出
     */
    std::optional<QVariantList> queryRow(const QString &sql);

    /**
     * @brief 流式查询（返回 QSqlQuery 供逐行遍历）
     *
     * @details 适用于大数据集场景，调用方自行遍历 QSqlQuery。
     *          返回的 QSqlQuery 绑定当前连接，必须在 ScopedConn 生命周期内用完，
     *          否则连接归还后 QSqlQuery 变为无效。
     *
     * @param[in] sql SELECT 查询语句
     * @return QSqlQuery 已执行查询的结果对象
     *
     * @warning QSqlQuery 生命周期不能超过当前 DBConn / ScopedConn
     *
     * @throws DBException 查询执行失败时抛出
     */
    QSqlQuery query(const QString &sql);

    // ==================== 预处理 ====================

    /**
     * @brief 预处理 SQL 模板
     *
     * @details 对 SQL 模板进行预编译，占位符支持 `?`（位置绑定）或 `:name`（命名绑定）。
     *          返回的 QSqlQuery 可多次调用 execPrepared() 绑定不同参数执行，
     *          适合批量插入 / 批量更新场景。
     *
     * @param[in] sql 含占位符的 SQL 模板
     * @return QSqlQuery 已预编译的查询对象
     *
     * @throws DBException 预编译失败时抛出
     *
     * @note 典型用法：
     * @code
     * auto stmt = conn.prepare("INSERT INTO t VALUES (?, ?)");
     * conn.execPrepared(stmt, {1, "a"});
     * conn.execPrepared(stmt, {2, "b"});
     * @endcode
     */
    QSqlQuery prepare(const QString &sql);

    /**
     * @brief 为已预编译的 QSqlQuery 绑定参数并执行（两步式第二步）
     *
     * @details 依次将 args 绑定到占位符，执行写操作。
     *          与 prepare() 配合实现批量复用：一次预编译，多次绑定执行。
     *
     * @param[in,out] query 已预编译的 QSqlQuery 对象（由 prepare() 返回）
     * @param[in]     args  参数列表，按占位符顺序排列
     *
     * @throws DBException 绑定或执行失败时抛出
     */
    void execPrepared(QSqlQuery &query, const QVariantList &args);

    // ---- 一步式预处理便捷方法（单次使用）----

    /**
     * @brief 一步式预处理写操作
     *
     * @details 内部完成 prepare → bind → exec 全流程，
     *          适合仅执行一次的场景，比两步式更简洁安全。
     *
     * @param[in] sql  含占位符的 SQL 模板
     * @param[in] args 参数列表
     *
     * @throws DBException 任一阶段失败时抛出
     */
    void execPrepared(const QString &sql, const QVariantList &args);

    /**
     * @brief 一步式预处理查询全部
     *
     * @details 内部完成 prepare → bind → exec → 读取全部结果。
     *
     * @param[in] sql  含占位符的 SELECT 语句
     * @param[in] args 参数列表
     * @return QList<QVariantList> 二维结果集
     *
     * @throws DBException 任一阶段失败时抛出
     */
    QList<QVariantList> queryAllPrepared(const QString &sql, const QVariantList &args);

    /**
     * @brief 一步式预处理查询第一行
     *
     * @details 内部完成 prepare → bind → exec → 读取第一行。
     *
     * @param[in] sql  含占位符的 SELECT 语句
     * @param[in] args 参数列表
     * @return QVariantList 第一行数据，无结果返回空列表
     *
     * @throws DBException 任一阶段失败时抛出
     */
    QVariantList queryOnePrepared(const QString &sql, const QVariantList &args);

    // ==================== 事务 ====================

    /**
     * @brief 开启数据库事务
     *
     * @details 调用 QSqlDatabase::transaction() 开启事务。
     *          建议配合 DBTransaction（RAII 事务封装）使用，避免遗漏 commit/rollback。
     *
     * @throws DBException 事务开启失败时抛出
     */
    void begin();

    /**
     * @brief 提交当前事务
     *
     * @details 将事务内所有修改持久化到数据库。
     *
     * @throws DBException 提交失败时抛出
     */
    void commit();

    /**
     * @brief 回滚当前事务
     *
     * @details 撤销事务内所有未提交的修改。
     *
     * @throws DBException 回滚失败时抛出
     */
    void rollback();

    // ==================== 状态检测 ====================

    /**
     * @brief 判断连接是否有效
     * @return true 连接有效且已打开
     */
    bool isValid() const;

    /**
     * @brief 心跳检测
     *
     * @details 先检查 isValid()，再执行 SELECT 1 轻量查询，
     *          用于连接池回收前检测连接是否仍然存活。
     *
     * @return true 连接存活且可正常通信
     */
    bool isAlive();

    /**
     * @brief 获取最近一次数据库操作的错误信息
     * @return QString 错误描述文本
     */
    QString lastError() const;

private:
    /**
     * @brief 获取底层 QSqlDatabase 引用
     *
     * @details 仅供 ScopedConn 在析构时归还连接到连接池。
     *          外部不可访问，防止误操破坏连接池内部状态。
     *
     * @return QSqlDatabase& 底层数据库连接引用
     */
    QSqlDatabase &raw();
    friend class ScopedConn;

    QSqlDatabase m_db;  ///< 底层 Qt 数据库连接对象
};

/**
 * @brief 数据库连接池（线程安全单例，基于 QThreadStorage 实现线程隔离）
 *
 * @details 核心设计：
 *          - 每线程独立池：通过 QThreadStorage 为每个线程维护独立的空闲连接队列，
 *            彻底消除 QSqlDatabase 跨线程使用的风险。
 *          - 共享配置：DBConfig 在 init() 时设置一次，所有线程共享（只读）。
 *          - 懒初始化：非 init 线程首次 acquire() 时自动创建本线程的池。
 *          - 预热机制：init() 仅预热调用线程的连接池。
 *          - 心跳检测：acquire() 和 release() 时对空闲连接做 SELECT 1 检测。
 *          - 线程退出时 QThreadStorage 自动回收该线程的所有连接。
 *
 *          使用流程：init() → acquire()/release()（各线程） → shutdown()
 *
 * @note 与旧版的关键区别：旧版所有线程共享一个连接队列，
 *       无法保证 QSqlDatabase 仅在其创建线程中使用。
 *       新版通过 ThreadCtx 将连接严格限制在创建线程内。
 */
class DBPool {
public:
    /**
     * @brief 获取连接池单例
     * @return DBPool& 全局唯一实例
     */
    static DBPool &instance();

    /**
     * @brief 初始化连接池
     *
     * @details 保存全局配置，并预热调用线程的连接池（预创建 minPoolSize 个连接）。
     *          重复调用无副作用。
     *          其他线程首次 acquire() 时会自动用已保存的配置初始化自己的池。
     *          必须在任何线程调用 acquire() 之前完成首次 init()。
     */
    void init();

    /**
     * @brief 从当前线程的连接池获取一个连接
     *
     * @details 获取策略（三级优先级）：
     *          1. 本线程有空闲连接 → 取队首 → 心跳检测通过 → 返回
     *          2. 心跳失败 → 销毁 → 重试
     *          3. 无空闲但本线程未达上限 → 创建新连接（在当前线程创建） → 返回
     *          4. 本线程已达上限 → 等待（timeoutMs 超时抛异常）
     *
     *          首次调用时自动用已保存的 DBConfig 初始化本线程的池。
     *
     * @return DBConn 在当前线程可用的数据库连接封装
     *
     * @throws DBException 未初始化或获取超时时抛出
     */
    DBConn acquire();

    /**
     * @brief 归还连接到当前线程的连接池
     *
     * @details 归还时做心跳检测：
     *          - 存活 → 入当前线程的空闲队列 → 唤醒同线程等待者
     *          - 断开 → 销毁连接 → 计数减一
     *
     * @param[in] db 待归还的连接
     */
    void release(QSqlDatabase db);

    /**
     * @brief 关闭当前线程的连接池并释放所有连接
     *
     * @details 清空当前线程的空闲队列，逐一销毁连接，重置计数器。
     *          其他线程的连接池不受影响。
     */
    void shutdown();

private:
    DBPool() = default;
    ~DBPool();

    /**
     * @brief 每线程独立的连接池上下文
     *
     * @details 每个线程有自己独立的空闲队列和计数器，
     *          连接在该线程中创建、使用、销毁，遵守 QSqlDatabase 线程亲和性。
     */
    struct ThreadCtx {
        QQueue<QSqlDatabase> idlePool;   /// 本线程的空闲连接队列
        int totalSize = 0;               /// 本线程当前总连接数
        QMutex mutex;                    /// 本线程池的互斥锁
        QWaitCondition cond;             /// 本线程池的条件变量

        ~ThreadCtx();                    /// 析构时销毁所有空闲连接（在 ThreadCtx.cpp 中实现）
    };

    /**
     * @brief 获取当前线程的 ThreadCtx（不存在则创建）
     * @return ThreadCtx& 当前线程的连接池上下文
     */
    ThreadCtx &threadCtx();

    /**
     * @brief 在当前线程创建并打开一个数据库连接
     *
     * @details 使用 UUID 生成唯一连接名（避免与 Qt 全局连接名冲突），
     *          必须在目标使用线程中调用，确保 QSqlDatabase 线程亲和性。
     *
     * @return QSqlDatabase 已打开的数据库连接
     *
     * @throws DBException 打开连接失败时抛出
     */
    QSqlDatabase createConnection();

    /**
     * @brief 在当前线程销毁数据库连接（静态方法）
     *
     * @details 关闭连接 → 解除本地引用 → 从 Qt 全局连接注册表移除。
     *          静态方法，供 ThreadCtx 析构和池内其他位置复用。
     *
     * @param[in,out] db 待销毁的连接引用
     */
    static void destroyConnection(QSqlDatabase &db);

    QAtomicInt m_ready{0};               /// 是否已完成 init 配置

    QThreadStorage<ThreadCtx *> m_threadCtx;  /// 每线程独立池，线程退出时自动回收
};

/**
 * @brief RAII 连接管理类（自动获取 + 自动归还）
 *
 * @details 构造时从连接池 acquire 一个 DBConn，
 *          析构时自动 release 回连接池。
 *          通过 operator-> 直接调用 DBConn 的方法。
 *
 * @note 典型用法：
 * @code
 * auto rows = ScopedConn()->queryAll("SELECT * FROM users");
 * @endcode
 */
class ScopedConn {
public:
    /**
     * @brief 构造时从池中获取一个连接
     * @throws DBException 连接池获取超时时抛出
     */
    ScopedConn() {
        m_conn = std::make_unique<DBConn>(DBPool::instance().acquire());
    }

    /**
     * @brief 析构时归还连接到池（若未提前 release）
     */
    ~ScopedConn() {
        if (m_conn) {
            DBPool::instance().release(m_conn->raw());
        }
    }

    /**
     * @brief 提前归还连接到池
     *
     * @details 调用后 ScopedConn 变为空壳，不可再使用 operator->。
     *          后续析构不会再重复归还。
     *
     * @note 适用场景：拿到查询结果后立即归还连接，
     *       后续仅对内存中的结果集做计算，不占用连接。
     */
    void release() {
        if (m_conn) {
            DBPool::instance().release(m_conn->raw());
            m_conn.reset();
        }
    }

    /**
     * @brief 指针访问运算符，直接调用 DBConn 方法
     * @return DBConn* 底层连接封装指针（release 后返回 nullptr）
     */
    DBConn *operator->() { return m_conn.get(); }

    /**
     * @brief 检查是否持有有效连接
     * @return true 连接有效可用
     */
    explicit operator bool() const { return m_conn != nullptr; }

private:
    std::unique_ptr<DBConn> m_conn;  ///< 持有的连接封装（release 后为 nullptr）
};

/**
 * @brief RAII 事务封装类
 *
 * @details 构造时自动 begin()，析构时若未 commit 则自动 rollback()。
 *          确保事务在任何退出路径上要么提交、要么回滚。
 *
 * @note 典型用法：
 * @code
 * DBTransaction txn(conn);
 * conn.exec("UPDATE ...");
 * // ...
 * txn.commit();  // 显式提交，否则析构自动回滚
 * @endcode
 */
class DBTransaction {
public:
    /**
     * @brief 构造即开启事务
     *
     * @details 调用 DBConn::begin() 开启事务，并将 m_active 置为 true。
     *
     * @param[in] conn 数据库连接封装引用
     *
     * @throws DBException 事务开启失败时抛出
     */
    explicit DBTransaction(DBConn &conn)
        : m_conn(conn), m_active(true) {
        m_conn.begin();
    }

    /**
     * @brief 析构时若未提交则自动回滚
     *
     * @details 回滚失败时捕获异常并记录（通过 qWarning），
     *          绝不抛出异常（避免在栈展开时触发 std::terminate）。
     */
    ~DBTransaction() {
        if (m_active) {
            try {
                m_conn.rollback();
            } catch (const DBException &e) {
                LOG_WARN() << "DBTransaction rollback failed: " << e.what();
            }
        }
    }

    /**
     * @brief 提交事务
     *
     * @details 提交后将 m_active 置为 false，
     *          析构时不再回滚。
     *
     * @throws DBException 提交失败时抛出
     */
    void commit() {
        m_conn.commit();
        m_active = false;
    }

private:
    DBConn &m_conn;   /// 数据库连接引用
    bool m_active;    /// 事务是否活跃（未提交）
};
