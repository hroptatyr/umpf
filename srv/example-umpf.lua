-- umpf example config

-- whether pfd runs as background process, default false
-- daemonise = true;

-- whether ipv6 multicast is preferred in s2s communication
prefer_ipv6 = true;

-- the pfd module configuration
load_module(
{
	-- path to the daemon dso
	file = "/usr/local/lib/unserding/dso-umpf.la",
	-- unix domain socket to register
	-- in the future %u for user and %p for pid will be allowed
	sock = "/tmp/.s.umpf",
	-- tcp socket port to listen to
	port = 8642,
});
