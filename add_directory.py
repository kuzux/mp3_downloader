#!/usr/bin/env python

import sqlite3
import sys
import glob
import os

path = sys.argv[1]

paths = glob.glob(path + "*.mp3")

conn = sqlite3.connect("server.db")

c = conn.cursor()

c.execute("CREATE TABLE IF NOT EXISTS users(username TEXT PRIMARY KEY, password TEXT)")
c.execute("CREATE TABLE IF NOT EXISTS files(mid INTEGER PRIMARY KEY ASC, name TEXT, path TEXT);")

conn.commit()

for path in paths:
    name = os.path.splitext(os.path.basename(path))[0]
    c.execute("INSERT INTO files(name, path) VALUES(?,?)", [name, path])

conn.commit()
