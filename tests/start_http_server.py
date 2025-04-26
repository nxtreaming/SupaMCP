#!/usr/bin/env python3
"""
Start HTTP Server Script for SupaMCP

This script starts the HTTP server for testing the HTTP protocol implementation.
"""

import os
import sys
import subprocess
import time
import argparse
import signal
import platform

def find_http_server_executable():
    """Find the HTTP server executable"""
    # Check if we're in the tests directory
    if os.path.basename(os.getcwd()) == "tests":
        # Go up one level
        os.chdir("..")
    
    # Check for Windows executable
    if platform.system() == "Windows":
        server_path = os.path.join("bin", "http_server.exe")
        if os.path.exists(server_path):
            return server_path
    
    # Check for Linux/macOS executable
    server_path = os.path.join("bin", "http_server")
    if os.path.exists(server_path):
        return server_path
    
    # Check in examples directory
    if platform.system() == "Windows":
        server_path = os.path.join("examples", "bin", "http_server.exe")
        if os.path.exists(server_path):
            return server_path
    else:
        server_path = os.path.join("examples", "bin", "http_server")
        if os.path.exists(server_path):
            return server_path
    
    return None

def start_server(port=8280, doc_root=None, enable_cors=True):
    """Start the HTTP server"""
    server_path = find_http_server_executable()
    if not server_path:
        print("Error: Could not find HTTP server executable")
        return None
    
    print(f"Starting HTTP server from: {server_path}")
    
    # Build command
    cmd = [server_path, "--port", str(port)]
    
    if doc_root:
        cmd.extend(["--doc-root", doc_root])
    
    if enable_cors:
        cmd.append("--enable-cors")
    
    # Start the server
    try:
        process = subprocess.Popen(cmd)
        print(f"HTTP server started with PID {process.pid}")
        
        # Wait a moment for the server to start
        time.sleep(2)
        
        return process
    except Exception as e:
        print(f"Error starting HTTP server: {e}")
        return None

def main():
    parser = argparse.ArgumentParser(description='Start the HTTP server for testing')
    parser.add_argument('--port', type=int, default=8280, help='Port to listen on (default: 8280)')
    parser.add_argument('--doc-root', help='Document root for static files')
    parser.add_argument('--no-cors', action='store_true', help='Disable CORS support')
    args = parser.parse_args()
    
    # Start the server
    server_process = start_server(
        port=args.port,
        doc_root=args.doc_root,
        enable_cors=not args.no_cors
    )
    
    if not server_process:
        sys.exit(1)
    
    print("Server is running. Press Ctrl+C to stop.")
    
    try:
        # Wait for the server to exit
        server_process.wait()
    except KeyboardInterrupt:
        print("Stopping server...")
        if platform.system() == "Windows":
            server_process.terminate()
        else:
            server_process.send_signal(signal.SIGTERM)
        
        # Wait for the server to exit
        try:
            server_process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            print("Server did not exit gracefully, forcing termination")
            server_process.kill()

if __name__ == "__main__":
    main()
