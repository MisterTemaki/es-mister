#!/usr/bin/env python3
"""Loopback-only behavioral regressions. No MiSTer devices or user files are accessed."""
import argparse
import http.server
import os
import pathlib
import ssl
import subprocess
import tempfile
import threading
import zipfile

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == '/missing':
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b'not-found')
        elif self.path == '/partial':
            self.send_response(200)
            self.send_header('Content-Length', '100')
            self.end_headers()
            self.wfile.write(b'short')
            self.close_connection = True
        else:
            self.send_response(200)
            self.send_header('Content-Length', '11')
            self.end_headers()
            self.wfile.write(b'new-content')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('executable', type=pathlib.Path)
    args = parser.parse_args()
    binary = str(args.executable.resolve())
    failures = 0
    with tempfile.TemporaryDirectory(prefix='es-audit-') as temp:
        work = pathlib.Path(temp)
        httpd = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        threading.Thread(target=httpd.serve_forever, daemon=True).start()
        cert, key = work/'cert.pem', work/'key.pem'
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                        '-keyout', str(key), '-out', str(cert), '-days', '1',
                        '-subj', '/CN=localhost', '-addext', 'subjectAltName=IP:127.0.0.1'],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        httpsd = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        httpsd.socket = context.wrap_socket(httpsd.socket, server_side=True)
        threading.Thread(target=httpsd.serve_forever, daemon=True).start()
        url = 'http://127.0.0.1:%d' % httpd.server_port
        tls = 'https://127.0.0.1:%d/' % httpsd.server_port
        cp437, utf8, nested, unsafe = [work/name for name in ('cp437.zip', 'utf8.zip', 'nested.zip', 'unsafe.zip')]
        with zipfile.ZipFile(cp437, 'w') as archive:
            archive.writestr('a.txt', 'abc')
        cp437.write_bytes(cp437.read_bytes().replace(b'a.txt', b'\x82.txt'))
        with zipfile.ZipFile(utf8, 'w') as archive:
            archive.writestr('é.txt', 'abc')
        with zipfile.ZipFile(nested, 'w', zipfile.ZIP_DEFLATED) as archive:
            archive.writestr('nested/file.txt', 'abc')
        with zipfile.ZipFile(unsafe, 'w') as archive:
            for name in ('../escape.txt', 'dir/../../escape2.txt', '/absolute.txt',
                         'C:/drive.txt', 'dir\\..\\escape3.txt', 'link/escape.txt'):
                archive.writestr(name, 'abc')
        extract = work/'extract'
        extract.mkdir()
        outside = work/'outside'
        outside.mkdir()
        (extract/'link').symlink_to(outside, target_is_directory=True)
        cases = [
            ('header', url), ('header-reset', url), ('custom-headers', url),
            ('write-count', url), ('download-preserve', url),
            ('flush-error', url), ('download-ok', url),
            ('download-fail', url+'/missing'), ('download-fail', url+'/partial'),
            ('tls-reject', tls), ('tls-trusted', tls, str(cert)), ('zip-list', str(cp437)),
            ('zip-reload', str(cp437), str(utf8)),
            ('zip-traversal', str(unsafe), str(extract)),
            ('zip-nested', str(nested), str(extract)),
        ]
        rename_source = work/'rename-source.bin'
        rename_source.write_text('new-content')
        cases.append(('rename-contracts', str(rename_source)))
        env = dict(os.environ, ES_TEST_HOME=temp, NO_PROXY='127.0.0.1', no_proxy='127.0.0.1',
                   ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        for index, case in enumerate(cases):
            destination = work/('download-%d.bin' % index)
            destination.write_text('original')
            command = [binary, *case]
            if not case[0].startswith('zip-'):
                command.append(str(destination))
            result = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, timeout=20)
            print('CASE', index+1, *case, 'exit', result.returncode)
            print(result.stdout, end='')
            failures += result.returncode != 0
        # On Linux /dev/shm is a distinct filesystem: force a real EXDEV failure.
        if pathlib.Path('/dev/shm').is_dir() and os.stat(work).st_dev != os.stat('/dev/shm').st_dev:
            with tempfile.TemporaryDirectory(prefix='es-audit-', dir='/dev/shm') as other:
                source = pathlib.Path(other)/'source.bin'
                source.write_text('new-content')
                destination = work/'cross-filesystem.bin'
                destination.write_text('original')
                result = subprocess.run([binary, 'rename-fail', str(source), str(destination)],
                                        env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        text=True, timeout=20)
                print('CASE cross-filesystem rename exit', result.returncode)
                print(result.stdout, end='')
                failures += result.returncode != 0
                cases.append(('rename-fail',))
        httpd.shutdown()
        httpsd.shutdown()
    print('%d/%d cases passed' % (len(cases)-failures, len(cases)))
    return bool(failures)

if __name__ == '__main__':
    raise SystemExit(main())
