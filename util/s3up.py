#!/usr/bin/python3

import argparse
import configparser
import minio
import minio.sse
import os
import sys
import syslog
import time

opts = {}


def read_options(filename="", opts={}):
    s = "[piler]\n" + open(filename, 'r').read()

    config = configparser.ConfigParser()
    config.read_string(s)

    opts['storedir'] = config['piler']['queuedir']
    opts['s3_host'] = config['piler']['s3_host']
    opts['s3_key'] = bytes(config['piler']['s3_key'], 'ascii')
    opts['s3_access_key'] = config['piler']['s3_access_key']
    opts['s3_secret_key'] = config['piler']['s3_secret_key']

    opts['server_id'] = "%02x" % int(config['piler']['server_id'])


def createBucket(mc):
    try:
        mc.make_bucket(opts['bucket'], location=opts['region'])
    except minio.error.BucketAlreadyOwnedByYou as err:
        pass
    except minio.error.BucketAlreadyExists as err:
        pass
    except minio.error.ResponseError as err:
        raise


def uploadFile(mc, filename):
    sse = minio.sse.SSE_C(opts['s3_key'])

    try:
        with open(filename, 'rb') as f:
            st = os.stat(filename)
            s = mc.put_object(opts['bucket'], filename, f, st.st_size, sse=sse)
            syslog.syslog("Uploaded " + filename)
    except minio.error.ResponseError as err:
        print(err)

    return s


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-c", "--config", type=str, help="piler.conf path",
                        default="/etc/piler/piler.conf")
    parser.add_argument("--region", type=str, default="us-east-1")

    args = parser.parse_args()

    syslog.openlog(logoption=syslog.LOG_PID, facility=syslog.LOG_MAIL)

    read_options(args.config, opts)

    opts['bucket'] = ''
    opts['region'] = args.region

    try:
        pid = os.fork()
        if pid > 0:
            sys.exit(0)
    except OSError as e:
        print("fork failed", e)
        sys.exit(1)

    storedir = opts['storedir'] + '/' + opts['server_id']

    os.chdir(storedir)

    mc = minio.Minio(opts['s3_host'],
                     access_key=opts['s3_access_key'],
                     secret_key=opts['s3_secret_key'],
                     secure=True)

    while True:
        files = os.listdir(storedir)
        for f in files:
            bucket = f[8:11]

            if opts['bucket'] != bucket:
                opts['bucket'] = bucket
                createBucket(mc)

            if os.path.isfile(f):
                uploadFile(mc, f)
                os.unlink(f)

        time.sleep(1)


if __name__ == "__main__":
    main()
