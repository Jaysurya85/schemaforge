CREATE TABLE countries (
  code CHAR(2) PRIMARY KEY
);

CREATE TABLE users (
  id INT PRIMARY KEY,
  country_code CHAR(2),
  FOREIGN KEY(country_code) REFERENCES countries(code)
);
