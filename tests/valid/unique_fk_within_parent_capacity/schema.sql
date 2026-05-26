CREATE TABLE users (
  id INT PRIMARY KEY,
  email TEXT UNIQUE
);

CREATE TABLE profiles (
  id INT PRIMARY KEY,
  user_id INT UNIQUE,
  FOREIGN KEY(user_id) REFERENCES users(id)
);
