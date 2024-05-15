import socket

def start_udp_server(listen_ip, listen_port, ack_message, ack_dest_ip, ack_dest_port):
    # Set up the UDP socket for receiving
    recv_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    recv_sock.bind((listen_ip, listen_port))
    print(f"Listening for UDP packets on {listen_ip}:{listen_port}")

    while True:
        try:
            # Receive data from sender
            data, sender_addr = recv_sock.recvfrom(1024)
            print(f"Received message: {data.decode()} from {sender_addr}")

            # Prepare to send ACK
            ack_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            ack_sock.sendto(ack_message.encode(), (ack_dest_ip, ack_dest_port))
            print(f"Sent ACK: {ack_message} to {ack_dest_ip}:{ack_dest_port}")
            ack_sock.close()

        except Exception as e:
            print(f"Error: {e}")
            break

    recv_sock.close()

# Parameters
listen_ip = '0.0.0.0'
listen_port = 12345
ack_message = 'ACK from VM'
ack_dest_ip = '144.122.137.79'  # IP of the host machine
ack_dest_port = 12345

# Start the UDP server
start_udp_server(listen_ip, listen_port, ack_message, ack_dest_ip, ack_dest_port)
