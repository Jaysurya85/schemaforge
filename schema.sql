CREATE TABLE users (
  id INT PRIMARY KEY
);

CREATE TABLE profiles (
  id INT PRIMARY KEY,
  user_id INT,
  FOREIGN KEY(user_id) REFERENCES users(id)
);

CREATE TABLE wallets (
  id INT PRIMARY KEY,
  user_id INT,
  FOREIGN KEY(user_id) REFERENCES users(id)
);

CREATE TABLE transactions (
  id INT PRIMARY KEY,
  profile_id INT,
  wallet_id INT,
  FOREIGN KEY(profile_id) REFERENCES profiles(id),
  FOREIGN KEY(wallet_id) REFERENCES wallets(id)
);
