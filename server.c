#include <arpa/inet.h>
#include <errno.h>
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
#define MAX_STUDENTS 256

typedef struct {
  char student_number[BUF_SIZE];
  int socket;
} Student;

Student students[MAX_STUDENTS];
int student_count = 0;

void *handle_clnt(void *arg);
void send_msg_to_all(char *msg, int len);
void send_msg_to_professor(char *msg, int len);
void send_msg_to_student(int student_sock, char *msg, int len);
void send_msg_to_student_by_number(const char *student_number, char *msg,
                                   int len);
void handle_file_transfer(int clnt_sock, char *filename);
void error_handling(const char *msg);

int clnt_cnt = 0;
int clnt_socks[MAX_CLNT];
int professor_sock = -1;
pthread_mutex_t mutx;

int main(int argc, char *argv[]) {
  int serv_sock, clnt_sock;
  struct sockaddr_in serv_adr, clnt_adr;
  int clnt_adr_sz;
  pthread_t t_id;
  int optval = 1;

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    exit(1);
  }

  pthread_mutex_init(&mutx, NULL);
  serv_sock = socket(PF_INET, SOCK_STREAM, 0);
  if (serv_sock == -1)
    error_handling("socket() error");

  // 포트 재사용 옵션 설정
  if (setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &optval,
                 sizeof(optval)) == -1)
    error_handling("setsockopt() error");

  memset(&serv_adr, 0, sizeof(serv_adr));
  serv_adr.sin_family = AF_INET;
  serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_adr.sin_port = htons(atoi(argv[1]));

  if (bind(serv_sock, (struct sockaddr *)&serv_adr, sizeof(serv_adr)) == -1)
    error_handling("bind() error");
  if (listen(serv_sock, 5) == -1)
    error_handling("listen() error");

  fprintf(stderr, "Server started on port %s\n", argv[1]);
  while (1) {
    clnt_adr_sz = sizeof(clnt_adr);
    clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_adr, &clnt_adr_sz);

    pthread_mutex_lock(&mutx);
    clnt_socks[clnt_cnt++] = clnt_sock;
    pthread_mutex_unlock(&mutx);

    pthread_create(&t_id, NULL, handle_clnt, (void *)&clnt_sock);
    pthread_detach(t_id);

    fprintf(stderr, "Connected client IP: %s \n", inet_ntoa(clnt_adr.sin_addr));
  }
  close(serv_sock);
  return 0;
}

void *handle_clnt(void *arg) {
  int clnt_sock = *((int *)arg);
  int str_len = 0, i;
  char msg[BUF_SIZE];
  char filename[BUF_SIZE];
  int is_professor = 0;

  // 클라이언트로부터 메시지 읽기
  str_len = read(clnt_sock, msg, sizeof(msg));
  if (strncmp(msg, "professor", str_len) == 0) {
    professor_sock = clnt_sock;
    is_professor = 1;
  }

  // 이름 읽기
  str_len = read(clnt_sock, msg, sizeof(msg));
  msg[str_len] = '\0'; // 문자열 끝에 null 문자 추가
  fprintf(stderr, "Received name: %s\n", msg);

  // 학번 읽기 (교수가 아닌 경우)
  if (!is_professor) {
    str_len = read(clnt_sock, msg, sizeof(msg));
    msg[str_len] = '\0'; // 문자열 끝에 null 문자 추가
    fprintf(stderr, "Received student number: %s\n", msg);

    // 학번과 소켓을 매핑하여 저장
    pthread_mutex_lock(&mutx);
    strncpy(students[student_count].student_number, msg, BUF_SIZE);
    students[student_count].socket = clnt_sock;
    student_count++;
    pthread_mutex_unlock(&mutx);
  }

  // 일반 메시지 처리
  while ((str_len = read(clnt_sock, msg, sizeof(msg) - 1)) > 0) {
    msg[str_len] = '\0';
    // filename: 명령어는 교수와 학생 모두 사용할 수 있음
    if (strncmp(msg, "sendfile:", 9) == 0) {
      // 파일 전송 요청 처리
      sscanf(msg + 9, "%s", filename);
      handle_file_transfer(clnt_sock, filename);
      continue; // 다음 메시지로 넘어감
    }

    if (is_professor) {
      if (strncmp(msg, "sendto:", 7) == 0) {
        // 특정 학생에게 메시지 전송
        char *token = strtok(msg + 7, " ");
        char student_number[BUF_SIZE];
        char *message;

        if (token != NULL) {
          strncpy(student_number, token, BUF_SIZE);
          message = strtok(NULL, "");
          if (message != NULL) {
            send_msg_to_student_by_number(student_number, message,
                                          strlen(message));
          } else {
            error_handling("No message provided\n");
          }
        } else {
          error_handling("Invalid sendto format\n");
        }
      } else {
        // 모든 클라이언트에게 메시지 전송
        send_msg_to_all(msg, str_len);
      }
    } else {
      // 교수에게 메시지 전송
      send_msg_to_professor(msg, str_len);
    }
  }

  // 클라이언트 연결 종료 처리
  pthread_mutex_lock(&mutx);
  for (i = 0; i < clnt_cnt; i++) {
    if (clnt_sock == clnt_socks[i]) {
      for (; i < clnt_cnt - 1; i++)
        clnt_socks[i] = clnt_socks[i + 1];
      break;
    }
  }
  clnt_cnt--;
  pthread_mutex_unlock(&mutx);
  close(clnt_sock);
  return NULL;
}

void send_msg_to_all(char *msg, int len) {
  pthread_mutex_lock(&mutx);
  // 모든 클라이언트에게 메시지 전송
  for (int i = 0; i < clnt_cnt; i++) {
    if (clnt_socks[i] != professor_sock) { // 교수 소켓 제외
      write(clnt_socks[i], msg, len);
    }
  }
  pthread_mutex_unlock(&mutx);
}

void send_msg_to_professor(char *msg, int len) {
  pthread_mutex_lock(&mutx);
  // 교수에게 메시지 전송
  if (professor_sock != -1) {
    write(professor_sock, msg, len);
  }
  pthread_mutex_unlock(&mutx);
}

void send_msg_to_student(int student_sock, char *msg, int len) {
  // 특정 학생에게 메시지 전송
  write(student_sock, msg, len);
}

void send_msg_to_student_by_number(const char *student_number, char *msg,
                                   int len) {
  pthread_mutex_lock(&mutx);
  // 학번을 통해 특정 학생에게 메시지 전송
  for (int i = 0; i < student_count; i++) {
    if (strcmp(students[i].student_number, student_number) == 0) {
      write(students[i].socket, msg, len);
      break;
    }
  }
  pthread_mutex_unlock(&mutx);
}

void handle_file_transfer(int clnt_sock, char *filename) {
  char file_buffer[FILE_BUF_SIZE];
  int n;
  char filepath[BUF_SIZE];
  char directory[BUF_SIZE];

  // 기본 디렉토리 생성
  snprintf(directory, sizeof(directory), "serverFiles");
  if (mkdir(directory, 0777) == -1 && errno != EEXIST) {
    error_handling("Error in creating directory.");
    return;
  }

  // 파일 경로 설정
  snprintf(filepath, sizeof(filepath), "serverFiles/%s", filename);
  FILE *file = fopen(filepath, "w");
  if (file == NULL) {
    fprintf(stderr, "Failed to open file %s: %s\n", filepath, strerror(errno));
    return;
  }
  fprintf(stderr, "File %s opened successfully.\n", filepath);

  // 파일 수신 및 저장
  while ((n = read(clnt_sock, file_buffer, FILE_BUF_SIZE)) > 0) {
    fprintf(stderr, "filebuffer: %s \n", file_buffer);
    fwrite(file_buffer, sizeof(char), n, file);
    bzero(file_buffer, FILE_BUF_SIZE);
  }

  fclose(file);
  fprintf(stderr, "File %s received successfully.\n", filepath);
}

void error_handling(const char *message) {
  fputs(message, stderr);
  fputc('\n', stderr);
  exit(1);
}
