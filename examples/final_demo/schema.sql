CREATE TABLE users (
  id INT PRIMARY KEY,
  email VARCHAR(80) NOT NULL UNIQUE,
  first_name TEXT NOT NULL,
  last_name TEXT NOT NULL,
  status TEXT NOT NULL CHECK (status IN ('active', 'inactive', 'pending')),
  created_at DATETIME NOT NULL
);

CREATE TABLE products (
  id INT PRIMARY KEY,
  seller_id INT NOT NULL,
  sku VARCHAR(40) NOT NULL,
  name TEXT NOT NULL,
  price DECIMAL(10, 2) NOT NULL CHECK (price >= 0.5),
  active BOOLEAN NOT NULL,
  UNIQUE(sku, seller_id)
);

CREATE TABLE orders (
  id INT PRIMARY KEY,
  user_id INT NOT NULL,
  order_number TEXT NOT NULL UNIQUE,
  status TEXT NOT NULL CHECK (status IN ('pending', 'paid', 'shipped')),
  total_amount DECIMAL(10, 2) NOT NULL CHECK (total_amount >= 0.5),
  coupon_code VARCHAR(20),
  created_at DATETIME NOT NULL,
  FOREIGN KEY(user_id) REFERENCES users(id)
);

CREATE TABLE order_items (
  order_id INT NOT NULL,
  line_number INT NOT NULL CHECK (line_number BETWEEN 1 AND 5),
  product_sku VARCHAR(40) NOT NULL,
  seller_id INT NOT NULL,
  quantity INT NOT NULL CHECK (quantity BETWEEN 1 AND 5),
  unit_price DECIMAL(10, 2) NOT NULL CHECK (unit_price >= 0.5),
  PRIMARY KEY(order_id, line_number),
  FOREIGN KEY(order_id) REFERENCES orders(id),
  FOREIGN KEY(product_sku, seller_id) REFERENCES products(sku, seller_id)
);

CREATE TABLE payments (
  id INT PRIMARY KEY,
  order_id INT NOT NULL,
  payment_method TEXT NOT NULL CHECK (payment_method IN ('card', 'paypal', 'wire')),
  amount DECIMAL(10, 2) NOT NULL CHECK (amount >= 0.5),
  successful BOOLEAN NOT NULL,
  external_reference TEXT,
  paid_at DATETIME,
  FOREIGN KEY(order_id) REFERENCES orders(id)
);
