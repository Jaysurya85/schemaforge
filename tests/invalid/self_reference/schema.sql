CREATE TABLE employees (
  id INT PRIMARY KEY,
  manager_id INT,
  FOREIGN KEY(manager_id) REFERENCES employees(id)
);
