PRAGMA application_id = 0x0C13FE56;
PRAGMA user_version = 1;

-- Per connection pragmas
-- PRAGMA foreign_keys = ON;

-- Protocol log
CREATE TABLE log_servers( -- Servers are keyed by hostname and port number
    id INTEGER PRIMARY KEY,
    host TEXT NOT NULL, -- Normalize the host part of the URL
    port INTEGER NOT NULL,
    UNIQUE(host, port)
) STRICT;

CREATE TABLE log_sessions( -- Record when each WebSockets connection was initiated
    id INTEGER PRIMARY KEY,
    server_id INTEGER NOT NULL,
    connect_posix_ms INTEGER NOT NULL,
    FOREIGN KEY(server_id) REFERENCES log_servers(id) ON DELETE RESTRICT
) STRICT;

CREATE TABLE log_wsmessages( -- Raw log of WebSockets text transmissions
    id INTEGER PRIMARY KEY,
    session_id INTEGER NOT NULL,
    time_posix_ms INTEGER NOT NULL,
    direction INTEGER NOT NULL, -- Rx=0, Tx=1, others reserved
    kind INTEGER NOT NULL, -- WsText=0, others reserved
    msg ANY, -- kind=WsText => TEXT NOT NULL, others reserved
    FOREIGN KEY(session_id) REFERENCES log_sessions(id) ON DELETE RESTRICT
) STRICT;

-- User data
CREATE TABLE user_dictionary( -- The translation dictionary
    signal INTEGER PRIMARY KEY,
    archived INTEGER NOT NULL, -- Soft deletion with recovery
    descr TEXT NOT NULL,
    fmt_before INTEGER NOT NULL,
    fmt_after INTEGER NOT NULL,
    fmt_break_on_double INTEGER NOT NULL
) STRICT;

CREATE TABLE user_translations( -- A historical ID to word association in the dictionary
    signal INTEGER NOT NULL,
    revision INTEGER NOT NULL,
    mtime_posix_ms INTEGER NOT NULL, -- Use to display modification time
    word TEXT NOT NULL,
    PRIMARY KEY(signal, revision),
    FOREIGN KEY(signal) REFERENCES user_dictionary(signal) ON DELETE RESTRICT
) STRICT, WITHOUT ROWID;

CREATE TABLE user_settings( -- Key-value store for trivial app settings
    key TEXT NOT NULL PRIMARY KEY,
    val ANY
) STRICT;

-- Indices
CREATE INDEX idx_log_sessions_server_id ON log_sessions(server_id); -- prepGetOrInsertServer FK check
CREATE INDEX idx_log_wsmessages_session_id ON log_wsmessages(session_id); -- prepInsertAndGetSession FK check
