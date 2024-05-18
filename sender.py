import socket
import os

BUFFER_SIZE = 1024

packets = {}
packets_acked = {}


def send_udp_packet(dest_ip, port):
  sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
  sequence_number = 0
  with open(os.path.join(os.getcwd(), "objects", "small-1.obj"), 'rb') as file:
    file_data = file.read()

  chunk_size = BUFFER_SIZE - 15
  total_chunks = len(file_data) // chunk_size + 1

  for i in range(0, len(file_data), chunk_size):
    sequence_number += 1
    message = file_data[i:i+chunk_size]
    packet = "small-1".encode() + sequence_number.to_bytes(4, "big") + \
        total_chunks.to_bytes(4, "big") + message

    packets[sequence_number] = packet
    packets_acked[sequence_number] = False

  try:
    for packet in packets.values():
      sock.sendto(packet, (dest_ip, port))
  finally:
    sock.close()


port = 12345
dest_ip = '192.168.122.192'  # Guest VM IP

send_udp_packet(dest_ip, port)
