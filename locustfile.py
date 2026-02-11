"""
Locust load testing script for TCP server on port 2323.

Usage:
    locust -f locustfile.py --host=localhost:2323
    
Or with web UI:
    locust -f locustfile.py --host=localhost:2323 --web-host=0.0.0.0 --web-port=8089
"""

import socket
import time
import logging
from locust import User, task, between, events

logger = logging.getLogger(__name__)


class TCPClient:
    """Simple TCP client for connecting to the server."""
    
    def __init__(self, host, port):
        self.host = host
        self.port = port
        
    def connect_and_receive(self):
        """Connect to server, receive response, and close connection."""
        start_time = time.time()
        response = None
        error = None
        
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(5)
            
            # Connect
            sock.connect((self.host, self.port))
            
            # Receive response
            response = sock.recv(256).decode('utf-8')
            
            # Calculate response time
            response_time = (time.time() - start_time) * 1000
            
            sock.close()
            
            return response_time, response, None
            
        except socket.timeout:
            error = "Timeout"
            response_time = (time.time() - start_time) * 1000
            return response_time, None, error
            
        except Exception as e:
            error = str(e)
            response_time = (time.time() - start_time) * 1000
            return response_time, None, error
        
        finally:
            try:
                sock.close()
            except:
                pass


class TCPUser(User):
    """Locust user that simulates TCP connections to the server."""
    
    # Wait between 1 and 3 seconds between tasks
    wait_time = between(1, 3)
    
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        
        # Parse host and port from environment.host
        if ':' in self.host:
            host, port = self.host.rsplit(':', 1)
            self.tcp_host = host
            self.tcp_port = int(port)
        else:
            self.tcp_host = self.host
            self.tcp_port = 2323
            
        self.client = TCPClient(self.tcp_host, self.tcp_port)
    
    @task
    def connect_to_server(self):
        """Task: Connect to server and receive connection info."""
        response_time, response, error = self.client.connect_and_receive()
        
        if error:
            logger.error(f"Error connecting to server: {error}")
            events.request.fire(
                request_type="TCP",
                name="connect",
                response_time=response_time,
                response_length=0,
                exception=Exception(error),
                context={}
            )
        else:
            logger.info(f"Server response: {response}")
            events.request.fire(
                request_type="TCP",
                name="connect",
                response_time=response_time,
                response_length=len(response) if response else 0,
                exception=None,
                context={}
            )


# For headless mode testing
@events.test_start.add_listener
def on_test_start(environment, **kwargs):
    print(f"Load test starting against {environment.host}")
    print(f"Expected server response format: IP/Port information")


@events.test_stop.add_listener
def on_test_stop(environment, **kwargs):
    print("Load test completed")
