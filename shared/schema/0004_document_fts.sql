-- M7.4：document 全文检索（ROADMAP 7.4「全文检索纳入 FTS」，验收门
-- 「文档可按正文检索命中」）。
--
-- tokenizer 用 trigram（SQLite ≥3.34，本项目 3.53.4）：person_fts 用
-- 默认 unicode61——连续 CJK 归为一个 token，中文只能整词命中；文档正文
-- 是大段文言，「星堂」搜「德星堂」不中不可接受。trigram 三字滑窗天然
-- 支持中文子串检索。
-- 代价：MATCH 查询不足 3 个字符时不命中——repository 层对 1-2 字查询
-- 回退 LIKE（文档量级几百篇，全扫在 50ms 门槛内）。
CREATE INDEX idx_document_lineage ON document(lineage_id);

CREATE VIRTUAL TABLE document_fts USING fts5(
  title, author, content,
  content='document', content_rowid='rowid',
  tokenize='trigram'
);

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

-- 存量行回填：老库升上来时 FTS 追平现有 document；空表 rebuild 无害，
-- 语句幂等（重复执行只是重建索引内容）。
INSERT INTO document_fts(document_fts) VALUES ('rebuild');
