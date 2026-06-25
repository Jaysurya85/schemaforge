CREATE TABLE regions (
  id INT PRIMARY KEY,
  country_code INT CHECK (country_code IN (1, 2)),
  region_code INT CHECK (region_code IN (1, 2)),
  UNIQUE(country_code, region_code)
);

CREATE TABLE offices (
  id INT PRIMARY KEY,
  country_code INT,
  region_code INT,
  FOREIGN KEY(country_code, region_code) REFERENCES regions(country_code, region_code)
);
