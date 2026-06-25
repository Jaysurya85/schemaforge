CREATE TABLE users (
  id INT PRIMARY KEY,
  status TEXT,
  CHECK (status = 'active' OR status = 'inactive')
);
