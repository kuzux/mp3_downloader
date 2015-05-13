#!/usr/bin/env python

import sqlite3
import sys

username = sys.argv[1]
password = sys.argv[2]

conn = sqlite3.connect("server.db")

c = conn.cursor()

c.execute("CREATE TABLE IF NOT EXISTS users(username TEXT PRIMARY KEY, password TEXT)")
c.execute("CREATE TABLE IF NOT EXISTS files(mid INTEGER PRIMARY KEY ASC, name TEXT, path TEXT);")

conn.commit()

c.execute("INSERT INTO users VALUES(?, ?)", [username, password])

conn.commit()

conn.close()
