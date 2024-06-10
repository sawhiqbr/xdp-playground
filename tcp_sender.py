import socket
import os

# Client configuration
SERVER_IP = '192.168.122.192'  # Change this to the receiver's IP address
PORT = 5001
SEND_FOLDER = '/objects'  # Change this to your folder containing the files

def send_files():
    files = [f for f in os.listdir(SEND_FOLDER) if os.path.isfile(os.path.join(SEND_FOLDER, f))]
    file_count = len(files)
    
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((SERVER_IP, PORT))
        s.sendall(str(file_count).encode())

        for file_name in files:
            file_path = os.path.join(SEND_FOLDER, file_name)
            file_size = os.path.getsize(file_path)
            
            s.sendall(file_name.encode())
            s.sendall(str(file_size).encode())

            with open(file_path, 'rb') as f:
                data = f.read()
                s.sendall(data)
            print(f"Sent file: {file_name}")

if __name__ == "__main__":
    send_files()
