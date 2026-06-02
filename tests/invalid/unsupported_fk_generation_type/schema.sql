CREATE TABLE users (
  id INT PRIMARY KEY
);

CREATE TABLE orders (
  id INT PRIMARY KEY,
  user_id TEXT,
  FOREIGN KEY(user_id) REFERENCES users(id)
);
