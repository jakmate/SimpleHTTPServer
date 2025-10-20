#!/usr/bin/env python3
#
#  Tests based on https://www.w3.org/Protocols/HTTP/1.0/spec.html
#

import os
import time
import http.client
import unittest
import socket

# Server configuration
SERVER_HOST = "::1" if socket.has_ipv6 else "localhost"
SERVER_PORT = 8000
TEST_DIR = "test_content"
BINARY_FILE = "test.bin"

def start_server():
    """Start the server in a separate thread (run manually before tests)"""
    os.system(f"./server &")

def create_test_files():
    """Create test directory and files"""
    os.makedirs(TEST_DIR, exist_ok=True)
    
    # Create text file
    with open(f"{TEST_DIR}/test.txt", "w", encoding="utf-8") as f:
        f.write("Hello HTTP/1.0")  # 14 characters including space
    
    # Create binary file
    with open(f"{TEST_DIR}/{BINARY_FILE}", "wb") as f:
        f.write(bytes(range(256)))
    
    # Create HTML file
    with open(f"{TEST_DIR}/test.html", "w", encoding="utf-8") as f:
        f.write("<html><body>Test</body></html>")

class TestHTTPServer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        create_test_files()
        start_server()
        time.sleep(1)  # Allow server to start

    def make_connection(self):
        """Create HTTP connection to IPv6 server"""
        return http.client.HTTPConnection(
            host=f"[{SERVER_HOST}]" if ":" in SERVER_HOST else SERVER_HOST,
            port=SERVER_PORT,
            timeout=10
        )

    # --------------------------
    # Section 3: Protocol Parameters
    # --------------------------
    def test_http_version(self):
        """Test HTTP version validation (Section 3.1)"""
        conn = self.make_connection()
        conn.request("GET", "/test.txt", headers={"Host": "test"})
        res = conn.getresponse()
        self.assertIn(res.version, (10, 11))  # Should be HTTP/1.0 or 1.1
        
        # Test invalid version
        with socket.create_connection((SERVER_HOST, SERVER_PORT)) as sock:
            sock.sendall(b"GET / HTTP/0.9\r\n\r\n")
            response = sock.recv(1024)
            self.assertNotIn(b"HTTP/1.0", response)

    # --------------------------
    # Section 5: Request Handling
    # --------------------------
    def test_get_request(self):
        """Test GET method (Section 8.1)"""
        conn = self.make_connection()
        conn.request("GET", f"/{TEST_DIR}/test.txt")
        res = conn.getresponse()
        self.assertEqual(res.status, 200)
        self.assertEqual(res.read().decode(), "Hello HTTP/1.0")

    def test_head_request(self):
        """Test HEAD method (Section 8.2)"""
        conn = self.make_connection()
        conn.request("HEAD", f"/{TEST_DIR}/test.txt")
        res = conn.getresponse()
        self.assertEqual(res.status, 200)
        self.assertEqual(res.getheader("Content-Length"), "14")
        self.assertEqual(res.read(), b"")  # No body

    def test_post_request(self):
        """Test POST method (Section 8.3)"""
        conn = self.make_connection()
        body = "Test content"
        conn.request("POST", f"/{TEST_DIR}/new.txt", body=body, 
                    headers={"Content-Length": str(len(body))})
        res = conn.getresponse()
        self.assertEqual(res.status, 201)
        
        # Verify resource creation
        with open(f"{TEST_DIR}/new.txt", encoding="utf-8") as f:
            self.assertEqual(f.read(), body)
        
        # Test Location header (Section 10.11)
        self.assertEqual(res.getheader("Location"), f"/{TEST_DIR}/new.txt")

    # --------------------------
    # Section 6: Response Handling
    # --------------------------
    def test_status_codes(self):
        """Test status code handling (Section 9)"""
        conn = self.make_connection()
        
        # 404 Not Found (Section 9.4)
        conn.request("GET", "/nonexistent.txt")
        self.assertEqual(conn.getresponse().status, 404)
        
        # 501 Not Implemented (Section 9.5)
        conn.request("PUT", "/test.txt")
        self.assertEqual(conn.getresponse().status, 501)
        
        # 411 Length Required (Section 9.4) - use raw socket
        sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
        sock.connect(('::1', 8000))
        request = b"POST /new.txt HTTP/1.1\r\nHost: [::1]:8000\r\n\r\ncontent"
        sock.send(request)
        response = sock.recv(1024).decode()
        sock.close()
        self.assertIn("411", response.split()[1])
        
        # 403 Forbidden (Section 9.4)
        conn.request("GET", "/../server.c")
        self.assertEqual(conn.getresponse().status, 403)

    # --------------------------
    # Section 7: Entity Handling
    # --------------------------
    def test_content_headers(self):
        """Test entity headers (Section 7.1)"""
        conn = self.make_connection()
        conn.request("GET", f"/{TEST_DIR}/test.txt")
        res = conn.getresponse()
        
        # Content-Type (Section 10.5)
        self.assertEqual(res.getheader("Content-Type"), "text/plain")
        
        # Content-Length (Section 10.4)
        self.assertEqual(res.getheader("Content-Length"), "14")
        
        # Last-Modified (Section 10.10)
        self.assertIsNotNone(res.getheader("Last-Modified"))

    def test_binary_content(self):
        """Test binary content handling (Section 7.2)"""
        conn = self.make_connection()
        conn.request("GET", f"/{TEST_DIR}/{BINARY_FILE}")
        res = conn.getresponse()
        
        # Verify binary integrity
        original = bytes(range(256))
        received = res.read()
        self.assertEqual(len(received), 256)
        self.assertEqual(received, original)
        
        # Content-Type (Section 3.6)
        self.assertEqual(res.getheader("Content-Type"), "application/octet-stream")

    # --------------------------
    # Section 10: Header Fields
    # --------------------------
    def test_general_headers(self):
        """Test general headers (Section 4.3)"""
        conn = self.make_connection()
        conn.request("GET", f"/{TEST_DIR}/test.txt")
        res = conn.getresponse()
        
        # Date header (Section 10.6)
        self.assertIsNotNone(res.getheader("Date"))
        
        # Server header (Section 10.14)
        self.assertEqual(res.getheader("Server"), "MyBadHTTPServer")
        
        # Connection header (Section 1.3)
        self.assertEqual(res.getheader("Connection"), "close")

    # --------------------------
    # Section 12: Security
    # --------------------------
    def test_path_traversal(self):
        """Test path traversal protection (Section 12.5)"""
        conn = self.make_connection()
        
        # Directory traversal
        conn.request("GET", "/../server.c")
        self.assertEqual(conn.getresponse().status, 403)
        
        # Absolute path
        conn.request("GET", "C:/Windows/system.ini")
        self.assertEqual(conn.getresponse().status, 403)
        
        # URL encoding bypass
        conn.request("GET", "/%2e%2e/etc/passwd")
        self.assertEqual(conn.getresponse().status, 403)
        
        # Parent directory in path
        conn.request("GET", f"/{TEST_DIR}/../../server.c")
        self.assertEqual(conn.getresponse().status, 403)

    def test_post_security(self):
        """Test POST validation (Section 12.1)"""
        # Missing Content-Length - use raw socket
        sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
        sock.connect(('::1', 8000))
        request = b"POST /test.txt HTTP/1.1\r\nHost: [::1]:8000\r\n\r\ncontent"
        sock.send(request)
        response = sock.recv(1024).decode()
        sock.close()
        self.assertIn("411", response.split()[1])
        
        conn = self.make_connection()
        
        # Invalid path
        conn.request("POST", "/invalid_dir/new.txt", 
                    body="content", headers={"Content-Length": "7"})
        self.assertEqual(conn.getresponse().status, 403)
        
        # Valid path
        conn.request("POST", f"/{TEST_DIR}/valid.txt", 
                    body="content", headers={"Content-Length": "7"})
        self.assertEqual(conn.getresponse().status, 201)

    # --------------------------
    # Section 11: Authentication
    # --------------------------
    def test_authentication(self):
        """Test authentication headers (Section 11)"""
        conn = self.make_connection()
        conn.request("GET", "/protected.txt")
        res = conn.getresponse()
        
        # 401 Unauthorized (Section 9.4)
        if res.status == 401:
            self.assertEqual(res.getheader("WWW-Authenticate"), "Basic realm=\"Protected Area\"")

    # --------------------------
    # Section 8: Method Definitions
    # --------------------------
    def test_options_method(self):
        """Test OPTIONS method (Appendix D.1)"""
        conn = self.make_connection()
        conn.request("OPTIONS", f"/{TEST_DIR}/test.txt")
        res = conn.getresponse()
        self.assertEqual(res.status, 501)  # Not implemented

    # --------------------------
    # Section 9: Status Codes
    # --------------------------
    def test_201_created(self):
        """Test 201 Created response (Section 9.2)"""
        conn = self.make_connection()
        conn.request("POST", f"/{TEST_DIR}/created.txt", 
                    body="new content", headers={"Content-Length": "11"})
        res = conn.getresponse()
        self.assertEqual(res.status, 201)
        self.assertEqual(res.getheader("Location"), f"/{TEST_DIR}/created.txt")

    # --------------------------
    # Section 10: Headers
    # --------------------------
    def test_location_header(self):
        """Test Location header (Section 10.11)"""
        # Test for 201 Created
        conn = self.make_connection()
        conn.request("POST", f"/{TEST_DIR}/location.txt", 
                    body="test", headers={"Content-Length": "4"})
        res = conn.getresponse()
        self.assertEqual(res.getheader("Location"), f"/{TEST_DIR}/location.txt")

    def test_connection_close(self):
        """Test Connection: close header (Section 1.3)"""
        conn = self.make_connection()
        conn.request("GET", f"/{TEST_DIR}/test.txt")
        res = conn.getresponse()
        self.assertEqual(res.getheader("Connection"), "close")

if __name__ == "__main__":
    # Create test content if not running in test discovery
    if not hasattr(unittest, "TestCase"):
        create_test_files()
    
    unittest.main(verbosity=2)