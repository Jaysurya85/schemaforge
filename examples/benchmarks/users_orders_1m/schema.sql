CREATE TABLE users (
  id INT PRIMARY KEY,
  email TEXT NOT NULL UNIQUE,
  account_status TEXT NOT NULL CHECK (account_status IN ('active', 'paused', 'closed'))
);

CREATE TABLE orders (
  id INT PRIMARY KEY,
  user_id INT NOT NULL,
  order_number TEXT NOT NULL UNIQUE,
  amount DECIMAL(12, 2) NOT NULL CHECK (amount > 0.5),
  order_status TEXT NOT NULL CHECK (order_status IN ('pending', 'paid', 'shipped')),
  FOREIGN KEY(user_id) REFERENCES users(id)
);
