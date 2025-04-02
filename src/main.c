#include <stdio.h>
#include <netdb.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define MAX_LENGTH 4096

void *handle_request_thread(void *);

int main() {
  int server_fd;
  
  server_fd = socket(AF_INET, SOCK_STREAM, 0);

  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse) == -1){
    printf("setsockopt Failed: %s\n", strerror(errno));
    return 1;
  }
  
  struct sockaddr_in serv_addr = {
    .sin_family = AF_INET,
    .sin_port = htons(3221),
    .sin_addr = { htonl(INADDR_ANY) }
  };

  if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof serv_addr) == -1){
    printf("Bind Error: %s\n", strerror(errno));
    return 1;
  }

  if (listen(server_fd, SOMAXCONN) == -1){
    printf("Listen Failed: %s\n", strerror(errno));
    return 1;
  }


  while(1){
    struct sockaddr_storage client_addr;
    socklen_t addr_size = sizeof client_addr;

    long long int client_fd;
    if ((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_size)) == -1 ){
      printf("Accept Failed: %s\n", strerror(errno));
      return 1;
    }

    pthread_t thread;
    pthread_create(&thread, NULL, handle_request_thread, (void *)client_fd);
  }

  close(server_fd);
  return 0;
}

void *handle_request_thread(void *fd){
  int client_fd = (long long int)fd;
  int err = 1;

  char buf[MAX_LENGTH];

  int bytes_received;
  if ( (bytes_received = recv(client_fd, buf, MAX_LENGTH - 1, 0)) <= 0){
    printf("Recv Failed: %s\n", strerror(errno));
    return NULL;
  }

  int buf_len = strlen(buf);
  char *saveptr;
  char *token = strtok_r(buf, " ", &saveptr);
  token = strtok_r(NULL, " ", &saveptr);

  if (strcmp(token, "/") == 0){
    char *buf = "HTTP/1.1 200 OK\r\n\r\n";
    int len = strlen(buf);

    if (send(client_fd, buf, len, 0) == -1){
      printf("Send Failed: %s\n", strerror(errno));
      return NULL;
    }
  }
  else if (strncmp(token,"/echo/", 6) == 0){
    char *str = token + 6;

    // Content-Length
    char res[MAX_LENGTH];
    sprintf(res, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %ld\r\n\r\n%s",            strlen(str), str);

    int len = strlen(res);

    if (send(client_fd, res, len, 0) == -1){
      printf("Send Failed: %s\n", strerror(errno));
      return NULL;
    }
  }
  else if (strncmp(token, "/user-agent", 11) == 0){
    //TODO Review code
    strtok_r(NULL, "\r\n", &saveptr);
    strtok_r(NULL, "\r\n", &saveptr);
    char *str = strtok_r(NULL, "\r\n", &saveptr)+ 12;

    char res[MAX_LENGTH];
    sprintf(res, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %ld\r\n\r\n%s",
            strlen(str), str);

    int len = strlen(res);

    if (send(client_fd, res, len, 0) == -1){
      printf("Send failed: %s\n", strerror(errno));
      return NULL;
    }
  }
  else {
    char *res= "HTTP/1.1 404 Not Found\r\n\r\n";
    int len = strlen(res);

    if (send(client_fd, res, len, 0) == -1){
      printf("Send Failed: %s\n", strerror(errno));
      return NULL;
    }
  }

  close(client_fd);
  return 0;
}
