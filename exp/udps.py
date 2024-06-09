import socket

def send_packet(ip, port, message):
    # Create a UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    try:
        # Send data
        print(f"sending {message}")
        sock.sendto(message.encode(), (ip, port))
    finally:
        print('closing socket')
        sock.close()

# Use the function
send_packet('172.22.96.1', 1111, 'Hello, World!')