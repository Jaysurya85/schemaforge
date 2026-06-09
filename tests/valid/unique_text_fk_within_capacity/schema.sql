CREATE TABLE users (
  email TEXT PRIMARY KEY,
  name TEXT
);

CREATE TABLE orders (
  id INT PRIMARY KEY,
  user_email TEXT UNIQUE,
  FOREIGN KEY(user_email) REFERENCES users(email)
);
