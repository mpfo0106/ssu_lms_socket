#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define BUF_SIZE 100
#define FILE_BUF_SIZE 524280
#define STU_INFO_SIZE 50

void *send_msg(void *arg);
void *recv_msg(void *arg);
void error_handling(char *msg);

char name[STU_INFO_SIZE];
char stuNum[STU_INFO_SIZE];
int is_professor = 0;

int main(int argc, char *argv[]) {
  int sock;
  struct sockaddr_in serv_addr;
  pthread_t snd_thread, rcv_thread;
  void *thread_return;

  if (argc != 3) {
    printf("Usage: %s <IP> <port>\n", argv[0]);
    exit(1);
  }

  sock = socket(PF_INET, SOCK_STREAM, 0);
  if (sock == -1)
    error_handling("socket() error");

  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
  serv_addr.sin_port = htons(atoi(argv[2]));

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
    error_handling("connect() error");

  printf("Connected to server\n");

  printf("Are you a professor? (yes/no): ");
  char response[10];
  fgets(response, sizeof(response), stdin);
  if (strncmp(response, "yes", 3) == 0) {
    is_professor = 1;
    write(sock, "professor", strlen("professor"));
  } else {
    write(sock, "student", strlen("student"));
  }

  if (is_professor) {
    printf("Enter your name: ");
    fgets(name, STU_INFO_SIZE - 2, stdin);
    name[strcspn(name, "\n")] = 0;
    char formatted_name[STU_INFO_SIZE];
    snprintf(formatted_name, STU_INFO_SIZE, "[%s]", name);
    strncpy(name, formatted_name, STU_INFO_SIZE);

    write(sock, name, strlen(name));
  } else {
    printf("Enter your name: ");
    fgets(name, STU_INFO_SIZE - 2, stdin);
    name[strcspn(name, "\n")] = 0;
    char formatted_name[STU_INFO_SIZE];
    snprintf(formatted_name, STU_INFO_SIZE, "[%s]", name);
    strncpy(name, formatted_name, STU_INFO_SIZE);

    printf("Enter your student number: ");
    fgets(stuNum, STU_INFO_SIZE - 2, stdin);
    stuNum[strcspn(stuNum, "\n")] = 0;
    char formatted_stuNum[STU_INFO_SIZE];
    snprintf(formatted_stuNum, STU_INFO_SIZE, "[%s]", stuNum);
    strncpy(stuNum, formatted_stuNum, STU_INFO_SIZE);

    write(sock, name, strlen(name));
    write(sock, stuNum, strlen(stuNum));
  }

  pthread_create(&snd_thread, NULL, send_msg, (void *)&sock);
  pthread_create(&rcv_thread, NULL, recv_msg, (void *)&sock);
  pthread_join(snd_thread, &thread_return);
  pthread_join(rcv_thread, &thread_return);
  close(sock);
  return 0;
}

void *send_msg(void *arg) {
  int sock = *((int *)arg);
  char msg[BUF_SIZE];
  char msg_with_name[BUF_SIZE + STU_INFO_SIZE];

  while (1) {
    fgets(msg, BUF_SIZE, stdin);

    if (!strncmp(msg, "quit", 4)) {
      close(sock);
      exit(0);
    } else if (!strncmp(msg, "sendfile ", 9)) {
      char filename[BUF_SIZE];
      sscanf(msg + 9, "%s", filename);

      FILE *fp = fopen(filename, "r");
      if (fp == NULL) {
        printf("Failed to open file %s\n", filename);
        continue;
      }

      snprintf(msg_with_name, sizeof(msg_with_name), "%s: %s", name, msg);

      // Send filename
      write(sock, msg_with_name, strlen(msg_with_name));

      // Send file content
      char file_buffer[FILE_BUF_SIZE];
      size_t n;
      while ((n = fread(file_buffer, 1, FILE_BUF_SIZE, fp)) > 0) {
        write(sock, file_buffer, n);
      }
      fclose(fp);
      printf("File %s sent successfully.\n", filename);
    } else {
      snprintf(msg_with_name, sizeof(msg_with_name), "%s: %s", name, msg);
      write(sock, msg_with_name, strlen(msg_with_name));
    }
  }
  return NULL;
}

void *recv_msg(void *arg) {
  int sock = *((int *)arg);
  char name_msg[BUF_SIZE];
  int str_len;

  while (1) {
    str_len = read(sock, name_msg, sizeof(name_msg) - 1);
    if (str_len == -1)
      return (void *)-1;
    name_msg[str_len] = 0;
    fputs(name_msg, stdout);
  }
  return NULL;
}

void error_handling(char *message) {
  fputs(message, stderr);
  fputc('\n', stderr);
  exit(1);
}
