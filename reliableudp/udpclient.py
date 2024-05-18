""" --------------------- RECEIVER --------------------- """
""" Please comment your code thoroughly, on what your implementation does rather than how. """
import socket
import hashlib
import threading
import os
import time
import sys
import queue

BUFFER_SIZE        = 1024
### WARNING! PLEASE SET THESE VALUES ACCORDING TO THE REPORT BEFORE RUNNING ###

SERVER_ADDRESS_PORT = (socket.gethostbyname("server"), 20002)
CURRENT_DIRECTORY  = os.getcwd()
SOCKET_TIMEOUT     = 0.5
TOTAL_CHUNKS_SMALL = 0
TOTAL_CHUNKS_LARGE = 0
TOTAL_CHUNKS_ALL   = 0
FILES              = [f"{size}-{i}" for size in ["small", "large"] for i in range(10)]
ALL_DONE           = False
total_acks         = 0
received_acks      = 0
chunks             = {}
files_done         = {}
chunks_lock        = threading.Lock()
terminate_event    = threading.Event()

total_chunks_small_not_set = True
total_chunks_large_not_set = True
new_chunks_not_ready       = True
total_chunks_set_cond      = threading.Condition()
new_chunks_ready_cond      = threading.Condition()
for file_name in FILES:
    files_done[file_name] = False

# Ensure the directory exists
os.makedirs(os.path.join(CURRENT_DIRECTORY, "objects_received_udp"), exist_ok=True)

# Delete all files inside the folder
folder_path = os.path.join(CURRENT_DIRECTORY, "objects_received_udp")
for file_name in os.listdir(folder_path):
    file_path = os.path.join(folder_path, file_name)
    if os.path.isfile(file_path):
        os.remove(file_path)


# Create a datagram socket at the Client side and bind it
UDPClientSocket = socket.socket(family=socket.AF_INET, type=socket.SOCK_DGRAM)
UDPClientSocket.settimeout(SOCKET_TIMEOUT)
starttime = time.time()

# Checks if all chunks have been received for a file and if so, reassembles the file
def file_creator():
    global files_done, ALL_DONE, new_chunks_not_ready

    with total_chunks_set_cond:
        while total_chunks_small_not_set and total_chunks_large_not_set:
            total_chunks_set_cond.wait()
    
    # total chunks is set
    while not all(files_done.values()):  # Continue until all files are done
        for file_name in files_done:
            if not files_done[file_name]:  # If the file is not done
                total_chunks = TOTAL_CHUNKS_SMALL if file_name.startswith("small") else TOTAL_CHUNKS_LARGE

                if file_name.startswith("small"):
                    n = int(file_name.split("-")[1])
                    start_sequence = total_chunks * n + 1
                    end_sequence = total_chunks * (n + 1)
                else:
                    n = int(file_name.split("-")[1])
                    start_sequence = TOTAL_CHUNKS_SMALL * 10 + total_chunks * n + 1
                    end_sequence = TOTAL_CHUNKS_SMALL * 10 + total_chunks * (n + 1)

                with chunks_lock:
                    all_chunks_received = all(chunks.get(i) is not None for i in range(start_sequence, end_sequence + 1))
                
                if all_chunks_received:
                    # If all chunks are received, create the file
                    with chunks_lock:
                        with open(os.path.join(CURRENT_DIRECTORY, "objects_received_udp", f"{file_name}.obj"), 'wb') as f:
                            f.truncate(0)  # Clear existing content
                            for sequence in range(start_sequence, end_sequence + 1):
                                f.write(chunks[sequence])

                    # Calculate MD5 hash
                    with open(os.path.join(CURRENT_DIRECTORY, "objects_received_udp", f"{file_name}.obj"), 'rb') as f:
                        file_data = f.read()
                        calculated_hash = hashlib.md5(file_data).hexdigest()

                    # Read the stored MD5 hash
                    with open(os.path.join(CURRENT_DIRECTORY, "objects", f"{file_name}.obj.md5"), 'r') as md5_file:
                        stored_hash = md5_file.read().strip()

                    # Compare hashes
                    if stored_hash == calculated_hash:
                        files_done[file_name] = True
                    else:
                        print(f"File {file_name} corrupted during transfer. Retrying... Don't close.")

                    # Mark the file as done
                    if all(files_done.values()):
                        ALL_DONE = True
                        terminate_event.set()

        if not ALL_DONE:
            with new_chunks_ready_cond:
                while new_chunks_not_ready:
                    new_chunks_ready_cond.wait()
            new_chunks_not_ready = True

    print("All files should be intact.")
    ALL_DONE = True
    return

file_creator_thread = threading.Thread(target=file_creator, name="File Creator")
file_creator_thread.start()

# This will send a hello packet to the server to start the transfer
# It will ensure that the server have our information
hello = 0
UDPClientSocket.sendto(hello.to_bytes(4, 'big'), SERVER_ADDRESS_PORT)

# Chunks will hold sequence number -> acked info. It will start from 1 and go up to the
# total number of chunks in all of the files. Total chunks will be set when the first packet
# from the file of that kind is received.
while(not terminate_event.is_set()):
    try:
        bytesAddressPair = UDPClientSocket.recvfrom(BUFFER_SIZE)
    except socket.timeout:
        if (received_acks == 0):
            UDPClientSocket.sendto(hello.to_bytes(4, 'big'), SERVER_ADDRESS_PORT)
        continue
    
    packet = bytesAddressPair[0]
    
    # Extract the checksum, length, sequence number, total chunks, and data from the packet
    file_name = packet[0:7].decode()
    sequence_number = int.from_bytes(packet[7:11], 'big')
    total_chunks = int.from_bytes(packet[11:15], 'big')
    message = packet[15:]

    # Try to set the total chunks if it is not set
    if total_chunks_small_not_set and file_name.startswith("small"):
        TOTAL_CHUNKS_SMALL = total_chunks
        total_chunks_small_not_set = False
    elif total_chunks_large_not_set and file_name.startswith("large"):
        TOTAL_CHUNKS_LARGE = total_chunks
        total_chunks_large_not_set = False
    with total_chunks_set_cond:
        if not total_chunks_small_not_set and not total_chunks_large_not_set:
            TOTAL_CHUNKS_ALL = TOTAL_CHUNKS_SMALL * 10 + TOTAL_CHUNKS_LARGE * 10
            total_chunks_set_cond.notify_all()
    
    ack_packet = sequence_number.to_bytes(4, 'big')

    UDPClientSocket.sendto(ack_packet, SERVER_ADDRESS_PORT)
    total_acks += 1

    with chunks_lock:
        if chunks.get(sequence_number) is None:
            chunks[sequence_number] = message
            received_acks += 1
    
    new_chunks_not_ready = False
    
    with new_chunks_ready_cond:
        if not new_chunks_not_ready:
            new_chunks_ready_cond.notify_all()

    if received_acks == TOTAL_CHUNKS_ALL:
        break
    
print(f"{time.time() - starttime}") 
