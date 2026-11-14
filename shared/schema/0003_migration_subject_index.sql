-- 迁徙查询按主体点查（REST ?subject_id=、expand=migrations、
-- 删除人物的级联清理），与 event 的 idx_event_subject 对齐。
-- branch_id 过滤的子查询吃 idx_person_branch，外层这个索引覆盖
-- 单主体与 IN 命中的回表。
CREATE INDEX idx_migration_subject ON migration(subject_type, subject_id);
