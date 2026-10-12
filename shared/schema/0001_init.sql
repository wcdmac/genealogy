-- 0001_init：家谱核心 schema（设计文档 4.2）
-- 表顺序满足外键依赖：lineage -> place/attachment -> person -> branch -> 其余
-- 注意：union 为 SQL 保留字，全程以双引号引用。

PRAGMA foreign_keys = ON;

CREATE TABLE lineage (
  id              TEXT PRIMARY KEY,
  name            TEXT NOT NULL,
  surname         TEXT NOT NULL,
  surname_origin  TEXT,
  commandery      TEXT,
  hall_name       TEXT,
  generation_poem TEXT,
  note            TEXT,
  created_at      TEXT NOT NULL,
  updated_at      TEXT NOT NULL,
  rev             INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE place (
  id              TEXT PRIMARY KEY,
  name            TEXT NOT NULL,
  type            TEXT CHECK (type IN
                  ('village','town','county','prefecture','province','grave',
                   'ancestral_hall','other')),
  modern_admin    TEXT,
  historical_name TEXT,
  lat             REAL,
  lng             REAL,
  note            TEXT
);

CREATE TABLE attachment (
  id            TEXT PRIMARY KEY,
  kind          TEXT NOT NULL CHECK (kind IN
                ('portrait','document_scan','photo','other')),
  filename      TEXT NOT NULL,
  mime          TEXT NOT NULL,
  sha256        TEXT NOT NULL,
  size_bytes    INTEGER NOT NULL,
  subject_type  TEXT NOT NULL,
  subject_id    TEXT NOT NULL,
  caption       TEXT
);

CREATE TABLE person (
  id                 TEXT PRIMARY KEY,
  lineage_id         TEXT NOT NULL REFERENCES lineage(id),
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
  father_id          TEXT REFERENCES person(id),
  mother_id          TEXT REFERENCES person(id),
  portrait_attachment_id TEXT REFERENCES attachment(id),
  note               TEXT,
  rev                INTEGER NOT NULL DEFAULT 1,
  created_at         TEXT NOT NULL,
  updated_at         TEXT NOT NULL
);

CREATE INDEX idx_person_branch     ON person(branch_id);
CREATE INDEX idx_person_generation ON person(lineage_id, generation_no);
CREATE INDEX idx_person_father     ON person(father_id);

CREATE TABLE branch (
  id                TEXT PRIMARY KEY,
  lineage_id        TEXT NOT NULL REFERENCES lineage(id),
  name              TEXT NOT NULL,
  founder_person_id TEXT REFERENCES person(id),
  parent_branch_id  TEXT REFERENCES branch(id),
  generation_char   TEXT,
  note              TEXT,
  rev               INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE parent_child (
  id          TEXT PRIMARY KEY,
  child_id    TEXT NOT NULL REFERENCES person(id),
  parent_id   TEXT NOT NULL REFERENCES person(id),
  relation    TEXT NOT NULL CHECK (relation IN
              ('biological','adoptive','foster','step','in_law_heir')),
  birth_order INTEGER,
  note        TEXT,
  UNIQUE (child_id, parent_id, relation)
);

CREATE TABLE "union" (
  id         TEXT PRIMARY KEY,
  type       TEXT NOT NULL CHECK (type IN ('marriage','concubinage','remarriage')),
  status     TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active','dissolved')),
  sequence   INTEGER,
  start_date TEXT,
  end_date   TEXT,
  place_id   TEXT REFERENCES place(id),
  note       TEXT,
  rev        INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE union_member (
  union_id  TEXT NOT NULL REFERENCES "union"(id),
  person_id TEXT NOT NULL REFERENCES person(id),
  role      TEXT NOT NULL CHECK (role IN ('husband','wife','concubine')),
  PRIMARY KEY (union_id, person_id)
);

CREATE TABLE event (
  id           TEXT PRIMARY KEY,
  subject_type TEXT NOT NULL CHECK (subject_type IN
               ('person','union','branch','lineage')),
  subject_id   TEXT NOT NULL,
  type         TEXT NOT NULL CHECK (type IN
               ('birth','death','burial','marriage','migration','examination',
                'office','honor','residence','annotation')),
  date         TEXT,
  place_id     TEXT REFERENCES place(id),
  description  TEXT,
  created_at   TEXT NOT NULL
);

CREATE INDEX idx_event_subject ON event(subject_type, subject_id);

CREATE TABLE migration (
  id            TEXT PRIMARY KEY,
  subject_type  TEXT NOT NULL CHECK (subject_type IN ('person','branch')),
  subject_id    TEXT NOT NULL,
  from_place_id TEXT REFERENCES place(id),
  to_place_id   TEXT REFERENCES place(id),
  date          TEXT,
  reason        TEXT,
  route         TEXT,
  note          TEXT
);

CREATE TABLE source (
  id         TEXT PRIMARY KEY,
  type       TEXT NOT NULL CHECK (type IN
             ('genealogy_book','local_gazetteer','stele','oral','archive',
              'photo','dna','document','other')),
  title      TEXT NOT NULL,
  author     TEXT,
  year_text  TEXT,
  repository TEXT,
  uri        TEXT,
  citation   TEXT,
  note       TEXT
);

CREATE TABLE citation (
  id           TEXT PRIMARY KEY,
  subject_type TEXT NOT NULL CHECK (subject_type IN
                 ('person','event','union','migration','branch','lineage')),
  subject_id   TEXT NOT NULL,
  source_id    TEXT NOT NULL REFERENCES source(id),
  locator      TEXT,
  confidence   TEXT NOT NULL DEFAULT 'medium'
               CHECK (confidence IN ('high','medium','low','disputed')),
  note         TEXT
);

CREATE INDEX idx_citation_subject ON citation(subject_type, subject_id);

CREATE TABLE generation_poem (
  id         TEXT PRIMARY KEY,
  owner_type TEXT NOT NULL CHECK (owner_type IN ('lineage','branch')),
  owner_id   TEXT NOT NULL,
  seq        INTEGER NOT NULL,
  char       TEXT NOT NULL,
  note       TEXT,
  UNIQUE (owner_type, owner_id, seq)
);

CREATE TABLE person_name (
  id        TEXT PRIMARY KEY,
  person_id TEXT NOT NULL REFERENCES person(id),
  name      TEXT NOT NULL,
  name_type TEXT NOT NULL CHECK (name_type IN
             ('genealogy','infant','school','renamed','posthumous')),
  note      TEXT
);

CREATE TABLE document (
  id         TEXT PRIMARY KEY,
  lineage_id TEXT NOT NULL REFERENCES lineage(id),
  type       TEXT NOT NULL CHECK (type IN
             ('preface','rule','regulation','literature','contract','edition_note')),
  title      TEXT NOT NULL,
  author     TEXT,
  date_text  TEXT,
  edition    TEXT,
  content    TEXT,
  seq        INTEGER
);

CREATE TABLE audit_log (
  id          TEXT PRIMARY KEY,
  entity_type TEXT NOT NULL,
  entity_id   TEXT NOT NULL,
  action      TEXT NOT NULL CHECK (action IN ('insert','update','delete')),
  actor       TEXT,
  at          TEXT NOT NULL,
  diff        TEXT
);

CREATE INDEX idx_audit_entity ON audit_log(entity_type, entity_id);

-- 全文检索（external content 模式，触发器保持同步）
CREATE VIRTUAL TABLE person_fts USING fts5(
  family_name, given_name, courtesy_name, art_name, biography,
  content='person', content_rowid='rowid'
);

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
