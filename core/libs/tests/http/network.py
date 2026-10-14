import gzip
import http.server
import os
from pathlib import Path
import shutil
import ssl
import subprocess
import sys
import tempfile
import threading
import time


pending = set()
pending_lock = threading.Lock()


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def finish_request(self, request, client_address):
        request.settimeout(3)
        try:
            if self.tls_context:
                request = self.tls_context.wrap_socket(request, server_side=True)
            with request:
                self.RequestHandlerClass(request, client_address, self)
        except (TimeoutError, ConnectionError, ssl.SSLError):
            request.close()


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass

    def handle(self):
        try:
            super().handle()
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
            pass

    def response(self, status, body, headers=()):
        self.send_response(status)
        self.send_header("Content-Length", str(len(body)))
        for name, value in headers:
            self.send_header(name, value)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def do_HEAD(self):
        self.do_GET()

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        self.response(200, self.rfile.read(length))

    def do_GET(self):
        try:
            if self.path == "/binary":
                self.response(200, b"abc\0tail", [("X-Test", "local")])
            elif self.path == "/final":
                self.response(200, b"final")
            elif self.path == "/missing":
                self.response(404, b"missing")
            elif self.path == "/disconnect":
                self.close_connection = True
            elif self.path == "/redirect":
                self.response(302, b"", [("Location", "/final")])
            elif self.path == "/headers":
                body = "|".join([self.headers.get("X-Inherited", ""), ",".join(self.headers.get_all("X-Value", [])), self.headers.get("User-Agent", "")])
                self.response(200, body.encode())
            elif self.path == "/credentials":
                body = self.headers.get("Authorization") or self.headers.get("Proxy-Authorization") or self.headers.get("Cookie", "")
                self.response(200, body.encode())
            elif self.path == "/largeheaders":
                self.response(200, b"", [("X-Large", "x" * 4096)])
            elif self.path == "/gzip":
                self.response(200, gzip.compress(b"compressed body"), [("Content-Encoding", "gzip")])
            elif self.path == "/chunked":
                self.send_response(200)
                self.send_header("Transfer-Encoding", "chunked")
                self.end_headers()
                self.wfile.write(b"5\r\nfirst\r\n6\r\nsecond\r\n0\r\n\r\n")
            elif self.path.startswith("/state/"):
                with pending_lock:
                    seen = self.path.rsplit("/", 1)[1] in pending
                self.response(200, b"1" if seen else b"0")
            elif self.path.startswith("/slowheaders/"):
                with pending_lock:
                    pending.add(self.path.rsplit("/", 1)[1])
                time.sleep(2)
                self.response(200, b"late")
            elif self.path.startswith("/slowbody/"):
                self.send_response(200)
                self.send_header("Content-Length", "1")
                self.end_headers()
                self.wfile.flush()
                time.sleep(2)
                self.wfile.write(b"x")
            else:
                self.response(404, b"unexpected path")
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
            pass


def openssl_command():
    openssl = shutil.which("openssl")
    if not openssl:
        git = shutil.which("git")
        candidate = Path(git).resolve().parent.parent / "usr" / "bin" / "openssl.exe" if git else None
        if candidate and candidate.is_file():
            openssl = str(candidate)
    return openssl


def certificate(folder, openssl):
    try:
        subprocess.run([openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-sha256",
                        "-days", "1", "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
                        "-keyout", str(folder / "localhost-key.pem"), "-out", str(folder / "localhost.pem")],
                       check=True, capture_output=True, timeout=10)
    except subprocess.CalledProcessError as error:
        sys.stderr.write(error.stderr.decode(errors="replace"))
        raise


def main():
    openssl = openssl_command()
    if not openssl:
        print("guest_http_network: skipped, OpenSSL is unavailable")
        return 77
    temporary = tempfile.TemporaryDirectory(prefix="anyps5-http-")
    folder = Path(temporary.name)
    servers = []
    threads = []
    try:
        certificate(folder, openssl)
        for secure in (False, True):
            server = Server(("127.0.0.1", 0), Handler)
            server.tls_context = None
            if secure:
                context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                context.maximum_version = ssl.TLSVersion.TLSv1_2
                context.load_cert_chain(folder / "localhost.pem", folder / "localhost-key.pem")
                server.tls_context = context
            servers.append(server)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            threads.append(thread)
        environment = os.environ.copy()
        environment["APS5_HTTP_NETWORK"] = "1"
        result = subprocess.run([sys.argv[1], f"http://127.0.0.1:{servers[0].server_port}", f"https://127.0.0.1:{servers[1].server_port}"], env=environment, timeout=15)
        return 0 if result.returncode == 0 else 1
    finally:
        for server in servers:
            server.shutdown()
            server.server_close()
        for thread in threads:
            thread.join(timeout=1)
        temporary.cleanup()


if __name__ == "__main__":
    sys.exit(main())
