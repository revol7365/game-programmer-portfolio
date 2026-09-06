CREATE DATABASE IF NOT EXISTS portfolio_verification CHARACTER SET utf8mb4;
USE portfolio_verification;
CREATE TABLE IF NOT EXISTS account (
    accountno BIGINT PRIMARY KEY,
    userid VARCHAR(19) NOT NULL,
    usernick VARCHAR(19) NOT NULL
);
INSERT INTO account VALUES
(100000,'probe100000','local100000'),(100001,'probe100001','local100001'),
(100002,'probe100002','local100002'),(100003,'probe100003','local100003'),
(100004,'probe100004','local100004'),(100005,'probe100005','local100005'),
(100006,'probe100006','local100006'),(100007,'probe100007','local100007'),
(100008,'probe100008','local100008'),(100009,'probe100009','local100009'),
(100010,'probe100010','local100010'),(100011,'probe100011','local100011'),
(100012,'probe100012','local100012'),(100013,'probe100013','local100013'),
(100014,'probe100014','local100014'),(100015,'probe100015','local100015')
ON DUPLICATE KEY UPDATE userid=VALUES(userid), usernick=VALUES(usernick);
