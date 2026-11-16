-- M7.1：名号变更史补列（ROADMAP 7.1「名号类型/起止/主名标记」）。
-- start/end 与 person 表同口径（GenealogyDate 文本，bind_opt_date 落库）；
-- 主名标记每人至多一条：partial unique index 在库层兜底，服务层置主
-- 时清除同人多条（双层互斥）。
ALTER TABLE person_name ADD COLUMN start_date TEXT;
ALTER TABLE person_name ADD COLUMN end_date TEXT;
ALTER TABLE person_name ADD COLUMN is_primary INTEGER NOT NULL DEFAULT 0;

CREATE INDEX idx_person_name_person ON person_name(person_id);
CREATE UNIQUE INDEX idx_person_name_primary
  ON person_name(person_id) WHERE is_primary = 1;
