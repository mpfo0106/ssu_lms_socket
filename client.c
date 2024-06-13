#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUF_SIZE 100
#define STU_INFO_SIZE 20

void *send_msg(void *arg);
void *recv_msg(void *arg);
void error_handling(char *message);

char name[STU_INFO_SIZE] = "[DEFAULT]";
char stuNum[STU_INFO_SIZE] = "[DEFAULT]";
char msg[BUF_SIZE];
int sock;

int main(int argc, char *argv[]) {
  struct sockaddr_in serv_addr;
  pthread_t snd_thread, rcv_thread;
  void *thread_return;

  if (argc != 3) {
    printf("Usage : %s <IP> <port>\n", argv[0]);
    exit(1);
  }

  sock = socket(PF_INET, SOCK_STREAM, 0);

  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
  serv_addr.sin_port = htons(atoi(argv[2]));

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
    error_handling("connect() error");

  printf("Connected to server at %s:%s\n", argv[1], argv[2]);

  // 서버와 연결된 후 name과 stuNum 입력 받기
  printf("Enter your name: ");
  fgets(name, STU_INFO_SIZE, stdin);
  name[strcspn(name, "\n")] = 0; // 개행 문자 제거
  sprintf(name, "[%s]", name);

  printf("Enter your student number: ");
  fgets(stuNum, STU_INFO_SIZE, stdin);
  stuNum[strcspn(stuNum, "\n")] = 0; // 개행 문자 제거
  sprintf(stuNum, "[%s]", stuNum);

  pthread_create(&snd_thread, NULL, send_msg, (void *)&sock);
  pthread_create(&rcv_thread, NULL, recv_msg, (void *)&sock);
  pthread_join(snd_thread, &thread_return);
  pthread_join(rcv_thread, &thread_return);
  close(sock);
  return 0;
}

void *send_msg(void *arg) {
  int sock = *((int *)arg);
  char name_msg[STU_INFO_SIZE + BUF_SIZE];

  while (1) {
    fgets(msg, BUF_SIZE, stdin);

    if (!strncmp(msg, "quit", 4)) {
      close(sock);
      exit(0);
    } else if (!strncmp(msg, "whisper", 7)) {
      char whisper_msg[BUF_SIZE];
      printf("Enter user to whisper: ");
      fgets(whisper_msg, BUF_SIZE, stdin);
      snprintf(name_msg, STU_INFO_SIZE + BUF_SIZE, "%s %s", msg, whisper_msg);
    } else {
      snprintf(name_msg, STU_INFO_SIZE + BUF_SIZE, "%s %s", name, msg);
    }

    write(sock, name_msg, strlen(name_msg));
    printf("Sent: %s", name_msg);
  }
  return NULL;
}

void *recv_msg(void *arg) {
  int sock = *((int *)arg);
  char name_msg[STU_INFO_SIZE + BUF_SIZE];
  int str_len;

  while (1) {
    str_len = read(sock, name_msg, STU_INFO_SIZE + BUF_SIZE - 1);
    if (str_len == -1)
      return (void *)-1;

    name_msg[str_len] = 0;
    fputs(name_msg, stdout);
    printf("Received: %s\n", name_msg);
  }
  return NULL;
}

void error_handling(char *message) {
  fputs(message, stderr);
  fputc('\n', stderr);
  exit(1);
}
