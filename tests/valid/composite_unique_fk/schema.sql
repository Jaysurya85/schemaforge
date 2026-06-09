CREATE TABLE users (
  id INT PRIMARY KEY
);

CREATE TABLE products (
  id INT PRIMARY KEY
);

CREATE TABLE order_items (
  id INT PRIMARY KEY,
  user_id INT,
  product_id INT,
  FOREIGN KEY(user_id) REFERENCES users(id),
  FOREIGN KEY(product_id) REFERENCES products(id),
  UNIQUE(user_id, product_id)
);
