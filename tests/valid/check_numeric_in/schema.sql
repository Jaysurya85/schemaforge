CREATE TABLE invoices (
  id INT PRIMARY KEY,
  priority INT CHECK (priority IN (10, 20, 30))
);
