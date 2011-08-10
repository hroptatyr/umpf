changequote
changequote(`[', `]')

define([N], [`]$1[`])
define([ASC], [])
define([ON_CONFLICT], [])
define([ROLLBACK], [])
define([AUTOINCREMENT], [AUTO_INCREMENT])
define([UNIQUE], [[UNIQUE KEY]])
define([TEXT], [dnl
ifelse([$1], [], [dnl
MEDIUMTEXT CHARSET utf8 COLLATE utf8_bin dnl
], [dnl
VARCHAR($1) CHARSET ascii COLLATE ascii_bin dnl
])])
define([POST], [ENGINE InnoDB CHARSET ascii COLLATE ascii_bin])

dnl mysql.m4 ends here
