CREATE TABLE users (
  id INT PRIMARY KEY,
  first_name TEXT NOT NULL,
  last_name TEXT NOT NULL,
  email VARCHAR(80) UNIQUE NOT NULL,
  phone TEXT,
  age INT CHECK (age BETWEEN 18 AND 80),
  status TEXT CHECK (status IN ('active', 'inactive', 'pending')),
  middle_name TEXT,
  city TEXT,
  state TEXT,
  postal_code TEXT,
  account_balance DECIMAL(10, 2),
  created_at DATETIME
);

CREATE TABLE messages (
  id INT PRIMARY KEY,
  user_email VARCHAR(80),
  notes TEXT,
  FOREIGN KEY(user_email) REFERENCES users(email)
);
