CREATE TABLE payments (
  id INT PRIMARY KEY,
  amount DECIMAL(10, 2) CHECK (amount >= 0.5)
);
