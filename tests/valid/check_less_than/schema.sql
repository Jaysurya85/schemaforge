CREATE TABLE users (
  id INT PRIMARY KEY,
  age INT check (age < 100)
);
