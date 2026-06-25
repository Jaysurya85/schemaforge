CREATE TABLE users (
  id INT PRIMARY KEY,
  age INT CHECK (age >= 18 AND age <= 90),
  price DECIMAL(10, 2),
  discount DECIMAL(10, 2),
  status TEXT,
  deleted BOOLEAN,
  start_date DATE,
  end_date DATE,
  min_value INT,
  max_value INT,
  CHECK (price > 0 AND discount >= 0),
  CHECK (status IN ('active', 'inactive') AND deleted = false),
  CHECK (start_date <= end_date),
  CHECK (min_value <= max_value)
);
