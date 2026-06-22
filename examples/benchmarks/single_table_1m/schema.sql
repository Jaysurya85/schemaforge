CREATE TABLE users (
  id INT PRIMARY KEY,
  email TEXT NOT NULL UNIQUE,
  age INT NOT NULL CHECK (age >= 18),
  account_balance DECIMAL(12, 2) NOT NULL CHECK (account_balance >= 0.5),
  active BOOLEAN NOT NULL
);
