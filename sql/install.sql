-- main table
-- the idea is to provide a short nickname per portfolio and
-- some longer description that could be used to associate
-- info used by external applications
CREATE TABLE IF NOT EXISTS `aou_pfd_portfolio` (
	portfolio_id INTEGER AUTO_INCREMENT PRIMARY KEY,
	-- nickname used to identify this portfolio externally
	short VARCHAR(64) CHARSET ASCII NOT NULL,
	-- description to elaborate on this pf
	description TEXT,
	UNIQUE KEY (`short`)
) ENGINE InnoDB CHARSET utf8 COLLATE utf8_unicode_ci;

-- portfolio tags
-- now the idea is to regard portfolios as the git tags of
-- a chain of orders (commits)
CREATE TABLE IF NOT EXISTS `aou_pfd_tag` (
	tag_id INTEGER AUTO_INCREMENT PRIMARY KEY,
	portfolio_id INTEGER NOT NULL,
	-- this has got to be UTC
	tag_stamp TIMESTAMP DEFAULT 0 NOT NULL
		COMMENT 'time stamp of snapshot of security positions',
	-- this is the time when this tag has been entered
	-- into the table, this is for logging purposes only
	-- UTC stamps preferred
	log_stamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP
		ON UPDATE CURRENT_TIMESTAMP
		COMMENT 'time stamp of database entry of snapshot',
	FOREIGN KEY (`portfolio_id`)
		REFERENCES `aou_pfd_portfolio` (`portfolio_id`)
		ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE InnoDB CHARSET ASCII;

-- portfolio securities
-- this is just a table to allow to elaborate on the securities
-- captured inside portfolios a bit
-- it is the whole responsibility of the caller to make sense
-- of any of these, this table exists to avoid redundance among
-- portfolio tags and must not be confused with a comprehensive
-- security database
CREATE TABLE IF NOT EXISTS `aou_pfd_security` (
	security_id INTEGER AUTO_INCREMENT PRIMARY KEY,
	-- portfolio id, this is to allow the same security nicks
	-- across portfolios
	portfolio_id INTEGER NOT NULL,
	-- nickname used to identify this security externally
	short VARCHAR(64) CHARSET ASCII NOT NULL,
	-- used to elaborate on the security if need be
	description TEXT,
	UNIQUE KEY (`portfolio_id`, `short`),
	FOREIGN KEY (`portfolio_id`)
		REFERENCES `aou_pfd_portfolio` (`portfolio_id`)
		ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE InnoDB CHARSET utf8 COLLATE utf8_unicode_ci;

-- portfolio positions
-- to actually capture whats inside a portfolio
-- securities are referenced from a different table and must
-- not be confused with a contract database
-- fact table
CREATE TABLE IF NOT EXISTS `aou_pfd_position` (
	tag_id INTEGER NOT NULL,
	security_id INTEGER NOT NULL,
	position DECIMAL(18,9),
	PRIMARY KEY (`tag_id`, `security_id`),
	FOREIGN KEY (`tag_id`)
		REFERENCES `aou_pfd_tag` (`tag_id`)
		ON DELETE CASCADE ON UPDATE CASCADE,
	FOREIGN KEY (`security_id`)
		REFERENCES `aou_pfd_security` (`security_id`)
		ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE InnoDB CHARSET ASCII;

-- just some getters
DROP FUNCTION IF EXISTS `aou_pfd_get_security`;
DELIMITER $$
CREATE FUNCTION `aou_pfd_get_security` (`short` VARCHAR(64))
RETURNS INTEGER
DETERMINISTIC READS SQL DATA
COMMENT 'Return the security_id assigned to SHORT'
BEGIN
	DECLARE res INTEGER DEFAULT 0;
	SELECT `security_id` INTO res
		FROM `aou_pfd_security`
		WHERE `aou_pfd_security`.`short` = `short`
	LIMIT 1;
	RETURN res;
END;
$$
DELIMITER ;
