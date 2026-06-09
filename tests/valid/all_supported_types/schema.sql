CREATE TABLE samples (
  id INT PRIMARY KEY,
  small_count SMALLINT,
  big_count BIGINT,
  title TEXT,
  slug VARCHAR(20),
  code CHAR,
  pair CHAR(2),
  amount DECIMAL(10, 2),
  ratio FLOAT,
  score DOUBLE,
  real_value REAL,
  active BOOLEAN,
  born_on DATE,
  starts_at DATETIME,
  alarm TIME
);
