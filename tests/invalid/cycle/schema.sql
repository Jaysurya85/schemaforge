CREATE TABLE teams (
  id INT PRIMARY KEY,
  default_user_id INT,
  FOREIGN KEY(default_user_id) REFERENCES users(id)
);

CREATE TABLE users (
  id INT PRIMARY KEY,
  team_id INT,
  FOREIGN KEY(team_id) REFERENCES teams(id)
);
