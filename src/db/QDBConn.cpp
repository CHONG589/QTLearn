#include "QDBConn.h"

static zch::ConfigVar<QString>::ptr g_db_ip =
    zch::Config::lookup("database.ip", QString("127.0.0.1"), "database ip address");

static zch::ConfigVar<int>::ptr g_db_port =
    zch::Config::lookup("database.port", (int)(3306), "database port");

static zch::ConfigVar<QString>::ptr g_db_user =
    zch::Config::lookup("database.user", QString(""), "database user");

static zch::ConfigVar<QString>::ptr g_db_pwd =
    zch::Config::lookup("database.pwd", QString(""), "database passwd");

static zch::ConfigVar<QString>::ptr g_db_driver =
    zch::Config::lookup("database.driver", QString(""), "database driver");

static zch::ConfigVar<int>::ptr g_db_min_size =
    zch::Config::lookup("database.minsize", (int)(100), "connectPool min size");

static zch::ConfigVar<int>::ptr g_db_max_size =
    zch::Config::lookup("database.maxsize", (int)(1024), "connectPool max size");

static zch::ConfigVar<int>::ptr g_db_timeout =
    zch::Config::lookup("database.timeout", (int)(1000), "connectPool timeout");

static zch::ConfigVar<QString>::ptr g_db_name =
    zch::Config::lookup("database.db", QString(""), "database name");

// ==================== DBConn ====================

/**
 * @brief 构造 DBConn
 *
 * @details 直接持有传入的 QSqlDatabase 副本（QSqlDatabase 采用隐式共享，拷贝轻量）。
 *           连接的实际生命周期由 DBPool 管理，DBConn 仅作为操作接口。
 *
 * @param[in] db 已在连接池中打开的数据库连接
 */
DBConn::DBConn(QSqlDatabase db)
    : m_db(db) {
}

// ==================== 写操作 ====================

/**
 * @brief 执行写 SQL
 *
 * @details 实现流程：
 *          1. 以当前连接构造 QSqlQuery
 *          2. 调用 QSqlQuery::exec() 执行
 *          3. 失败则抛出 DBException，包含 Qt 驱动的具体错误信息
 *
 * @param[in] sql 待执行的 INSERT / UPDATE / DELETE / DDL 语句
 *
 * @throws DBException 执行失败时抛出，携带 lastError().text()
 */
void DBConn::exec(const QString &sql) {
    QSqlQuery query(m_db);
    if (!query.exec(sql)) {
        throw DBException(query.lastError().text());
    }
}

// ==================== 查询 ====================

/**
 * @brief 流式查询
 *
 * @details 实现流程：
 *          1. 以当前连接构造 QSqlQuery
 *          2. 执行 SQL
 *          3. 返回 QSqlQuery 对象供调用方逐行遍历
 *
 *          与 queryAll / queryOne 的区别：
 *          数据尚未读取到内存，适合大数据集场景。
 *          代价是返回的 QSqlQuery 绑定当前连接，必须在 ScopedConn 生命周期内使用。
 *
 * @param[in] sql SELECT 查询语句
 * @return QSqlQuery 已执行查询的结果对象
 *
 * @throws DBException 执行失败时抛出
 */
QSqlQuery DBConn::query(const QString &sql) {
    QSqlQuery query(m_db);

    if (!query.exec(sql)) {
        throw DBException(query.lastError().text());
    }

    return query;
}

/**
 * @brief 查询全部结果集
 *
 * @details 实现流程：
 *          1. 调用 query() 执行 SELECT
 *          2. 通过 QSqlRecord 获取列数
 *          3. 遍历 QSqlQuery 每一行，逐列取值构造 QVariantList
 *          4. 所有行收集为 QList<QVariantList> 返回
 *
 *          数据完全读入内存后才返回，不依赖连接生命周期。
 *
 * @param[in] sql SELECT 查询语句
 * @return QList<QVariantList> 二维结果集
 *
 * @throws DBException 执行失败时抛出
 */
QList<QVariantList> DBConn::queryAll(const QString &sql) {
    QSqlQuery query = this->query(sql);

    QList<QVariantList> result;

    int colCount = query.record().count();

    while (query.next()) {
        QVariantList row;
        for (int i = 0; i < colCount; ++i) {
            row << query.value(i);
        }
        result << row;
    }

    return result;
}

/**
 * @brief 查询第一行数据
 *
 * @details 实现流程：
 *          1. 调用 query() 执行 SELECT
 *          2. 调用 query.next() 尝试读取第一行
 *          3. 有数据 → 构造 QVariantList 返回
 *          4. 无数据 → 返回空的 QVariantList
 *
 * @param[in] sql SELECT 查询语句
 * @return QVariantList 第一行数据，无结果返回 {}
 */
QVariantList DBConn::queryOne(const QString &sql) {
    QSqlQuery query = this->query(sql);
    if (query.next()) {
        QVariantList row;
        const int colCount = query.record().count();
        for (int i = 0; i < colCount; ++i) {
            row << query.value(i);
        }
        return row;
    }
    return {};
}

/**
 * @brief 查询至多一行（std::optional 版本）
 *
 * @details 实现流程同 queryOne，区别在于返回值语义：
 *          - 有数据 → std::optional<QVariantList> 含值
 *          - 无数据 → std::nullopt
 *
 * @param[in] sql SELECT 查询语句
 * @return std::optional<QVariantList> 有数据时含值，否则 std::nullopt
 */
std::optional<QVariantList> DBConn::queryRow(const QString &sql) {
    QSqlQuery query = this->query(sql);
    if (query.next()) {
        QVariantList row;
        const int colCount = query.record().count();
        for (int i = 0; i < colCount; ++i) {
            row << query.value(i);
        }
        return row;
    }
    return std::nullopt;
}

// ==================== 预处理 ====================

/**
 * @brief 预处理 SQL 模板
 *
 * @details 实现流程：
 *          1. 以当前连接构造 QSqlQuery
 *          2. 调用 QSqlQuery::prepare() 对 SQL 模板进行预编译
 *          3. 失败抛 DBException
 *          4. 返回 QSqlQuery，可复用执行 execPrepared()
 *
 *          预编译由数据库驱动层完成（如 MySQL 的 COM_STMT_PREPARE），
 *          后续执行只需发送参数，减少 SQL 解析开销，同时防止 SQL 注入。
 *
 * @param[in] sql 含占位符的 SQL 模板（占位符：? 或 :name）
 * @return QSqlQuery 已预编译的查询对象
 *
 * @throws DBException 预编译失败时抛出
 */
QSqlQuery DBConn::prepare(const QString &sql) {
    QSqlQuery query(m_db);
    if (!query.prepare(sql)) {
        throw DBException(query.lastError().text());
    }
    return query;
}

/**
 * @brief 为预编译对象绑定参数并执行
 *
 * @details 实现流程：
 *          1. 遍历 args，逐一调用 QSqlQuery::addBindValue() 绑定参数
 *          2. 调用 QSqlQuery::exec() 执行预编译语句
 *          3. 失败抛 DBException
 *
 *          与 prepare() 搭配实现"一次编译，多次执行"的批量操作。
 *
 * @param[in,out] query 由 prepare() 返回的预编译对象
 * @param[in]     args  参数列表，按占位符顺序排列
 *
 * @throws DBException 绑定或执行失败时抛出
 */
void DBConn::execPrepared(QSqlQuery &query, const QVariantList &args) {
    for (const auto &arg : args) {
        query.addBindValue(arg);
    }
    if (!query.exec()) {
        throw DBException(query.lastError().text());
    }
}

// ---- 一步式便捷实现 ----

/**
 * @brief 一步式预处理写操作
 *
 * @details 实现：先 prepare(sql) 再 execPrepared(query, args)，
 *          将两步合并为一次调用。适合单次执行的场景。
 *
 * @param[in] sql  含占位符的 SQL 模板
 * @param[in] args 参数列表
 *
 * @throws DBException 任一阶段失败时抛出
 */
void DBConn::execPrepared(const QString &sql, const QVariantList &args) {
    auto q = prepare(sql);
    execPrepared(q, args);
}

/**
 * @brief 一步式预处理查询全部
 *
 * @details 实现流程：
 *          1. prepare(sql) → execPrepared(q, args) → 遍历结果集
 *          2. 与 queryAll 相同的逐行逐列读取逻辑
 *
 * @param[in] sql  含占位符的 SELECT 语句
 * @param[in] args 参数列表
 * @return QList<QVariantList> 二维结果集
 *
 * @throws DBException 任一阶段失败时抛出
 */
QList<QVariantList> DBConn::queryAllPrepared(const QString &sql, const QVariantList &args) {
    auto q = prepare(sql);
    execPrepared(q, args);

    QList<QVariantList> result;
    const int colCount = q.record().count();
    while (q.next()) {
        QVariantList row;
        for (int i = 0; i < colCount; ++i) {
            row << q.value(i);
        }
        result << row;
    }
    return result;
}

/**
 * @brief 一步式预处理查询第一行
 *
 * @details 实现流程：
 *          1. prepare(sql) → execPrepared(q, args) → 读取第一行
 *          2. 无结果返回空 QVariantList
 *
 * @param[in] sql  含占位符的 SELECT 语句
 * @param[in] args 参数列表
 * @return QVariantList 第一行数据，无结果返回 {}
 *
 * @throws DBException 任一阶段失败时抛出
 */
QVariantList DBConn::queryOnePrepared(const QString &sql, const QVariantList &args) {
    auto q = prepare(sql);
    execPrepared(q, args);
    if (q.next()) {
        QVariantList row;
        const int colCount = q.record().count();
        for (int i = 0; i < colCount; ++i) {
            row << q.value(i);
        }
        return row;
    }
    return {};
}

// ==================== 事务 ====================

/**
 * @brief 开启数据库事务
 *
 * @details 调用 QSqlDatabase::transaction()，失败抛 DBException。
 *
 * @throws DBException 事务开启失败时抛出
 */
void DBConn::begin() {
    if (!m_db.transaction()) {
        throw DBException(m_db.lastError().text());
    }
}

/**
 * @brief 提交当前事务
 *
 * @details 调用 QSqlDatabase::commit() 持久化所有修改。
 *
 * @throws DBException 提交失败时抛出
 */
void DBConn::commit() {
    if (!m_db.commit()) {
        throw DBException(m_db.lastError().text());
    }
}

/**
 * @brief 回滚当前事务
 *
 * @details 调用 QSqlDatabase::rollback() 撤销所有未提交修改。
 *
 * @throws DBException 回滚失败时抛出
 */
void DBConn::rollback() {
    if (!m_db.rollback()) {
        throw DBException(m_db.lastError().text());
    }
}

// ==================== 状态检测 ====================

/**
 * @brief 判断连接是否有效
 *
 * @details 同时检查 QSqlDatabase::isValid()（Qt 内部状态）和 isOpen()（实际 socket 状态）。
 *
 * @return true 连接有效且已打开
 */
bool DBConn::isValid() const {
    return m_db.isValid() && m_db.isOpen();
}

/**
 * @brief 心跳检测
 *
 * @details 实现流程：
 *          1. 先调用 isValid() 快速排除无效连接
 *          2. 执行 SELECT 1 轻量查询验证数据库仍在响应
 *          3. 用于连接池回收前判断连接是否存活
 *
 * @return true 连接存活且可通信
 */
bool DBConn::isAlive() {
    if (!isValid()) {
        return false;
    }

    QSqlQuery q(m_db);
    return q.exec("SELECT 1");
}

/**
 * @brief 获取最近一次操作的数据库错误信息
 * @return QString 错误描述文本
 */
QString DBConn::lastError() const {
    return m_db.lastError().text();
}

/**
 * @brief 获取底层 QSqlDatabase 引用（仅供 ScopedConn 使用）
 * @return QSqlDatabase& 底层连接引用
 */
QSqlDatabase &DBConn::raw() {
    return m_db;
}

// ==================== DBPool::ThreadCtx ====================

/**
 * @brief ThreadCtx 析构：销毁当前线程的所有空闲连接
 *
 * @details QThreadStorage 在线程退出时自动 delete ThreadCtx 指针，
 *          因此析构函数负责清理该线程持有的所有空闲连接。
 *          这是线程安全连接管理的最后一道防线。
 */
DBPool::ThreadCtx::~ThreadCtx() {
    while (!idlePool.isEmpty()) {
        QSqlDatabase db = idlePool.dequeue();
        DBPool::destroyConnection(db);
    }
}

// ==================== DBPool ====================

/**
 * @brief 获取连接池单例
 *
 * @details 利用 C++11 Magic Statics 保证线程安全的懒加载单例。
 *
 * @return DBPool& 全局唯一连接池实例
 */
DBPool &DBPool::instance() {
    static DBPool pool;
    return pool;
}

/**
 * @brief 初始化连接池
 *
 * @details 实现流程：
 *          1. 保存全局配置（后续其他线程创建本地池时作为模板）
 *          2. 获取当前线程的 ThreadCtx（首次访问，自动 new 并注册到 QThreadStorage）
 *          3. 预创建 minPoolSize 个连接入本线程空闲队列
 *          4. 设置 m_ready 标志（此时其他线程才可 acquire）
 *
 *          仅预热调用线程（通常是主线程），
 *          其他工作线程在首次 acquire() 时自动用共享的 m_config 初始化自己的池。
 */
void DBPool::init() {
    // 防重复初始化：已就绪则忽略后续调用
    if (m_ready.loadAcquire()) {
        return;
    }

    ThreadCtx &ctx = threadCtx();
    QMutexLocker locker(&ctx.mutex);
    for (int i = 0; i < g_db_min_size->getValue(); ++i) {
        QSqlDatabase db = createConnection();
        if (db.isOpen()) {
            ctx.idlePool.enqueue(db);
            ctx.totalSize++;
        }
    }

    // release 语义：确保 m_config 的写入对所有线程可见后再放行
    m_ready.storeRelease(1);
}

/**
 * @brief 获取当前线程的 ThreadCtx（不存在则创建）
 *
 * @details 利用 QThreadStorage::hasLocalData() 检查当前线程是否已有池上下文，
 *          无则 new ThreadCtx 并通过 setLocalData() 注册。
 *          QThreadStorage 保证在线程退出时自动 delete 该指针，
 *          从而触发 ThreadCtx::~ThreadCtx() → 清理所有空闲连接。
 *
 * @return ThreadCtx& 当前线程的连接池上下文
 */
DBPool::ThreadCtx &DBPool::threadCtx() {
    if (!m_threadCtx.hasLocalData()) {
        m_threadCtx.setLocalData(new ThreadCtx());
    }
    return *m_threadCtx.localData();
}

/**
 * @brief 从当前线程的连接池获取连接
 *
 * @details 获取策略（三级优先级）：
 *          1. **复用本线程空闲连接**：
 *             - 从本线程空闲队列头部取出连接
 *             - 执行 SELECT 1 心跳检测（连接可能在空闲期间被 MySQL 服务端关闭）
 *             - 存活 → 包装为 DBConn 返回
 *             - 断开 → destroyConnection() → 计数减一 → 重试
 *          2. **在本线程创建新连接**：
 *             - 本线程当前连接数 < maxPoolSize → createConnection() → 计数加一 → 返回
 *             - ★ 关键：连接在 acquire() 调用线程中创建，满足 QSqlDatabase 线程亲和性
 *          3. **等待本线程 release**：
 *             - 本线程已达上限 → 在条件变量上等待（最多 timeoutMs 毫秒）
 *             - 被同线程 release() 唤醒后回到步骤 1
 *             - 超时 → 抛 DBException
 *
 * @return DBConn 在当前线程可用的连接封装
 *
 * @throws DBException 未初始化或等待超时时抛出
 */
DBConn DBPool::acquire() {
    if (!m_ready.loadAcquire()) {
        //throw DBException("DBPool not initialized");
        init();     // 自动初始化
    }

    ThreadCtx &ctx = threadCtx();
    QMutexLocker locker(&ctx.mutex);

    while (true) {
        // 优先级 1：复用本线程空闲连接
        if (!ctx.idlePool.isEmpty()) {
            QSqlDatabase db = ctx.idlePool.dequeue();

            QSqlQuery q(db);
            if (!q.exec("SELECT 1")) {
                destroyConnection(db);
                ctx.totalSize--;
                continue;
            }

            return DBConn(db);
        }

        // 优先级 2：在本线程创建新连接
        if (ctx.totalSize < g_db_max_size->getValue()) {
            QSqlDatabase db = createConnection();
            ctx.totalSize++;
            return DBConn(db);
        }

        // 优先级 3：等待同线程 release
        if (!ctx.cond.wait(&ctx.mutex, g_db_timeout->getValue())) {
            throw DBException("DBPool acquire timeout");
        }
    }
}

/**
 * @brief 归还连接到当前线程的连接池
 *
 * @details 实现流程：
 *          1. 加锁
 *          2. 检查连接有效性（无效则直接丢弃，不回收）
 *          3. 执行 SELECT 1 心跳检测
 *          4. 存活 → 入本线程空闲队列 → 唤醒可能等待的同线程 acquire
 *          5. 断开 → 销毁连接 → 计数减一
 *
 *          ★ 关键：连接始终归还到创建它的线程池中，避免跨线程使用。
 *
 * @param[in] db 待归还的连接
 */
void DBPool::release(QSqlDatabase db) {
    ThreadCtx &ctx = threadCtx();
    QMutexLocker locker(&ctx.mutex);

    if (!db.isValid()) {
        return;
    }

    QSqlQuery q(db);
    if (!q.exec("SELECT 1")) {
        destroyConnection(db);
        ctx.totalSize--;
    } else {
        ctx.idlePool.enqueue(db);
    }

    ctx.cond.wakeOne();
}

/**
 * @brief 在当前线程创建并打开数据库连接
 *
 * @details 实现流程：
 *          1. 用 QUuid 生成全局唯一连接名
 *          2. QSqlDatabase::addDatabase() 注册到 Qt 全局连接表
 *          3. 配置主机 / 端口 / 数据库名 / 用户名 / 密码
 *          4. open() 建立 TCP 连接
 *
 *          此方法始终在目标使用线程中被调用，确保 QSqlDatabase 线程亲和性。
 *
 * @return QSqlDatabase 已打开的连接
 *
 * @throws DBException 驱动不可用或连接失败时抛出
 */
QSqlDatabase DBPool::createConnection() {
    QString connName = QUuid::createUuid().toString();

    QSqlDatabase db = QSqlDatabase::addDatabase(g_db_driver->getValue(), connName);

    db.setHostName(g_db_ip->getValue());
    db.setPort(g_db_port->getValue());
    db.setDatabaseName(g_db_name->getValue());
    db.setUserName(g_db_user->getValue());
    db.setPassword(g_db_pwd->getValue());

    if (!db.open()) {
        throw DBException("DB open failed: " + db.lastError().text());
    }

    return db;
}

/**
 * @brief 在当前线程销毁数据库连接
 *
 * @details 实现流程：
 *          1. 保存连接名（close 后无法通过 QSqlDatabase 访问名称）
 *          2. db.close() 关闭底层 socket
 *          3. db = QSqlDatabase() 清除本地引用（关键步骤）
 *          4. QSqlDatabase::removeDatabase() 从 Qt 全局注册表移除
 *
 *          第 3 步至关重要：QSqlDatabase 使用隐式共享（引用计数），
 *          若仍有任何副本持有该连接名，removeDatabase() 会因引用计数非零而静默失败，
 *          导致连接名永久泄漏在 Qt 全局注册表中。
 *
 * @param[in,out] db 待销毁的连接引用
 */
void DBPool::destroyConnection(QSqlDatabase &db) {
    QString name = db.connectionName();

    db.close();
    db = QSqlDatabase();

    QSqlDatabase::removeDatabase(name);
}

/**
 * @brief 关闭当前线程的连接池
 *
 * @details 加锁后遍历当前线程的空闲队列，逐一销毁连接，重置计数器。
 *          其他线程的连接池完全不受影响。
 *
 *          运行中的其他线程：其 ThreadCtx 由 QThreadStorage 在线程退出时
 *          自动 delete（触发 ThreadCtx::~ThreadCtx() 清理空闲连接）。
 */
void DBPool::shutdown() {
    if (!m_threadCtx.hasLocalData()) {
        return;
    }

    ThreadCtx &ctx = threadCtx();
    QMutexLocker locker(&ctx.mutex);

    while (!ctx.idlePool.isEmpty()) {
        QSqlDatabase db = ctx.idlePool.dequeue();
        destroyConnection(db);
    }

    ctx.totalSize = 0;
}

/**
 * @brief DBPool 析构
 *
 * @details 关闭调用线程（主线程）的连接池。
 *          单例通常在进程退出时析构，此时所有子线程已终止，
 *          QThreadStorage 已完成各线程的 ThreadCtx 自动回收。
 */
DBPool::~DBPool() {
    shutdown();
}
