CREATE TABLE users (
  id INT PRIMARY KEY,
  born_on DATE UNIQUE
);

CREATE TABLE orders (
  id INT PRIMARY KEY,
  user_born_on DATE,
  FOREIGN KEY(user_born_on) REFERENCES users(born_on)
);
