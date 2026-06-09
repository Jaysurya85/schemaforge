CREATE TABLE users (
  id TEXT PRIMARY KEY,
  name TEXT
);

CREATE TABLE orders (
  id INT PRIMARY KEY,
  user_id TEXT,
  FOREIGN KEY(user_id) REFERENCES users(id)
);
