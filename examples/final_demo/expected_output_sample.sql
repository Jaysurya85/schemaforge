-- Deterministic excerpt from:
-- ./build/schemaforge generate --config examples/final_demo/schemaforge.yaml
-- The complete generated output is ignored at examples/final_demo/output.sql.
INSERT INTO "users" ("id", "email", "first_name", "last_name", "status", "created_at") VALUES (1, 'olivia.thompson+1@sample.org', 'Olivia', 'Thompson', 'active', '2026-01-01 00:00:00');
INSERT INTO "users" ("id", "email", "first_name", "last_name", "status", "created_at") VALUES (2, 'harper.smith+2@example.com', 'Harper', 'Smith', 'inactive', '2026-01-01 00:00:01');
INSERT INTO "products" ("id", "seller_id", "sku", "name", "price", "active") VALUES (1, 1, 'SKU-100000', 'Premium Camera', 0.50, true);
INSERT INTO "orders" ("id", "user_id", "order_number", "status", "total_amount", "coupon_code", "created_at") VALUES (1, 2, 'order_number_1', 'pending', 0.50, NULL, '2026-01-01 00:00:00');
INSERT INTO "order_items" ("order_id", "line_number", "product_sku", "seller_id", "quantity", "unit_price") VALUES (1, 1, 'SKU-100000', 1, 1, 0.50);
INSERT INTO "payments" ("id", "order_id", "payment_method", "amount", "successful", "external_reference", "paid_at") VALUES (1, 8, 'card', 0.50, true, NULL, '2025-03-23 20:38:50');
