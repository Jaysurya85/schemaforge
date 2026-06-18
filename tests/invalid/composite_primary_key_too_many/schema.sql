CREATE TABLE memberships (
  user_id INT CHECK (user_id IN (1, 2)),
  team_id INT CHECK (team_id IN (1, 2)),
  PRIMARY KEY(user_id, team_id)
);
