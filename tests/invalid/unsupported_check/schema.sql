CREATE TABLE users (
  id INT PRIMARY KEY,
  age INT,
  score INT,
  CHECK (age + score > 100)
);
