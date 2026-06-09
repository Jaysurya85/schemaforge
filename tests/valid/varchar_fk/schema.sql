CREATE TABLE products (
  sku VARCHAR(30) PRIMARY KEY,
  name TEXT
);

CREATE TABLE order_items (
  id INT PRIMARY KEY,
  product_sku VARCHAR(30),
  FOREIGN KEY(product_sku) REFERENCES products(sku)
);
