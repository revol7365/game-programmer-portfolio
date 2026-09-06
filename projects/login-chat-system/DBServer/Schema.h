#pragma once
/*
--gamedb 생성
CREATE DATABASE IF NOT EXISTS `gamedb` DEFAULT CHARACTER SET utf8mb4;
USE `gamedb`;

--플레이어 테이블
CREATE TABLE IF NOT EXISTS `player` (
    `account_no`    BIGINT       NOT NULL,
    `level`         INT          NOT NULL DEFAULT 1,
    `money`         INT          NOT NULL DEFAULT 0,
    PRIMARY KEY(`account_no`)
    ) ENGINE = InnoDB;

--퀘스트 완료 테이블
CREATE TABLE IF NOT EXISTS `quest_complete` (
    `idx`           BIGINT       NOT NULL AUTO_INCREMENT,
    `account_no`    BIGINT       NOT NULL,
    `quest_id`      INT          NOT NULL,
    `complete_date` DATETIME     NOT NULL,
    PRIMARY KEY(`idx`),
        INDEX `idx_account` (`account_no`)
            ) ENGINE = InnoDB;

--인벤토리 테이블
CREATE TABLE IF NOT EXISTS `inventory` (
    `idx`           BIGINT       NOT NULL AUTO_INCREMENT,
    `account_no`    BIGINT       NOT NULL,
    `item_id`       INT          NOT NULL,
    `slot`          INT          NOT NULL,
    `price`         INT          NOT NULL DEFAULT 0,
    PRIMARY KEY(`idx`),
        INDEX `idx_account_slot` (`account_no`, `slot`)
            ) ENGINE = InnoDB;

--테스트용 플레이어 데이터 삽입
INSERT IGNORE INTO `player` (`account_no`, `level`, `money`) VALUES
(10000, 1, 100000),
(10001, 1, 100000),
(10002, 1, 100000),
(10003, 1, 100000),
(10004, 1, 100000);
*/