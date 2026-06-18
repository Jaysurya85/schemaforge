CREATE TABLE memberships (
  user_id INT,
  team_id INT CHECK (team_id IN (1, 2, 3)),
  PRIMARY KEY(user_id, team_id)
);
