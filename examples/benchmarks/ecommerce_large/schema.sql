CREATE TABLE users (
  id INT PRIMARY KEY,
  email TEXT NOT NULL UNIQUE,
  customer_tier TEXT NOT NULL CHECK (customer_tier IN ('standard', 'plus', 'business'))
);

CREATE TABLE products (
  id INT PRIMARY KEY,
  sku TEXT NOT NULL UNIQUE,
  price DECIMAL(12, 2) NOT NULL CHECK (price > 0.5),
  active BOOLEAN NOT NULL
);

CREATE TABLE orders (
  id INT PRIMARY KEY,
  user_id INT NOT NULL,
  order_number TEXT NOT NULL UNIQUE,
  order_status TEXT NOT NULL CHECK (order_status IN ('pending', 'paid', 'shipped')),
  total_amount DECIMAL(12, 2) NOT NULL CHECK (total_amount >= 0.5),
  FOREIGN KEY(user_id) REFERENCES users(id)
);

CREATE TABLE order_items (
  id INT PRIMARY KEY,
  order_id INT NOT NULL,
  product_id INT NOT NULL,
  quantity INT NOT NULL CHECK (quantity BETWEEN 1 AND 10),
  unit_price DECIMAL(12, 2) NOT NULL CHECK (unit_price > 0.5),
  FOREIGN KEY(order_id) REFERENCES orders(id),
  FOREIGN KEY(product_id) REFERENCES products(id)
);
