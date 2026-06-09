CREATE TABLE cart_items (
  id INT PRIMARY KEY,
  user_key TEXT,
  product_key VARCHAR(30),
  UNIQUE(user_key, product_key)
);
