CREATE TABLE users (
  id INT,
  email TEXT UNIQUE
);

CREATE TABLE orders (
  id INT PRIMARY KEY,
  user_id INT,
  FOREIGN KEY(user_id) REFERENCES users(id)
);
