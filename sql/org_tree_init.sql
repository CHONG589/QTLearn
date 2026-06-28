-- ============================================================
-- 组织架构与内容树 建表 + 测试数据
-- 使用方法：在 MySQL 中 source 本文件即可
--    mysql> source E:/Code/QTCode/QTLearn/sql/org_tree_init.sql
-- ============================================================

DROP TABLE IF EXISTS org_tree;

-- ============================================================
-- 建表
-- ============================================================
CREATE TABLE org_tree (
    id         BIGINT AUTO_INCREMENT PRIMARY KEY,
    name       VARCHAR(255) NOT NULL   COMMENT '节点名称',
    node_type  TINYINT      NOT NULL   COMMENT '0=部门, 1=分类, 2=内容',
    parent_id  BIGINT       NULL       COMMENT '父节点 ID，根节点为 NULL',
    sort_order INT          DEFAULT 0  COMMENT '同级排序',

    INDEX idx_parent (parent_id),
    INDEX idx_type   (node_type),

    CONSTRAINT fk_org_parent
        FOREIGN KEY (parent_id) REFERENCES org_tree(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='组织架构与内容树';

-- ============================================================
-- 测试数据
-- 关键原则：每个 INSERT 批次后立刻用 LAST_INSERT_ID() + 偏移
--          获取该批次每一行的实际 ID，绝不跨批次推算
-- ============================================================

-- L1: 公司总部 -------------------------------------------------
INSERT INTO org_tree (name, node_type, parent_id, sort_order)
VALUES ('公司总部', 0, NULL, 0);
SET @root = LAST_INSERT_ID();

-- L2: 一级部门 (5 条，ID 连续) ---------------------------------
INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('技术部',      0, @root, 1),
('市场部',      0, @root, 2),
('财务部',      0, @root, 3),
('人力资源部',  0, @root, 4),
('行政部',      0, @root, 5);
-- LAST_INSERT_ID() = 技术部 的 ID

SET @dept_tech = LAST_INSERT_ID() + 0;
SET @dept_mkt  = LAST_INSERT_ID() + 1;
SET @dept_fin  = LAST_INSERT_ID() + 2;
SET @dept_hr   = LAST_INSERT_ID() + 3;
SET @dept_adm  = LAST_INSERT_ID() + 4;

-- L3: 技术部子部门 (4 条) ---------------------------------------
INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('研发一部', 0, @dept_tech, 1),
('研发二部', 0, @dept_tech, 2),
('测试部',   0, @dept_tech, 3),
('运维部',   0, @dept_tech, 4);

SET @rd1 = LAST_INSERT_ID() + 0;
SET @rd2 = LAST_INSERT_ID() + 1;
SET @qa  = LAST_INSERT_ID() + 2;
SET @ops = LAST_INSERT_ID() + 3;

-- L3: 市场部子部门 (3 条) ---------------------------------------
INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('市场一部', 0, @dept_mkt, 1),
('市场二部', 0, @dept_mkt, 2),
('品牌部',   0, @dept_mkt, 3);

SET @mkt1  = LAST_INSERT_ID() + 0;
SET @mkt2  = LAST_INSERT_ID() + 1;
SET @brand = LAST_INSERT_ID() + 2;

-- L3: 财务部子部门 (2 条) ---------------------------------------
INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('财务一部', 0, @dept_fin, 1),
('财务二部', 0, @dept_fin, 2);

SET @fin1 = LAST_INSERT_ID() + 0;
SET @fin2 = LAST_INSERT_ID() + 1;

-- ============================================================
-- 分类 → 研发一部 (3 条)
-- ============================================================
INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('规章制度', 1, @rd1, 1),
('技术规范', 1, @rd1, 2),
('培训资料', 1, @rd1, 3);

SET @cat_rd1_reg  = LAST_INSERT_ID() + 0;
SET @cat_rd1_tech = LAST_INSERT_ID() + 1;
SET @cat_rd1_trn  = LAST_INSERT_ID() + 2;

-- 内容 → 规章制度 (3 条) ---------------------------------------
INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('员工手册', 2, @cat_rd1_reg, 1),
('考勤制度', 2, @cat_rd1_reg, 2),
('薪酬福利', 2, @cat_rd1_reg, 3);

SET @c_handbook   = LAST_INSERT_ID() + 0;
SET @c_attendance = LAST_INSERT_ID() + 1;
SET @c_salary     = LAST_INSERT_ID() + 2;

INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('入职指引',       2, @c_handbook,   1),
('行为准则',       2, @c_handbook,   2),
('信息安全规范',   2, @c_handbook,   3),
('请假规定',       2, @c_attendance, 1),
('加班管理',       2, @c_attendance, 2),
('工资结构',       2, @c_salary,     1),
('奖金制度',       2, @c_salary,     2),
('社保公积金',     2, @c_salary,     3);

-- 内容 → 技术规范 (3 条) ---------------------------------------
INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('编码规范',     2, @cat_rd1_tech, 1),
('代码审查流程', 2, @cat_rd1_tech, 2),
('版本管理规范', 2, @cat_rd1_tech, 3);

SET @c_coding  = LAST_INSERT_ID() + 0;
SET @c_review  = LAST_INSERT_ID() + 1;
SET @c_version = LAST_INSERT_ID() + 2;

INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('C++代码规范',      2, @c_coding,  1),
('Qt开发规范',       2, @c_coding,  2),
('数据库设计规范',   2, @c_coding,  3),
('自审清单',         2, @c_review,  1),
('同行评审指南',     2, @c_review,  2),
('评审记录模板',     2, @c_review,  3),
('Git分支策略',      2, @c_version, 1),
('版本号命名规则',   2, @c_version, 2),
('发布流程',         2, @c_version, 3);

-- 内容 → 培训资料 (2 条) ---------------------------------------
INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('新员工入职培训', 2, @cat_rd1_trn, 1),
('技术分享计划',   2, @cat_rd1_trn, 2);

SET @c_onboard = LAST_INSERT_ID() + 0;
SET @c_tshare  = LAST_INSERT_ID() + 1;

INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('公司文化介绍', 2, @c_onboard, 1),
('开发环境搭建', 2, @c_onboard, 2),
('内部讲师制度', 2, @c_tshare,  1),
('分享主题库',   2, @c_tshare,  2);

-- ============================================================
-- 分类 → 研发二部 (2 条)
-- ============================================================
INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('项目管理制度', 1, @rd2, 1),
('安全规范',     1, @rd2, 2);

SET @cat_rd2_pm  = LAST_INSERT_ID() + 0;
SET @cat_rd2_sec = LAST_INSERT_ID() + 1;

-- 内容 → 项目管理制度 (3 条) -----------------------------------
INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('立项流程', 2, @cat_rd2_pm, 1),
('项目跟踪', 2, @cat_rd2_pm, 2),
('项目结项', 2, @cat_rd2_pm, 3);

SET @c_init     = LAST_INSERT_ID() + 0;
SET @c_tracking = LAST_INSERT_ID() + 1;
SET @c_closure  = LAST_INSERT_ID() + 2;

INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('需求评审',     2, @c_init,     1),
('可行性分析',   2, @c_init,     2),
('立项审批',     2, @c_init,     3),
('周报制度',     2, @c_tracking, 1),
('里程碑评审',   2, @c_tracking, 2),
('风险管理',     2, @c_tracking, 3),
('验收标准',     2, @c_closure,  1),
('总结报告模板', 2, @c_closure,  2);

-- 内容 → 安全规范 (2 条) ---------------------------------------
INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('数据安全管理办法', 2, @cat_rd2_sec, 1),
('应急预案',         2, @cat_rd2_sec, 2);

SET @c_datasec   = LAST_INSERT_ID() + 0;
SET @c_emergency = LAST_INSERT_ID() + 1;

INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('数据分级标准', 2, @c_datasec,   1),
('访问控制策略', 2, @c_datasec,   2),
('数据备份规范', 2, @c_datasec,   3),
('安全事件响应', 2, @c_emergency, 1),
('灾备恢复流程', 2, @c_emergency, 2);

-- ============================================================
-- 分类 → 测试部 (2 条)
-- ============================================================
INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('测试规范',     1, @qa, 1),
('质量管理制度', 1, @qa, 2);

SET @cat_qa_spec = LAST_INSERT_ID() + 0;
SET @cat_qa_qm   = LAST_INSERT_ID() + 1;

INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('测试用例编写标准', 2, @cat_qa_spec, 1),
('自动化测试规范',   2, @cat_qa_spec, 2),
('缺陷管理办法',     2, @cat_qa_qm,   1),
('质量度量指标',     2, @cat_qa_qm,   2);

-- ============================================================
-- 分类 → 人力资源部 (2 条)
-- ============================================================
INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('人事制度', 1, @dept_hr, 1),
('招聘流程', 1, @dept_hr, 2);

SET @cat_hr_reg = LAST_INSERT_ID() + 0;
SET @cat_hr_rec = LAST_INSERT_ID() + 1;

-- 内容 → 人事制度 (3 条) ---------------------------------------
INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('招聘管理办法', 2, @cat_hr_reg, 1),
('绩效考核制度', 2, @cat_hr_reg, 2),
('培训管理制度', 2, @cat_hr_reg, 3);

SET @c_recruit = LAST_INSERT_ID() + 0;
SET @c_perf    = LAST_INSERT_ID() + 1;

INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('岗位发布流程', 2, @c_recruit, 1),
('面试评价标准', 2, @c_recruit, 2),
('背调流程',     2, @c_recruit, 3),
('KPI设定指南',  2, @c_perf,    1),
('季度考核流程', 2, @c_perf,    2),
('晋升评审标准', 2, @c_perf,    3);

-- 内容 → 招聘流程 (2 条) ---------------------------------------
INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('需求审批', 2, @cat_hr_rec, 1),
('渠道管理', 2, @cat_hr_rec, 2);

SET @c_demand  = LAST_INSERT_ID() + 0;
SET @c_channel = LAST_INSERT_ID() + 1;

INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('编制审核',     2, @c_demand,  1),
('JD模板',       2, @c_demand,  2),
('猎头合作规范', 2, @c_channel, 1),
('内部推荐制度', 2, @c_channel, 2);

-- ============================================================
-- 分类 → 财务一部 (1 条)
-- ============================================================
INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('财务制度', 1, @fin1, 1);

SET @cat_fin1_reg = LAST_INSERT_ID();

INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('报销管理规定', 2, @cat_fin1_reg, 1),
('预算管理制度', 2, @cat_fin1_reg, 2),
('资金管理制度', 2, @cat_fin1_reg, 3);

SET @c_reimburse = LAST_INSERT_ID() + 0;
SET @c_budget    = LAST_INSERT_ID() + 1;

INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('差旅报销标准', 2, @c_reimburse, 1),
('日常采购报销', 2, @c_reimburse, 2),
('招待费报销',   2, @c_reimburse, 3),
('年度预算编制', 2, @c_budget,    1),
('预算执行监控', 2, @c_budget,    2);

-- ============================================================
-- 分类 → 财务二部 (1 条)
-- ============================================================
INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('税务管理制度', 1, @fin2, 1);

SET @cat_fin2_tax = LAST_INSERT_ID();

INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('增值税管理',   2, @cat_fin2_tax, 1),
('所得税汇算',   2, @cat_fin2_tax, 2),
('税务稽查应对', 2, @cat_fin2_tax, 3);

-- ============================================================
-- 分类 → 市场一部 (1 条)
-- ============================================================
INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('市场管理制度', 1, @mkt1, 1);

SET @cat_mkt1_reg = LAST_INSERT_ID();

INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('品牌管理办法', 2, @cat_mkt1_reg, 1),
('市场活动流程', 2, @cat_mkt1_reg, 2);

-- ============================================================
-- 分类 → 行政部 (1 条)
-- ============================================================
INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('行政管理制度', 1, @dept_adm, 1);

SET @cat_adm_reg = LAST_INSERT_ID();

INSERT INTO org_tree (name, node_type, parent_id, sort_order) VALUES
('办公用品管理',   2, @cat_adm_reg, 1),
('固定资产管理',   2, @cat_adm_reg, 2),
('会议室使用规范', 2, @cat_adm_reg, 3);

-- ============================================================
-- 验证
-- ============================================================
SELECT COUNT(*) AS total_nodes FROM org_tree;
SELECT node_type, COUNT(*) AS cnt FROM org_tree GROUP BY node_type;
