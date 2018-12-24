#!/usr/bin/python3

# apt-get install python3-pip
# pip3 install minio aiohttp cchardet aiodns

import aiohttp.web
import argparse
import configparser
import minio
import minio.sse
import re
import syslog
import zlib


def readOptions(filename, app):
    s = "[piler]\n" + open(filename, 'r').read()

    config = configparser.ConfigParser()
    config.read_string(s)

    app['s3_host'] = config['piler']['s3_host']
    app['s3_key'] = bytes(config['piler']['s3_key'], 'ascii')
    app['s3_access_key'] = config['piler']['s3_access_key']
    app['s3_secret_key'] = config['piler']['s3_secret_key']

    app['bucket'] = 'piler'


async def indexpage(request):
    return aiohttp.web.Response(text="Hello world!")


async def get_attachment(request):
    name = request.match_info.get('name')
    result = get_object(request, name)
    return aiohttp.web.Response(text=result)


async def get_message(request):
    name = request.match_info.get('name')
    result = get_object(request, name)

    # ATTACHMENT_POINTER_400000005c20f03c38ffaeb4000d95cb6b46.a1_XXX_PILER
    pointers = re.findall(r'ATTACHMENT_POINTER_[0-9a-f]{36}\.a[0-9]{,2}_XXX_PILER', result, re.M)
    for pointer in pointers:
        ptr_content = get_object(request, pointer[19:-10])
        result = result.replace(pointer, ptr_content)

    return aiohttp.web.Response(text=result)


def get_object(request, name):
    result = 'ERROR'
    content = bytes('', 'ascii')

    sse = minio.sse.SSE_C(request.app['s3_key'])

    try:
        data = request.app['mc'].get_object(request.app['bucket'], name, sse=sse)
        for d in data.stream(32*1024):
            content = content + d
    except minio.error.ResponseError as err:
        syslog.syslog(err)
    except minio.error.NoSuchKey as err:
        syslog.syslog(err)

    if content:
        result = zlib.decompress(content).decode('utf-8')

    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-c", "--config", type=str, help="piler.conf path",
                        default="/etc/piler/piler.conf")
    parser.add_argument("--host", type=str, default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)

    args = parser.parse_args()

    syslog.openlog(logoption=syslog.LOG_PID, facility=syslog.LOG_MAIL)

    app = aiohttp.web.Application()
    readOptions(args.config, app)

    app.add_routes([aiohttp.web.get('/', indexpage),
                    aiohttp.web.get('/m/{name}', get_message),
                    aiohttp.web.get('/a/{name}', get_attachment)])

    app['mc'] = minio.Minio(app['s3_host'],
                            access_key=app['s3_access_key'],
                            secret_key=app['s3_secret_key'],
                            secure=True)

    aiohttp.web.run_app(app, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
