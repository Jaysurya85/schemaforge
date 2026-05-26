CREATE TABLE users (
  id TEXT PRIMARY KEY
);

CREATE TABLE orders (
  id INT PRIMARY KEY,
  user_id INT,
  FOREIGN KEY(user_id) REFERENCES users(id)
);
