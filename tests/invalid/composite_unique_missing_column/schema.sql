CREATE TABLE cart_items (
  id INT PRIMARY KEY,
  user_id INT,
  UNIQUE(user_id, product_id)
);
