#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <locale.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include <sys/resource.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <stdbool.h>

#define BUFFER_SIZE 1024
#define MAX_CHUNKS 10000
#define TOTAL_CHUNKS_SMALL 10
#define TOTAL_CHUNKS_LARGE 991
#define PORT_ 12345
bool files_done[20];
int done_counter = 0;
int first_packet = 0;

struct timeval start, temp, end;
long seconds, useconds;
double mtime;

struct packet_message
{
  char message[BUFFER_SIZE - 15];
  int length;
};

struct ack_packet
{
  int sequence_number;
};

// UDP packet structure
struct udp_custom_packet
{
  char file_name[8];
  int sequence_number;
  int total_chunks;
  char message[BUFFER_SIZE - 15];
};

struct file_info
{
  struct packet_message *received_messages[MAX_CHUNKS];
  char file_name[8];
  int total_chunks;
  int rcvd_chunks;
  bool done;
};

static struct file_info files[20];
bool global_exit = false;

void init_files()
{
  for (int i = 0; i < 10; i++)
  {
    snprintf(files[i].file_name, sizeof(files[i].file_name), "small-%d", i);
    files[i].total_chunks = 0;
    files[i].rcvd_chunks = 0;
    files[i].done = false;
    memset(files[i].received_messages, 0, sizeof(files[i].received_messages));
  }

  for (int i = 0; i < 10; i++)
  {
    snprintf(files[10 + i].file_name, sizeof(files[10 + i].file_name), "large-%d", i);
    files[10 + i].total_chunks = 0;
    files[10 + i].rcvd_chunks = 0;
    files[10 + i].done = false;
    memset(files[10 + i].received_messages, 0, sizeof(files[10 + i].received_messages));
  }
}

void check_and_create_file()
{
  for (int i = 0; i < 20; i++)
  {
    if (files[i].done)
      continue;

    if (files[i].total_chunks == 0)
      continue;

    if (files[i].total_chunks != files[i].rcvd_chunks)
      continue;

    char file_path[256];
    snprintf(file_path, sizeof(file_path), "received_objects/%s.obj", files[i].file_name);

    mkdir("received_objects", 0777);

    FILE *file = fopen(file_path, "wb");
    if (file == NULL)
    {
      fprintf(stderr, "ERROR: Failed to create file %s\n", file_path);
      return;
    }

    for (int j = 1; j <= files[i].total_chunks; j++)
    {
      fwrite(files[i].received_messages[j]->message, 1, files[i].received_messages[j]->length, file);
    }

    fclose(file);
    files[i].done = true;
    done_counter++;
    if (done_counter == 20)
    {
      gettimeofday(&end, NULL);

      seconds = end.tv_sec - start.tv_sec;
      useconds = end.tv_usec - start.tv_usec;

      mtime = ((seconds) * 1000 + useconds / 1000.0) + 0.5;

      printf("Time taken to receive and create all files: %f milliseconds\n", mtime);

      printf("All files are received\n");
      exit(0);
    }
  }
}

void init_message_storage()
{
  for (int i = 0; i < 20; i++)
  {
    for (int j = 0; j < MAX_CHUNKS; j++)
    {
      files[i].received_messages[j] = NULL;
    }
  }
}

void free_message_storage()
{
  for (int i = 0; i < 20; i++)
  {
    for (int j = 0; j < MAX_CHUNKS; j++)
    {
      if (files[i].received_messages[j] != NULL)
      {
        free(files[i].received_messages[j]);
        files[i].received_messages[j] = NULL;
      }
    }
  }
}

void handle_receive_packets(int sockfd, struct sockaddr_in *peer_addr)
{
  char buffer[BUFFER_SIZE];
  struct sockaddr_in src_addr;
  socklen_t addrlen = sizeof(src_addr);
  struct udp_custom_packet *cust_pkt;
  int message_length;

  while (!global_exit)
  {
    int len = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&src_addr, &addrlen);
    if (len < 0)
    {
      perror("recvfrom failed");
      continue;
    }

    if (first_packet == 0)
    {
      printf("First packet arrived\n");
      first_packet = 1;
      gettimeofday(&start, NULL);
    }

    cust_pkt = (struct udp_custom_packet *)buffer;

    int total_chunks = ntohl(cust_pkt->total_chunks);
    int sequence_number = ntohl(cust_pkt->sequence_number);
    message_length = len - sizeof(struct udphdr) - 16;

    if (sequence_number < MAX_CHUNKS * 20)
    {
      int file_index = 0;
      for (; file_index < 19; file_index++)
      {
        if (strcmp(files[file_index].file_name, cust_pkt->file_name) == 0)
          break;
      }
      files[file_index].total_chunks = total_chunks;

      int sequence_number_file_based = 0;
      if (sequence_number <= 100)
      {
        sequence_number_file_based = sequence_number % TOTAL_CHUNKS_SMALL;
        if (sequence_number_file_based == 0)
          sequence_number_file_based = TOTAL_CHUNKS_SMALL;
      }
      else
      {
        sequence_number_file_based = (sequence_number - 100) % TOTAL_CHUNKS_LARGE;
        if (sequence_number_file_based == 0)
          sequence_number_file_based = TOTAL_CHUNKS_LARGE;
      }

      if (files[file_index].received_messages[sequence_number_file_based] == NULL)
      {
        files[file_index].received_messages[sequence_number_file_based] = (struct packet_message *)malloc(sizeof(struct packet_message));
        if (files[file_index].received_messages[sequence_number_file_based] == NULL)
        {
          fprintf(stderr, "ERROR: Failed to allocate memory for message chunk\n");
          continue;
        }
        if (message_length > BUFFER_SIZE - 15)
        {
          message_length = BUFFER_SIZE - 15;
        }
        memcpy(files[file_index].received_messages[sequence_number_file_based]->message, cust_pkt->message, message_length);
        files[file_index].received_messages[sequence_number_file_based]->length = message_length;
        files[file_index].rcvd_chunks++;

        struct ack_packet ack;
        ack.sequence_number = htonl(sequence_number);
        sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)peer_addr, sizeof(*peer_addr));
      }
      else
      {
        struct ack_packet ack;
        ack.sequence_number = htonl(sequence_number);
        sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)peer_addr, sizeof(*peer_addr));
      }
    }
    else
    {
      printf("Sequence number %d out of bounds\n", sequence_number);
    }

    check_and_create_file();
  }
}

static void exit_application(int signal)
{
  free_message_storage();
  global_exit = true;
}

int main(int argc, char **argv)
{
  int sockfd;
  struct sockaddr_in server_addr, peer_addr;

  signal(SIGINT, exit_application);

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0)
  {
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(PORT_);

  if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
  {
    perror("bind failed");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  memset(&peer_addr, 0, sizeof(peer_addr));
  peer_addr.sin_family = AF_INET;
  peer_addr.sin_port = htons(PORT_);
  inet_pton(AF_INET, "192.168.122.97", &peer_addr.sin_addr);

  init_message_storage();
  init_files();

  handle_receive_packets(sockfd, &peer_addr);

  close(sockfd);
  return 0;
}
