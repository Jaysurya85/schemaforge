CREATE TABLE users (
  id INT PRIMARY KEY,
  age INT CHECK (age IN (18, 19)),
  status TEXT CHECK (status IN ('active', 'inactive')),
  UNIQUE(age, status)
);
