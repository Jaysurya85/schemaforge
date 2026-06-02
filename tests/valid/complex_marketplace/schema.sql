CREATE TABLE countries (
  id INT PRIMARY KEY,
  name TEXT NOT NULL,
  iso_code TEXT UNIQUE
);

CREATE TABLE currencies (
  id INT PRIMARY KEY,
  code TEXT UNIQUE,
  name TEXT NOT NULL
);

CREATE TABLE regions (
  id INT PRIMARY KEY,
  country_id INT NOT NULL,
  name TEXT NOT NULL,
  FOREIGN KEY(country_id) REFERENCES countries(id)
);

CREATE TABLE cities (
  id INT PRIMARY KEY,
  region_id INT NOT NULL,
  name TEXT NOT NULL,
  FOREIGN KEY(region_id) REFERENCES regions(id)
);

CREATE TABLE warehouses (
  id INT PRIMARY KEY,
  city_id INT NOT NULL,
  name TEXT NOT NULL,
  active BOOLEAN,
  FOREIGN KEY(city_id) REFERENCES cities(id)
);

CREATE TABLE customers (
  id INT PRIMARY KEY,
  city_id INT NOT NULL,
  email TEXT UNIQUE,
  first_name TEXT NOT NULL,
  last_name TEXT NOT NULL,
  FOREIGN KEY(city_id) REFERENCES cities(id)
);

CREATE TABLE merchants (
  id INT PRIMARY KEY,
  city_id INT NOT NULL,
  name TEXT NOT NULL,
  support_email TEXT UNIQUE,
  FOREIGN KEY(city_id) REFERENCES cities(id)
);

CREATE TABLE departments (
  id INT PRIMARY KEY,
  merchant_id INT NOT NULL,
  name TEXT NOT NULL,
  FOREIGN KEY(merchant_id) REFERENCES merchants(id)
);

CREATE TABLE categories (
  id INT PRIMARY KEY,
  department_id INT NOT NULL,
  name TEXT NOT NULL,
  FOREIGN KEY(department_id) REFERENCES departments(id)
);

CREATE TABLE products (
  id INT PRIMARY KEY,
  merchant_id INT NOT NULL,
  category_id INT NOT NULL,
  sku TEXT UNIQUE,
  name TEXT NOT NULL,
  price DECIMAL,
  active BOOLEAN,
  FOREIGN KEY(merchant_id) REFERENCES merchants(id),
  FOREIGN KEY(category_id) REFERENCES categories(id)
);

CREATE TABLE inventory_batches (
  id INT PRIMARY KEY,
  product_id INT NOT NULL,
  warehouse_id INT NOT NULL,
  quantity INT,
  unit_cost DECIMAL,
  FOREIGN KEY(product_id) REFERENCES products(id),
  FOREIGN KEY(warehouse_id) REFERENCES warehouses(id)
);

CREATE TABLE orders (
  id INT PRIMARY KEY,
  customer_id INT NOT NULL,
  billing_city_id INT NOT NULL,
  shipping_city_id INT NOT NULL,
  currency_id INT NOT NULL,
  order_number TEXT UNIQUE,
  total_amount DECIMAL,
  FOREIGN KEY(customer_id) REFERENCES customers(id),
  FOREIGN KEY(billing_city_id) REFERENCES cities(id),
  FOREIGN KEY(shipping_city_id) REFERENCES cities(id),
  FOREIGN KEY(currency_id) REFERENCES currencies(id)
);

CREATE TABLE shipments (
  id INT PRIMARY KEY,
  order_id INT NOT NULL,
  warehouse_id INT NOT NULL,
  destination_city_id INT NOT NULL,
  tracking_number TEXT UNIQUE,
  delivered BOOLEAN,
  FOREIGN KEY(order_id) REFERENCES orders(id),
  FOREIGN KEY(warehouse_id) REFERENCES warehouses(id),
  FOREIGN KEY(destination_city_id) REFERENCES cities(id)
);

CREATE TABLE order_items (
  id INT PRIMARY KEY,
  order_id INT NOT NULL,
  product_id INT NOT NULL,
  shipment_id INT NOT NULL,
  quantity INT,
  unit_price DECIMAL,
  FOREIGN KEY(order_id) REFERENCES orders(id),
  FOREIGN KEY(product_id) REFERENCES products(id),
  FOREIGN KEY(shipment_id) REFERENCES shipments(id)
);

CREATE TABLE payments (
  id INT PRIMARY KEY,
  order_id INT NOT NULL,
  currency_id INT NOT NULL,
  amount DECIMAL,
  successful BOOLEAN,
  provider_reference TEXT UNIQUE,
  FOREIGN KEY(order_id) REFERENCES orders(id),
  FOREIGN KEY(currency_id) REFERENCES currencies(id)
);

CREATE TABLE reviews (
  id INT PRIMARY KEY,
  customer_id INT NOT NULL,
  product_id INT NOT NULL,
  order_item_id INT NOT NULL,
  rating INT,
  body TEXT,
  FOREIGN KEY(customer_id) REFERENCES customers(id),
  FOREIGN KEY(product_id) REFERENCES products(id),
  FOREIGN KEY(order_item_id) REFERENCES order_items(id)
);
