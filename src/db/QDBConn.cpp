#include "QDBConn.h"

static zch::Logger::ptr g_logger = LOG_NAME("default");

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
 * @brief 构造 DBConn，持有传入的 QSqlDatabase 副本
 * @details QSqlDatabase 采用隐式共享，拷贝轻量。
 *          连接生命周期由 DBPool 管理，DBConn 仅作为操作接口。
 */
DBConn::DBConn(QSqlDatabase db)
    : m_db(db) {
}

// ==================== 写操作 ====================

/**
 * @brief 执行写 SQL，失败时抛出 DBException
 * @details 构造 QSqlQuery 并调用 exec()，失败消息来自 Qt 驱动的 lastError()。
 */
void DBConn::exec(const QString &sql) {
    QSqlQuery query(m_db);
    if (!query.exec(sql)) {
        throw DBException(query.lastError().text());
    }
}

// ==================== 查询 ====================

/**
 * @brief 流式查询，返回 QSqlQuery 供调用方逐行遍历
 * @details 数据尚未读取到内存，适合大数据集场景。
 *          返回的 QSqlQuery 绑定当前连接，必须在 ScopedConn 生命周期内用完。
 */
QSqlQuery DBConn::query(const QString &sql) {
    QSqlQuery query(m_db);

    if (!query.exec(sql)) {
        throw DBException(query.lastError().text());
    }

    return query;
}

/**
 * @brief 查询全部结果集，数据完整读入内存后返回
 * @details 通过 QSqlRecord 获取列数，逐行逐列构造 QVariantList，
 *          返回后不再依赖连接生命周期。
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
 * @brief 查询第一行，无结果返回空 QVariantList
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
 * @brief 查询至多一行，通过 std::optional 区分"有数据"与"无数据"
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
 * @brief 预处理 SQL 模板，返回可复用的 QSqlQuery
 * @details 预编译由数据库驱动层完成（如 MySQL 的 COM_STMT_PREPARE），
 *          后续执行只需发送参数，减少 SQL 解析开销并防止注入。
 */
QSqlQuery DBConn::prepare(const QString &sql) {
    QSqlQuery query(m_db);
    if (!query.prepare(sql)) {
        throw DBException(query.lastError().text());
    }
    return query;
}

/**
 * @brief 绑定参数并执行已预编译的 QSqlQuery
 * @details 遍历 args 逐一 addBindValue() 后 exec()，
 *          与 prepare() 搭配实现"一次编译，多次执行"。
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
 * @brief 一步式预处理写操作：内部完成 prepare → bind → exec
 */
void DBConn::execPrepared(const QString &sql, const QVariantList &args) {
    auto q = prepare(sql);
    execPrepared(q, args);
}

/**
 * @brief 一步式预处理查询全部：prepare → bind → exec → 读取全部结果
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
 * @brief 一步式预处理查询第一行：prepare → bind → exec → 读取首行
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

void DBConn::begin() {
    if (!m_db.transaction()) {
        throw DBException(m_db.lastError().text());
    }
}

void DBConn::commit() {
    if (!m_db.commit()) {
        throw DBException(m_db.lastError().text());
    }
}

void DBConn::rollback() {
    if (!m_db.rollback()) {
        throw DBException(m_db.lastError().text());
    }
}

// ==================== 状态检测 ====================

/**
 * @brief 判断连接是否有效
 * @details 同时检查 QSqlDatabase::isValid()（Qt 内部状态）和 isOpen()（socket 状态）。
 */
bool DBConn::isValid() const {
    return m_db.isValid() && m_db.isOpen();
}

/**
 * @brief 心跳检测，用于连接池回收前验证连接仍存活
 * @details 先快速排除无效连接，再执行 SELECT 1 验证数据库仍在响应。
 */
bool DBConn::isAlive() {
    if (!isValid()) {
        return false;
    }

    QSqlQuery q(m_db);
    return q.exec("SELECT 1");
}

QString DBConn::lastError() const {
    return m_db.lastError().text();
}

QSqlDatabase &DBConn::raw() {
    return m_db;
}

// ==================== DBPool::ThreadCtx ====================

/**
 * @brief ThreadCtx 析构：销毁当前线程的所有空闲连接
 * @details QThreadStorage 在线程退出时自动 delete ThreadCtx 指针，
 *          析构函数是线程安全连接管理的最后一道防线。
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
 * @details C++11 Magic Statics 保证线程安全的懒加载。
 */
DBPool &DBPool::instance() {
    static DBPool pool;
    return pool;
}

/**
 * @brief 初始化连接池，预热调用线程的连接
 * @details 防重复初始化（m_ready 标志），
 *          预创建 minPoolSize 个连接入本线程空闲队列，
 *          其他线程首次 acquire() 时用共享配置自动初始化自己的池。
 */
void DBPool::init() {
    // 已就绪则忽略后续调用
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

    // release 语义：确保配置写入对所有线程可见后再放行
    m_ready.storeRelease(1);
}

/**
 * @brief 获取当前线程的 ThreadCtx，不存在则创建
 * @details QThreadStorage 保证线程退出时自动 delete，触发 ~ThreadCtx() 清理空闲连接。
 */
DBPool::ThreadCtx &DBPool::threadCtx() {
    if (!m_threadCtx.hasLocalData()) {
        m_threadCtx.setLocalData(new ThreadCtx());
    }
    return *m_threadCtx.localData();
}

/**
 * @brief 从当前线程的连接池获取连接
 * @details 三级优先级获取策略：
 *          1. 复用本线程空闲连接（先心跳检测，失败则销毁重试）
 *          2. 本线程未达上限时创建新连接（在当前线程创建，满足 QSqlDatabase 线程亲和性）
 *          3. 已达上限则在条件变量上等待（最多 timeoutMs 毫秒，超时抛异常）
 *          未初始化时自动调用 init() 懒初始化。
 */
DBConn DBPool::acquire() {
    if (!m_ready.loadAcquire()) {
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
 * @brief 归还连接到当前线程的池
 * @details 心跳检测通过则入队并唤醒等待者，断开则销毁。
 *          连接始终归还到创建它的线程池，避免跨线程使用。
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
 * @details 使用 UUID 生成唯一连接名避免冲突，
 *          用户密码从加密配置中解密后使用。
 *          必须在目标使用线程中调用，确保 QSqlDatabase 线程亲和性。
 */
QSqlDatabase DBPool::createConnection() {
    QString connName = QUuid::createUuid().toString();

    QSqlDatabase db = QSqlDatabase::addDatabase(g_db_driver->getValue(), connName);

    db.setHostName(g_db_ip->getValue());
    db.setPort(g_db_port->getValue());
    db.setDatabaseName(g_db_name->getValue());
    db.setUserName(Crypto::decrypt(g_db_user->getValue()));
    db.setPassword(Crypto::decrypt(g_db_pwd->getValue()));

    if (!db.open()) {
        throw DBException("DB open failed: " + db.lastError().text());
    }

    return db;
}

/**
 * @brief 销毁数据库连接
 * @details 关闭 socket → 清除本地引用 → 从 Qt 全局注册表移除。
 *          中间将 db 重置为 QSqlDatabase() 是关键步骤：
 *          QSqlDatabase 使用隐式共享（引用计数），若仍有副本持有该连接名，
 *          removeDatabase() 会因引用计数非零而静默失败，导致连接名泄漏。
 */
void DBPool::destroyConnection(QSqlDatabase &db) {
    QString name = db.connectionName();

    db.close();
    db = QSqlDatabase();

    QSqlDatabase::removeDatabase(name);
}

/**
 * @brief 关闭当前线程的连接池，释放所有连接
 * @details 仅影响调用线程，其他线程的池由 QThreadStorage 在线程退出时自动回收。
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
 * @brief DBPool 析构：关闭主线程连接池
 * @details 进程退出时调用，此时子线程已终止，
 *          QThreadStorage 已完成各线程 ThreadCtx 的自动回收。
 */
DBPool::~DBPool() {
    shutdown();
}
