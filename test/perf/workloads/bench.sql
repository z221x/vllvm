-- sqlite 性能负载：纯 CPU（:memory: 库），递归 CTE 造数 + 索引 + 聚合扫描。
CREATE TABLE t(id INTEGER PRIMARY KEY, a INTEGER, b REAL, c TEXT);
WITH RECURSIVE cnt(i) AS (
  SELECT 1 UNION ALL SELECT i + 1 FROM cnt WHERE i < 1200000
)
INSERT INTO t(a, b, c)
SELECT i % 997, sqrt(i * 1.5), hex(i % 65536) || substr('abcdef', 1 + (i % 6), 4)
FROM cnt;

-- VM 循环纯计算（不经存储层）
WITH RECURSIVE c2(i) AS (
  SELECT 1 UNION ALL SELECT i + 1 FROM c2 WHERE i < 3000000
)
SELECT printf('%d', sum((i * 2654435761) % 1000003)) FROM c2;

CREATE INDEX t_a ON t(a);
CREATE INDEX t_c ON t(c);

SELECT count(*), sum(a), printf('%.6f', avg(b)) FROM t;
SELECT a % 10, count(*), printf('%.4f', sum(b)) FROM t GROUP BY a % 10 ORDER BY 2 DESC;
SELECT count(*) FROM t WHERE c LIKE '%ab%' AND b > 100.0;
SELECT printf('%.6f', sum(b)) FROM t WHERE a BETWEEN 100 AND 500;
SELECT count(*), max(length(c)) FROM t ORDER BY a LIMIT 1200000;
SELECT printf('%d|%s|%.4f', t.id, t.c, t.b)
FROM t JOIN t t2 ON t2.id = t.id + 1 WHERE t.id % 7 = 0 LIMIT 20000;
