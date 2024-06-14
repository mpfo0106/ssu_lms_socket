#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define BUF_SIZE 100
#define FILE_BUF_SIZE 524280
#define MAX_CLNT 256

void *handle_clnt(void *arg);
void send_msg_to_all(char *msg, int len);
void send_msg_to_professor(char *msg, int len);
void send_msg_to_student(int student_sock, char *msg, int len);
void error_handling(char *message);
void handle_file_transfer(int clnt_sock, char *filename);

int clnt_cnt = 0;
int clnt_socks[MAX_CLNT];
char clnt_ids[MAX_CLNT][BUF_SIZE];
int professor_sock = -1;
int professor_connected = 0;
pthread_mutex_t mutx;

int main(int argc, char *argv[]) {
  int serv_sock, clnt_sock;
  struct sockaddr_in serv_adr, clnt_adr;
  socklen_t clnt_adr_sz; // 여기서 int를 socklen_t로 변경합니다.
  pthread_t t_id;
  int option = 1;

  if (argc != 2) {
    printf("Usage : %s <port>\n", argv[0]);
    fflush(stdout); // 출력 버퍼 비우기
    exit(1);
  }

  pthread_mutex_init(&mutx, NULL);
  serv_sock = socket(PF_INET, SOCK_STREAM, 0);

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
  fflush(stdout); // 출력 버퍼 비우기

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
    fflush(stdout); // 출력 버퍼 비우기
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
    fprintf(stderr, "start~\n"); // stderr로 출력하여 즉시 출력되도록 합��다.

    // 개행 문자 제거
    msg[strcspn(msg, "\r\n")] = 0;

    if (strncmp(msg, "sendfile ", 9) == 0) {
      fprintf(stderr, "ssssend file\n");
      char filename[BUF_SIZE];
      sscanf(msg + 9, "%s", filename);
      handle_file_transfer(clnt_sock, filename);
    } else if (strcmp(client_type, "professor") == 0) {
      if (strncmp(msg, "sendto ", 7) == 0) {
        char target_id[BUF_SIZE];
        sscanf(msg + 7, "%s", target_id);
        for (int i = 0; i < clnt_cnt; i++) {
          if (strcmp(clnt_ids[i], target_id) == 0) {
            send_msg_to_student(clnt_socks[i], msg + 7 + strlen(target_id),
                                str_len - 7 - strlen(target_id));
            break;
          }
        }
      } else {
        send_msg_to_all(msg, str_len);
      }
    } else {
      send_msg_to_professor(msg, str_len);
    }
  }

  pthread_mutex_lock(&mutx);
  for (int i = 0; i < clnt_cnt; i++) {
    if (clnt_sock == clnt_socks[i]) {
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
  pthread_mutex_unlock(&mutx);
  close(clnt_sock);
  return NULL;
}

void send_msg_to_all(char *msg, int len) {
  pthread_mutex_lock(&mutx);
  for (int i = 0; i < clnt_cnt; i++) {
    if (clnt_socks[i] != professor_sock) {
      write(clnt_socks[i], msg, len);
    }
  }
  pthread_mutex_unlock(&mutx);
}

void send_msg_to_professor(char *msg, int len) {
  pthread_mutex_lock(&mutx);
  if (professor_sock != -1) {
    write(professor_sock, msg, len);
  }
  pthread_mutex_unlock(&mutx);
}

void send_msg_to_student(int student_sock, char *msg, int len) {
  write(student_sock, msg, len);
}

void handle_file_transfer(int clnt_sock, char *filename) {
  char file_buffer[FILE_BUF_SIZE];
  int n;
  char filepath[BUF_SIZE];
  char directory[BUF_SIZE];

  // serverFiles 디렉토리 생성
  snprintf(directory, sizeof(directory), "mkdir -p serverFiles");
  system(directory);

  // 클라이언트별 디렉토리 생성
  snprintf(directory, sizeof(directory), "mkdir -p serverFiles/%s",
           clnt_ids[clnt_cnt - 1]);
  system(directory);

  snprintf(filepath, sizeof(filepath), "serverFiles/%s/%s",
           clnt_ids[clnt_cnt - 1], filename);

  FILE *file = fopen(filepath, "w");
  if (file == NULL) {
    printf("Failed to open file %s: %s\n", filepath, strerror(errno));
    fflush(stdout);
    return;
  }

  while ((n = read(clnt_sock, file_buffer, FILE_BUF_SIZE)) > 0) {
    fwrite(file_buffer, sizeof(char), n, file);
  }

  fclose(file);
  printf("File %s received successfully.\n", filepath);
  fflush(stdout);
}

void error_handling(char *message) {
  fputs(message, stderr);
  fputc('\n', stderr);
  exit(1);
}
