CREATE TABLE users (
  id INT PRIMARY KEY
);

CREATE TABLE orders (
  id INT PRIMARY KEY,
  FOREIGN KEY(user_id) REFERENCES users(id)
);
