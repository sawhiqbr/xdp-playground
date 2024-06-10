import socket
import os

# Server configuration
HOST = '0.0.0.0'  # Listen on all available interfaces
PORT = 5001       # Port to listen on
RECEIVE_FOLDER = '/received_files'  # Change this to your desired folder

def start_server():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind((HOST, PORT))
        s.listen()
        print(f"Server listening on {HOST}:{PORT}")

        while True:
            conn, addr = s.accept()
            with conn:
                print(f"Connected by {addr}")
                file_count = int(conn.recv(2).decode())
                for _ in range(file_count):
                    filename = conn.recv(8).decode()
                    file_size = int(conn.recv(1024).decode())
                    file_path = os.path.join(RECEIVE_FOLDER, filename)

                    with open(file_path, 'wb') as f:
                        bytes_received = 0
                        while bytes_received < file_size:
                            data = conn.recv(1024)
                            f.write(data)
                            bytes_received += len(data)
                    print(f"Received file: {filename}")

if __name__ == "__main__":
    if not os.path.exists(RECEIVE_FOLDER):
        os.makedirs(RECEIVE_FOLDER)
    start_server()
