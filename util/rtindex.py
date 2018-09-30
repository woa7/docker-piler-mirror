#!/usr/bin/python

import ConfigParser
import MySQLdb as dbapi
import StringIO
import argparse
import getpass
import os
import sys
import syslog
import time

SQL_SELECT_INDEX_QUERY = "SELECT id, `from`, fromdomain, `to`, todomain, subject, body, attachment_types, size, direction, attachments, sent FROM sph_index ORDER BY id ASC"
SQL_INSERT_QUERY = "INSERT INTO rt1 VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)"
SQL_DELETE_QUERY = "DELETE FROM sph_index WHERE id IN (%s)"
SLEEP_DELAY = 5

opts = {}


def read_options(filename="", opts={}):
    syslog.syslog("Reading %s" % (filename))

    s = "[piler]\n" + open(filename, 'r').read()
    fp = StringIO.StringIO(s)
    config = ConfigParser.RawConfigParser()
    config.readfp(fp)

    opts['username'] = config.get('piler', 'mysqluser')
    opts['password'] = config.get('piler', 'mysqlpwd')
    opts['database'] = config.get('piler', 'mysqldb')


def process_batch(opts={}):
    try:
        opts['db'] = dbapi.connect("localhost", opts['username'],
                                   opts['password'], opts['database'])

        cursor = opts['db'].cursor()
        cursor.execute(SQL_SELECT_INDEX_QUERY)

        while True:
            rows = cursor.fetchmany(opts['batch_size'])
            if rows == ():
                time.sleep(SLEEP_DELAY)
                break

            ids = [x[0] for x in rows]

            # Push data to sphinx
            opts['sphx'] = dbapi.connect(host=opts['sphinx'], port=opts['port'])
            sphx_cursor = opts['sphx'].cursor()
            sphx_cursor.executemany(SQL_INSERT_QUERY, rows)
            opts['sphx'].commit()
            opts['sphx'].close()

            syslog.syslog("%d record inserted" % (sphx_cursor.rowcount))

            # Delete rows from sph_index table
            format = ", ".join(['%s'] * len(ids))
            cursor.execute(SQL_DELETE_QUERY % (format), ids)
            opts['db'].commit()

    except dbapi.DatabaseError, e:
        print "Error %s" % e
        syslog.syslog("Error %s" % e) 
        time.sleep(SLEEP_DELAY)

    if opts['db']:
        opts['db'].close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-c", "--config", type=str, help="piler.conf path",
                        default="/etc/piler/piler.conf")
    parser.add_argument("-b", "--batch-size", type=int, help="batch size " +
                        "to delete", default=1000)
    parser.add_argument("-s", "--sphinx", type=str, help="sphinx server",
                        default="127.0.0.1")
    parser.add_argument("-p", "--port", type=int, help="sphinx sql port",
                        default=9306)
    parser.add_argument("-d", "--dry-run", help="dry run", action='store_true')
    parser.add_argument("-v", "--verbose", help="verbose mode",
                        action='store_true')

    args = parser.parse_args()

    if getpass.getuser() not in ['root', 'piler']:
        print "Please run me as user 'piler'"
        sys.exit(1)

    opts['sphinx'] = args.sphinx
    opts['port'] = args.port
    opts['dry_run'] = args.dry_run
    opts['verbose'] = args.verbose
    opts['batch_size'] = args.batch_size
    opts['db'] = None
    opts['sphx'] = None

    syslog.openlog(logoption=syslog.LOG_PID, facility=syslog.LOG_MAIL)

    read_options(args.config, opts)

    while True:
        process_batch(opts)


if __name__ == "__main__":
    main()
