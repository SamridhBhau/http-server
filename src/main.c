#include <stdio.h>
#include <netdb.h>
#include <errno.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>

#define MAX_LENGTH 4096

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

  int backlog = 4;
  if (listen(server_fd, backlog) == -1){
    printf("Listen Failed: %s\n", strerror(errno));
    return 1;
  }

  struct sockaddr_storage client_addr;
  socklen_t addr_size = sizeof client_addr;

  int client_fd;
  if ((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_size)) == -1 ){
    printf("Accept Failed: %s\n", strerror(errno));
    return 1;
  }

  printf("Client Connected!\n");

  char buf[MAX_LENGTH];

  if (recv(client_fd, buf, MAX_LENGTH, 0) <= 0){
    printf("Recv Failed: %s\n", strerror(errno));
    return 1;
  }

  char *token = strtok(buf, " ");
  token = strtok(NULL, " ");

  if (strcmp(token, "/") == 0){
    char *buf = "HTTP/1.1 200 OK\r\n\r\n";
    int len = strlen(buf);

    if (send(client_fd, buf, len, 0) == -1){
      printf("Send Failed: %s\n", strerror(errno));
      return 1;
    }
  }
  else if (strncmp(token,"/echo/", 6) == 0){
    char *str = strtok(NULL, "/");
    char buf[] = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: ";

    // Content-Length
    int body_len = strlen(str);
    char str_len[body_len];
    snprintf(str_len, body_len, "%d\r\n\r\n", body_len);
    strcat(buf, str_len);

    // Response
    strcat(buf, str);

    int len = strlen(buf);

    if (send(client_fd, buf, len, 0) == -1){
      printf("Send Failed: %s\n", strerror(errno));
      return 1;
    }
  }
  else if (strcmp(token, "/user-agent") == 0){
    //TODO Review code
    char *ua = strtok(NULL, "\r\n");
    ua = strtok(NULL, "\r\n");
    ua = strtok(NULL, "\r\n");
    char *str = strtok(ua, " ");
    str = strtok(NULL, " ");

    char res[] = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: ";


    int body_len = strlen(str);
    char str_len[body_len];
    snprintf(str_len, body_len, "%d\r\n\r\n", body_len);

    strcat(res, str_len);
    strcat(res, str);
    printf("%s\n", res);

    int len = strlen(res);
    if (send(client_fd, res, len, 0) == -1){
      printf("Send failed: %s\n", strerror(errno));
      return 1;
    }
  }
  else {
    char *buf = "HTTP/1.1 404 Not Found\r\n\r\n";
    int len = strlen(buf);

    if (send(client_fd, buf, len, 0) == -1){
      printf("Send Failed: %s\n", strerror(errno));
      return 1;
    }
  }

  
  close(server_fd);

  return 0;
}
