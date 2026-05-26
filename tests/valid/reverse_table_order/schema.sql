CREATE TABLE orders (
  id INT PRIMARY KEY,
  user_id INT NOT NULL,
  FOREIGN KEY(user_id) REFERENCES users(id)
);

CREATE TABLE users (
  id INT PRIMARY KEY,
  email TEXT UNIQUE
);
