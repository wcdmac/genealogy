-- 人物卡按母亲找子女（parent_any 过滤）与父链同频：father_id 有索引
-- （0001 的 idx_person_father），mother_id 补齐同款，避免母链查询全表扫。
CREATE INDEX idx_person_mother ON person(mother_id);
