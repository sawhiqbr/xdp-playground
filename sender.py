import socket


def send_udp_packet(dest_ip, port, message):
  sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
  try:
    sock.sendto(message.encode(), (dest_ip, port))
    print(f"Sent message: {message}")
  finally:
    sock.close()


def wait_for_ack(port):
  sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
  sock.bind(('0.0.0.0', port))
  print("Waiting for ack")

  try:
    data, addr = sock.recvfrom(1024)
    print(f"Received: {data.decode()} from {addr}")
  except Exception as e:
    print(e)
    return False
  return True


port = 12345
dest_ip = '192.168.122.192'  # Guest VM IP

message = 'WOAH Hello from HOST MACHINE!'
send_udp_packet(dest_ip, port, message)

if (wait_for_ack(port)):
  print("ACK received")
