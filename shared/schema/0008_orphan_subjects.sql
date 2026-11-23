-- 0008：修复 0007 单库迁移遗留的 lineage 主体孤儿行。
--
-- 背景：0007 删除了 lineage 表，但 event / citation 的 CHECK 仍只允许
-- 'lineage' 而不含 'book'。老库里若曾有以 lineage 为主体的事实/证据，
-- 升级后这些行的 subject_id 指向不存在的主体，成为静默孤儿。
-- 本迁移把这类行重映射到单库本体 'book'，并扩展 CHECK 使 book 合法。
-- 用 rename + 重建保留全部列/索引/外键；需临时关闭 foreign_keys
-- （由 database.cpp 的重建迁移分支处理，与 0007 同口径）。

CREATE TABLE event_new (
  id           TEXT PRIMARY KEY,
  subject_type TEXT NOT NULL CHECK (subject_type IN
               ('person','union','branch','lineage','book')),
  subject_id   TEXT NOT NULL,
  type         TEXT NOT NULL CHECK (type IN
               ('birth','death','burial','marriage','migration','examination',
                'office','honor','residence','annotation')),
  date         TEXT,
  place_id     TEXT REFERENCES place(id),
  description  TEXT,
  created_at   TEXT NOT NULL
);
INSERT INTO event_new
SELECT id,
       CASE WHEN subject_type = 'lineage' THEN 'book' ELSE subject_type END,
       CASE WHEN subject_type = 'lineage' THEN 'book' ELSE subject_id END,
       type, date, place_id, description, created_at
FROM event;
DROP TABLE event;
ALTER TABLE event_new RENAME TO event;
CREATE INDEX idx_event_subject ON event(subject_type, subject_id);

CREATE TABLE citation_new (
  id           TEXT PRIMARY KEY,
  subject_type TEXT NOT NULL CHECK (subject_type IN
                 ('person','event','union','migration','branch','lineage','book')),
  subject_id   TEXT NOT NULL,
  source_id    TEXT NOT NULL REFERENCES source(id),
  locator      TEXT,
  confidence   TEXT NOT NULL DEFAULT 'medium'
               CHECK (confidence IN ('high','medium','low','disputed')),
  note         TEXT
);
INSERT INTO citation_new
SELECT id,
       CASE WHEN subject_type = 'lineage' THEN 'book' ELSE subject_type END,
       CASE WHEN subject_type = 'lineage' THEN 'book' ELSE subject_id END,
       source_id, locator, confidence, note
FROM citation;
DROP TABLE citation;
ALTER TABLE citation_new RENAME TO citation;
CREATE INDEX idx_citation_subject ON citation(subject_type, subject_id);
