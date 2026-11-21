-- 0007_single_book：单谱单库定位收口（2026-11-21）。
--
-- 一个数据库文件只承载一部家谱：lineage 维度整体移除。
--   · lineage 表 → book 表（单行，谱自身的名/姓/堂号/派语元数据），
--     固定 id 'book'；generation_poem 的谱级 owner 归一到 book；
--   · person/branch/document 重建，去掉 lineage_id 列（列上有 FK
--     引用 lineage 且有索引，DROP COLUMN 不可行，按官方文档走表重建）；
--   · person_fts / document_fts 为 external content——表重建后 rowid
--     变化，末尾 rebuild 追平索引；
--   · 联网时代的服务端表（audit_log / idempotency_cache）一并移除。
--
-- 存量库升级：老库的 lineage 行内容会迁入 book（首行）；空库自动
-- 得到空 book 行（由迁移末尾 INSERT 确保单行存在）。

CREATE TABLE book (
  id              TEXT PRIMARY KEY CHECK (id = 'book'),
  name            TEXT NOT NULL DEFAULT '',
  surname         TEXT NOT NULL DEFAULT '',
  surname_origin  TEXT,
  commandery      TEXT,
  hall_name       TEXT,
  generation_poem TEXT,
  note            TEXT,
  created_at      TEXT NOT NULL,
  updated_at      TEXT NOT NULL,
  rev             INTEGER NOT NULL DEFAULT 1
);

-- ---- book 行建立：取 lineage 首行元数据（老库升级路径）；新库由
-- ---- 末尾的 INSERT 兜底建空行。
INSERT INTO book (id, name, surname, surname_origin, commandery, hall_name, generation_poem, note, created_at, updated_at, rev)
SELECT 'book', name, surname, surname_origin, commandery, hall_name, generation_poem, note, created_at, updated_at, rev
FROM lineage LIMIT 1;

INSERT INTO book (id, name, surname, surname_origin, commandery, hall_name, generation_poem, note, created_at, updated_at, rev)
SELECT 'book', '', '', NULL, NULL, NULL, NULL, NULL,
       strftime('%Y-%m-%dT%H:%M:%SZ','now'),
       strftime('%Y-%m-%dT%H:%M:%SZ','now'), 1
WHERE NOT EXISTS (SELECT 1 FROM book);

UPDATE generation_poem SET owner_id = 'book' WHERE owner_type = 'lineage';

-- ---- person 重建（去 lineage_id；列序保持其余不变）----
CREATE TABLE person_new (
  id                 TEXT PRIMARY KEY,
  branch_id          TEXT REFERENCES branch(id),
  family_name        TEXT NOT NULL,
  given_name         TEXT NOT NULL,
  courtesy_name      TEXT,
  art_name           TEXT,
  posthumous_title   TEXT,
  alias              TEXT,
  gender             TEXT NOT NULL CHECK (gender IN ('male','female','unknown')),
  birth_order        INTEGER,
  generation_no      INTEGER,
  is_heir            INTEGER NOT NULL DEFAULT 0,
  is_living          INTEGER NOT NULL DEFAULT 0,
  privacy_level      TEXT NOT NULL DEFAULT 'kin'
                     CHECK (privacy_level IN ('public','kin','private')),
  birth_date         TEXT NOT NULL,
  death_date         TEXT,
  birth_place_id     TEXT REFERENCES place(id),
  death_place_id     TEXT REFERENCES place(id),
  burial_place_id    TEXT REFERENCES place(id),
  biography          TEXT,
  father_id          TEXT REFERENCES person_new(id),
  mother_id          TEXT REFERENCES person_new(id),
  portrait_attachment_id TEXT REFERENCES attachment(id),
  note               TEXT,
  rev                INTEGER NOT NULL DEFAULT 1,
  created_at         TEXT NOT NULL,
  updated_at         TEXT NOT NULL
);

INSERT INTO person_new
SELECT id, branch_id, family_name, given_name, courtesy_name, art_name,
       posthumous_title, alias, gender, birth_order, generation_no,
       is_heir, is_living, privacy_level, birth_date, death_date,
       birth_place_id, death_place_id, burial_place_id, biography,
       father_id, mother_id, portrait_attachment_id, note, rev,
       created_at, updated_at
FROM person;

DROP TABLE person;
ALTER TABLE person_new RENAME TO person;

DROP INDEX IF EXISTS idx_person_branch;
DROP INDEX IF EXISTS idx_person_generation;
DROP INDEX IF EXISTS idx_person_father;
DROP INDEX IF EXISTS idx_person_mother;
CREATE INDEX idx_person_branch ON person(branch_id);
CREATE INDEX idx_person_generation ON person(generation_no);
CREATE INDEX idx_person_father ON person(father_id);
CREATE INDEX idx_person_mother ON person(mother_id);

-- ---- branch 重建（去 lineage_id）----
CREATE TABLE branch_new (
  id                TEXT PRIMARY KEY,
  name              TEXT NOT NULL,
  founder_person_id TEXT REFERENCES person(id),
  parent_branch_id  TEXT REFERENCES branch_new(id),
  generation_char   TEXT,
  note              TEXT,
  rev               INTEGER NOT NULL DEFAULT 1
);

INSERT INTO branch_new
SELECT id, name, founder_person_id, parent_branch_id, generation_char, note, rev
FROM branch;

DROP TABLE branch;
ALTER TABLE branch_new RENAME TO branch;

-- ---- document 重建（去 lineage_id；document_fts external content
-- ---- 引用 document 表，重建期间触发器随表删除，末尾 rebuild 追平）----
CREATE TABLE document_new (
  id         TEXT PRIMARY KEY,
  type       TEXT NOT NULL CHECK (type IN
             ('preface','rule','regulation','literature','contract','edition_note')),
  title      TEXT NOT NULL,
  author     TEXT,
  date_text  TEXT,
  edition    TEXT,
  content    TEXT,
  seq        INTEGER
);

INSERT INTO document_new
SELECT id, type, title, author, date_text, edition, content, seq
FROM document;

DROP TABLE document;
ALTER TABLE document_new RENAME TO document;

-- ---- 联网时代的服务端表移除 ----
DROP TABLE IF EXISTS audit_log;
DROP TABLE IF EXISTS idempotency_cache;

-- ---- lineage 表最后删（此前 FK 引用已随列移除）----
DROP TABLE lineage;

-- document 谱内索引重建（idx_document_lineage 随旧表删除，无需重建——
-- 单谱单库下 document 全库即一谱）

-- ---- FTS 触发器重建（DROP TABLE 时随表消失）----
CREATE TRIGGER person_fts_ai AFTER INSERT ON person BEGIN
  INSERT INTO person_fts(rowid, family_name, given_name, courtesy_name, art_name, biography)
  VALUES (new.rowid, new.family_name, new.given_name, new.courtesy_name, new.art_name, new.biography);
END;
CREATE TRIGGER person_fts_ad AFTER DELETE ON person BEGIN
  INSERT INTO person_fts(person_fts, rowid, family_name, given_name, courtesy_name, art_name, biography)
  VALUES ('delete', old.rowid, old.family_name, old.given_name, old.courtesy_name, old.art_name, old.biography);
END;
CREATE TRIGGER person_fts_au AFTER UPDATE ON person BEGIN
  INSERT INTO person_fts(person_fts, rowid, family_name, given_name, courtesy_name, art_name, biography)
  VALUES ('delete', old.rowid, old.family_name, old.given_name, old.courtesy_name, old.art_name, old.biography);
  INSERT INTO person_fts(rowid, family_name, given_name, courtesy_name, art_name, biography)
  VALUES (new.rowid, new.family_name, new.given_name, new.courtesy_name, new.art_name, new.biography);
END;
CREATE TRIGGER document_fts_ai AFTER INSERT ON document BEGIN
  INSERT INTO document_fts(rowid, title, author, content)
  VALUES (new.rowid, new.title, new.author, new.content);
END;
CREATE TRIGGER document_fts_ad AFTER DELETE ON document BEGIN
  INSERT INTO document_fts(document_fts, rowid, title, author, content)
  VALUES ('delete', old.rowid, old.title, old.author, old.content);
END;
CREATE TRIGGER document_fts_au AFTER UPDATE ON document BEGIN
  INSERT INTO document_fts(document_fts, rowid, title, author, content)
  VALUES ('delete', old.rowid, old.title, old.author, old.content);
  INSERT INTO document_fts(rowid, title, author, content)
  VALUES (new.rowid, new.title, new.author, new.content);
END;

-- ---- FTS 追平（external content：person/document 重建后 rowid 变化）----
INSERT INTO person_fts(person_fts) VALUES ('rebuild');
INSERT INTO document_fts(document_fts) VALUES ('rebuild');
