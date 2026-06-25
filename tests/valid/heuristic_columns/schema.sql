CREATE TABLE users (
  id INT PRIMARY KEY,
  email TEXT UNIQUE NOT NULL,
  first_name TEXT NOT NULL,
  last_name TEXT NOT NULL,
  username TEXT UNIQUE NOT NULL,
  phone TEXT,
  price DECIMAL(10, 2),
  amount DECIMAL(10, 2),
  status TEXT,
  checked_status TEXT CHECK (checked_status IN ('queued', 'done')),
  created_at DATETIME,
  is_active BOOLEAN,
  optional_phone TEXT
);

CREATE TABLE messages (
  id INT PRIMARY KEY,
  user_email TEXT,
  FOREIGN KEY(user_email) REFERENCES users(email)
);

CREATE TABLE typed_precedence (
  id INT PRIMARY KEY,
  email INT
);
