CREATE TABLE users (
  id INT PRIMARY KEY,
  email TEXT UNIQUE
);

CREATE TABLE orders (
  id INT PRIMARY KEY,
  user_id INT NOT NULL,
  FOREIGN KEY(user_id) REFERENCES users(id)
);

CREATE TABLE order_items (
  id INT PRIMARY KEY,
  order_id INT NOT NULL,
  quantity INT,
  FOREIGN KEY(order_id) REFERENCES orders(id)
);
