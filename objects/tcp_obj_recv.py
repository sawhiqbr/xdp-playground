import socket
import time

if __name__ == '__main__':
    # Defining Socket
    host = '0.0.0.0'
    port = 8080
    totalclient = 1

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind((host, port))
    sock.listen(totalclient)
    # Establishing Connections
    connections = []
    print('Initiating clients')
    for i in range(totalclient):
        conn = sock.accept()
        connections.append(conn)
        print('Connected with client', i+1)

    fileno = 0
    idx = 0
    start = 0
    for conn in connections:
        # Receiving File Data
        idx += 1
        data = conn[0].recv(1024).decode()

        if not data:
            continue

        if start == 0:
            start = 1
            starttime = time.time()
        # Creating a new file at server end and writing the data 
        filename = 'output' + str(fileno) + '.txt'
        fileno = fileno + 1
        fo = open(filename, "w")
        while data:
            if not data:
                break
            else:
                fo.write(data)
                data = conn[0].recv(1024).decode()

        print()
        print('Receiving file from client', idx)
        print()
        print('Received successfully! New filename is:', filename)
        print(f"Time taken: {time.time() - starttime}")

        fo.close()
    # Closing all Connections
    for conn in connections:
        conn[0].close()
