CREATE TABLE users (
  id INT PRIMARY KEY,
  age INT CHECK (age >= 18)
);
