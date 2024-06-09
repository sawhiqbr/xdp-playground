import socket

def udp_listener(port):
    # Create a UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    # Bind the socket to the port
    server_address = ('', port)
    print(f'starting up on port {port}')
    sock.bind(server_address)

    while True:
        print('\nwaiting to receive message')
        data, address = sock.recvfrom(4096)

        print(f'received {len(data)} bytes from {address}')
        print(data)

# Use the function
udp_listener(1111)