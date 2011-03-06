-- main table
-- the idea is to provide a short nickname per portfolio and
-- some longer description that could be used to associate
-- info used by external applications
CREATE TABLE IF NOT EXISTS "aou_umpf_portfolio" (
	portfolio_id INTEGER PRIMARY KEY ASC ON CONFLICT ROLLBACK AUTOINCREMENT,
	-- nickname used to identify this portfolio externally
	short TEXT(64) NOT NULL UNIQUE ON CONFLICT ROLLBACK,
	-- description to elaborate on this pf
	description TEXT
);

-- portfolio tags
-- now the idea is to regard portfolios as the git tags of
-- a chain of orders (commits)
CREATE TABLE IF NOT EXISTS "aou_umpf_tag" (
	tag_id INTEGER PRIMARY KEY ASC ON CONFLICT ROLLBACK AUTOINCREMENT,
	portfolio_id INTEGER NOT NULL ON CONFLICT ROLLBACK,
	-- this has got to be UTC
	-- COMMENT 'time stamp of snapshot of security positions'
	tag_stamp TIMESTAMP NOT NULL ON CONFLICT ROLLBACK,
	-- this is the time when this tag has been entered
	-- into the table, this is for logging purposes only
	-- UTC stamps preferred
	-- COMMENT 'time stamp of database entry of snapshot'
	log_stamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
	FOREIGN KEY ("portfolio_id")
		REFERENCES "aou_umpf_portfolio" ("portfolio_id")
		ON DELETE CASCADE ON UPDATE CASCADE
);

-- portfolio securities
-- this is just a table to allow to elaborate on the securities
-- captured inside portfolios a bit
-- it is the whole responsibility of the caller to make sense
-- of any of these, this table exists to avoid redundance among
-- portfolio tags and must not be confused with a comprehensive
-- security database
CREATE TABLE IF NOT EXISTS "aou_umpf_security" (
	security_id INTEGER PRIMARY KEY ASC ON CONFLICT ROLLBACK AUTOINCREMENT,
	-- portfolio id, this is to allow the same security nicks
	-- across portfolios
	portfolio_id INTEGER NOT NULL ON CONFLICT ROLLBACK,
	-- nickname used to identify this security externally
	short TEXT(64) NOT NULL ON CONFLICT ROLLBACK,
	-- used to elaborate on the security if need be
	description TEXT,
	UNIQUE ("portfolio_id", "short") ON CONFLICT ROLLBACK,
	FOREIGN KEY ("portfolio_id")
		REFERENCES "aou_umpf_portfolio" ("portfolio_id")
		ON DELETE CASCADE ON UPDATE CASCADE
);

-- portfolio positions
-- to actually capture whats inside a portfolio
-- securities are referenced from a different table and must
-- not be confused with a contract database
-- fact table
CREATE TABLE IF NOT EXISTS "aou_umpf_position" (
	tag_id INTEGER NOT NULL ON CONFLICT ROLLBACK,
	security_id INTEGER NOT NULL ON CONFLICT ROLLBACK,
	long_qty DECIMAL(18,9),
	short_qty DECIMAL(18,9),
	PRIMARY KEY ("tag_id", "security_id") ON CONFLICT ROLLBACK,
	FOREIGN KEY ("tag_id")
		REFERENCES "aou_umpf_tag" ("tag_id")
		ON DELETE CASCADE ON UPDATE CASCADE,
	FOREIGN KEY ("security_id")
		REFERENCES "aou_umpf_security" ("security_id")
		ON DELETE CASCADE ON UPDATE CASCADE
);

-- last portfolio
-- keeps track of the last tag in chronological order
CREATE TABLE IF NOT EXISTS "aou_umpf_last" (
	portfolio_id INTEGER NOT NULL ON CONFLICT ROLLBACK,
	tag_id INTEGER NOT NULL ON CONFLICT ROLLBACK,
	UNIQUE ("portfolio_id", "tag_id") ON CONFLICT ROLLBACK,
	FOREIGN KEY ("portfolio_id")
		REFERENCES "aou_umpf_portfolio" ("portfolio_id")
		ON DELETE CASCADE ON UPDATE CASCADE,
	FOREIGN KEY ("tag_id")
		REFERENCES "aou_umpf_tag" ("tag_id")
		ON DELETE CASCADE ON UPDATE CASCADE
);
