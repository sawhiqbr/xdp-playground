import socket
import os

BUFFER_SIZE = 1024
ACK_BUFFER_SIZE = 4

packets = {}
packets_acked = {}


def send_udp_packet(dest_ip, port):
  sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
  ack_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
  ack_sock.bind(('0.0.0.0', 12345))  # Bind to the port to listen for ACKs

  sequence_number = 0
  with open(os.path.join(os.getcwd(), "objects", "small-1.obj"), 'rb') as file:
    file_data = file.read()

  chunk_size = BUFFER_SIZE - 15  # this is where the -16 problem stems from
  total_chunks = len(file_data) // chunk_size + 1

  for i in range(0, len(file_data), chunk_size):
    sequence_number += 1
    message = file_data[i:i + chunk_size]
    packet = "small-1".encode() + b'\x00' + sequence_number.to_bytes(4, "big") + \
             total_chunks.to_bytes(4, "big") + message

    packets[sequence_number] = packet
    packets_acked[sequence_number] = False

  try:
    for seq_num, packet in packets.items():
      sock.sendto(packet, (dest_ip, port))
      print(f"Sent packet {seq_num}")

      ack, _ = ack_sock.recvfrom(ACK_BUFFER_SIZE)
      ack_sequence_number = int.from_bytes(ack, 'big')
      if ack_sequence_number in packets_acked:
        packets_acked[ack_sequence_number] = True
        print(f"Received ACK for packet {ack_sequence_number}")

  finally:
    sock.close()
    ack_sock.close()


port = 12345
dest_ip = '192.168.122.192'  # Guest VM IP

send_udp_packet(dest_ip, port)
