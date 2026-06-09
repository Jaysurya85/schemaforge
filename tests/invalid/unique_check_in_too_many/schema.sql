CREATE TABLE users (
  id INT PRIMARY KEY,
  status TEXT UNIQUE CHECK (status IN ('active', 'inactive'))
);
