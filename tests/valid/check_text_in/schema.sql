CREATE TABLE invoices (
  id INT PRIMARY KEY,
  status TEXT CHECK (status IN ('pending', 'paid', 'cancelled'))
);
