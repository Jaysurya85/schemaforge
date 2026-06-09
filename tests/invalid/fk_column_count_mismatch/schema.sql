CREATE TABLE users (
  id INT PRIMARY KEY
);

CREATE TABLE orders (
  id INT PRIMARY KEY,
  user_id INT,
  tenant_id INT,
  FOREIGN KEY(user_id, tenant_id) REFERENCES users(id)
);
