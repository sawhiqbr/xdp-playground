import socket

# Configuration
listen_ip = "192.168.122.192"  # IP of VM1
listen_port = 12345  # Same port as the sender

# Create UDP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# Bind socket to address
sock.bind((listen_ip, listen_port))

print(f"Listening on {listen_ip}:{listen_port}")

while True:
    data, addr = sock.recvfrom(1024)  # Buffer size is 1024 bytes
    print(f"Received message: {data} from {addr}")
