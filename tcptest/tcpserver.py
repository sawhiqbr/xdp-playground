""" ---------------------- SENDER ---------------------- """
import socket
import os
import time

HOST              = 'server'
PORT              = 17000
CURRENT_DIRECTORY = os.getcwd()


def send_file(conn, filename):
    try:
        with open(filename, 'rb') as file:
            file_data = file.read()
        file_size = len(file_data)
        # Check if conn is still open
        if conn.fileno() != -1:
            conn.sendall(file_size.to_bytes(8, 'big'))  # Send file size as 8 byte integer
            conn.sendall(file_data)  # Then send the file data
    except OSError as e:
        print(f"Error sending file: {e}")


def receive_acknowledgment(conn):
    buffer = ""
    while True:
        data = conn.recv(1024).decode()
        buffer += data
        if "File received" in buffer:
            break
    # print('Received from client: File received')
    return

for i in range(30):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind((HOST, PORT + i))
        s.listen()
        conn, addr = s.accept()
        # starttime = time.time()
        with conn:
            print('Connected by', addr)
            for i in range(10):
                send_file(conn, os.path.join(CURRENT_DIRECTORY, "objects", f"large-{i}.obj"))
                receive_acknowledgment(conn)

                send_file(conn, os.path.join(CURRENT_DIRECTORY, "objects", f"small-{i}.obj"))
                receive_acknowledgment(conn)

        # print(f"Time taken: {time.time() - starttime}")
        conn.close()
        s.close()