#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BUF_SIZE 100
#define FILE_BUF_SIZE 524280
#define MAX_CLNT 256

void *handle_clnt(void *arg);
void send_msg(char *msg, int len, int sender_sock);
void error_handling(char *message);
void handle_file_transfer(int clnt_sock, char *filename);

int clnt_cnt = 0;
int clnt_socks[MAX_CLNT];
char clnt_ids[MAX_CLNT][BUF_SIZE]; // Array to store student IDs
int professor_sock = -1;
int professor_connected = 0;
pthread_mutex_t mutx;

int main(int argc, char *argv[]) {
  int serv_sock, clnt_sock;
  struct sockaddr_in serv_adr, clnt_adr;
  int clnt_adr_sz;
  pthread_t t_id;
  int option = 1;

  if (argc != 2) {
    printf("Usage : %s <port>\n", argv[0]);
    exit(1);
  }

  pthread_mutex_init(&mutx, NULL);
  serv_sock = socket(PF_INET, SOCK_STREAM, 0);

  // Set the socket options to reuse the address
  setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

  memset(&serv_adr, 0, sizeof(serv_adr));
  serv_adr.sin_family = AF_INET;
  serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_adr.sin_port = htons(atoi(argv[1]));

  if (bind(serv_sock, (struct sockaddr *)&serv_adr, sizeof(serv_adr)) == -1)
    error_handling("bind() error");
  if (listen(serv_sock, 5) == -1)
    error_handling("listen() error");

  printf("Server started on port %s\n", argv[1]);

  while (1) {
    clnt_adr_sz = sizeof(clnt_adr);
    clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_adr, &clnt_adr_sz);

    pthread_mutex_lock(&mutx);
    clnt_socks[clnt_cnt] = clnt_sock;
    pthread_mutex_unlock(&mutx);

    pthread_create(&t_id, NULL, handle_clnt, (void *)&clnt_socks[clnt_cnt]);
    pthread_detach(t_id);
    clnt_cnt++;
    printf("Connected client IP: %s \n", inet_ntoa(clnt_adr.sin_addr));
  }
  close(serv_sock);
  return 0;
}

void *handle_clnt(void *arg) {
  int clnt_sock = *((int *)arg);
  int str_len = 0;
  char msg[BUF_SIZE];
  char client_type[BUF_SIZE];
  char filename[BUF_SIZE];
  char exit_msg[BUF_SIZE + 20]; // Adjust size as needed

  // Receive client type
  if ((str_len = read(clnt_sock, client_type, sizeof(client_type) - 1)) <= 0) {
    close(clnt_sock);
    return NULL;
  }
  client_type[str_len] = 0;

  if (strcmp(client_type, "professor") == 0) {
    if (professor_connected) {
      char *err_msg = "Another professor is already connected.\n";
      write(clnt_sock, err_msg, strlen(err_msg));
      close(clnt_sock);
      return NULL;
    } else {
      professor_connected = 1;
      professor_sock = clnt_sock;
      printf("Professor connected.\n");
    }
  } else {
    printf("Student connected.\n");
  }

  // Receive student ID or professor notification
  if ((str_len = read(clnt_sock, msg, sizeof(msg) - 1)) <= 0) {
    close(clnt_sock);
    return NULL;
  }
  msg[str_len] = 0;
  pthread_mutex_lock(&mutx);
  strcpy(clnt_ids[clnt_cnt - 1], msg);
  pthread_mutex_unlock(&mutx);

  while ((str_len = read(clnt_sock, msg, sizeof(msg) - 1)) != 0) {
    if (str_len == -1)
      return (void *)-1;
    msg[str_len] = 0;
    if (strncmp(msg, "sendfile ", 9) == 0) {
      sscanf(msg + 9, "%s", filename);
      handle_file_transfer(clnt_sock, filename);
    } else {
      send_msg(msg, str_len, clnt_sock);
    }
  }

  pthread_mutex_lock(&mutx);
  int exiting_client_index = -1;
  for (int i = 0; i < clnt_cnt; i++) {
    if (clnt_sock == clnt_socks[i]) {
      exiting_client_index = i;
      while (i++ < clnt_cnt - 1)
        clnt_socks[i] = clnt_socks[i + 1];
      break;
    }
  }
  if (clnt_sock == professor_sock) {
    professor_connected = 0;
    professor_sock = -1;
    printf("Professor disconnected.\n");
  }
  clnt_cnt--;
  if (exiting_client_index != -1) {
    snprintf(exit_msg, sizeof(exit_msg), "%s 님이 퇴장하였습니다.\n",
             clnt_ids[exiting_client_index]);
    send_msg(exit_msg, strlen(exit_msg), clnt_sock);
  }
  pthread_mutex_unlock(&mutx);
  close(clnt_sock);
  return NULL;
}

void send_msg(char *msg, int len, int sender_sock) {
  pthread_mutex_lock(&mutx);
  char formatted_msg[BUF_SIZE + 20]; // Adjust size as needed
  int sender_index = -1;

  // Find the sender's index
  for (int i = 0; i < clnt_cnt; i++) {
    if (clnt_socks[i] == sender_sock) {
      sender_index = i;
      break;
    }
  }

  if (sender_index != -1) {
    snprintf(formatted_msg, sizeof(formatted_msg), "%s%s",
             clnt_ids[sender_index], msg);
    for (int i = 0; i < clnt_cnt; i++) {
      write(clnt_socks[i], formatted_msg, strlen(formatted_msg));
    }
  }
  pthread_mutex_unlock(&mutx);
}

void handle_file_transfer(int clnt_sock, char *filename) {
  FILE *fp;
  char file_buffer[FILE_BUF_SIZE];
  int n;
  char filepath[BUF_SIZE + 20]; // Adjust size as needed
  struct stat st = {0};

  // Check if the directory exists, if not, create it
  if (stat("serverFiles", &st) == -1) {
    if (mkdir("serverFiles", 0700) == -1) {
      printf("Failed to create directory serverFiles: %s\n", strerror(errno));
      return;
    }
  }

  snprintf(filepath, sizeof(filepath), "serverFiles/%s", filename);
  fp = fopen(filepath, "wb");
  if (fp == NULL) {
    printf("Failed to open file %s\n", filepath);
    return;
  }

  while ((n = read(clnt_sock, file_buffer, FILE_BUF_SIZE)) > 0) {
    fwrite(file_buffer, 1, n, fp);
  }

  fclose(fp);
  printf("File %s received successfully.\n", filepath);
}

void error_handling(char *message) {
  fputs(message, stderr);
  fputc('\n', stderr);
  exit(1);
}
