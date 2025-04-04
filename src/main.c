#include <stdio.h>
#include <netdb.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define MAX_LENGTH 4096

void *handle_request_thread(void *);
void *handle_file_request(void *);
void *handle_not_found(void *);

struct client_info {
  long long int client_fd;
  char **args;      
  int arg_len;
  char *filename;   // requested file
};

int main(int argc, char *argv[]) {
  int server_fd;
  
  server_fd = socket(AF_INET, SOCK_STREAM, 0);

  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse) == -1){
    printf("setsockopt Failed: %s\n", strerror(errno));
    return 1;
  }
  
  struct sockaddr_in serv_addr = {
    .sin_family = AF_INET,
    .sin_port = htons(4221),
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

    struct client_info *c_info = (struct client_info*)malloc(sizeof(*c_info));
    c_info->client_fd = client_fd;
    c_info->args = argv;
    c_info->arg_len = argc;

    pthread_t thread;
    pthread_create(&thread, NULL, handle_request_thread, c_info);
    pthread_detach(thread);
  }

  close(server_fd);
  return 0;
}

void *handle_request_thread(void *c){
  struct client_info* c_info = (struct client_info*)c;
  long long int client_fd = c_info->client_fd;
  int err = 1;

  char buf[MAX_LENGTH];

  int bytes_received;
  if ( (bytes_received = recv(client_fd, buf, MAX_LENGTH, 0)) <= 0){
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

    if (send(client_fd, res, strlen(res) + 1, 0) == -1){
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
    sprintf(res,"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %ld\r\n\r\n%s",
            strlen(str), str);

    if (send(client_fd, res, strlen(res) + 1, 0) == -1){
      printf("Send failed: %s\n", strerror(errno));
      return NULL;
    }
  }
  else if(strncmp(token, "/files/", 7) == 0){
    char filename[strlen(token) - 7];
    strcpy(filename, token+7);
    c_info->filename = filename;
    handle_file_request(c);
  }
  else{
    handle_not_found((void *)c_info);
  }

  free(c_info);
  shutdown(client_fd, SHUT_WR);
  close(client_fd);
  return 0;
}

void *handle_file_request(void *c){
  struct client_info *c_info = (struct client_info *)c;
  long long int client_fd = c_info->client_fd;
  char *filename = c_info->filename;
  char **args = c_info->args;
  int arg_len = c_info->arg_len;
  char *dir;

  if (arg_len > 1 && strcmp(args[1], "--directory") == 0){
    dir = args[2];
  }
  if (dir == NULL)
    return NULL;
  
  int len = strlen(dir) + strlen(filename) + 2;
  char path[len];
  snprintf(path, len,"%s/%s", dir, filename);

  char s[MAX_LENGTH];
  pthread_mutex_t lock;
  int rc = pthread_mutex_init(&lock, NULL);
  
  FILE *fptr = fopen(path, "r");

  if (fptr == NULL){
    handle_not_found(c_info);
    return 0;
  }
  
  pthread_mutex_lock(&lock);
  // Critical Section
  while ((fgets(s, sizeof s, fptr)) != NULL){
     continue;
  }
  fclose(fptr);
  pthread_mutex_unlock(&lock);

  char res[MAX_LENGTH];

  sprintf(res, "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %ld\r\n\r\n%s", strlen(s), s);

  if (send(client_fd, res, strlen(res) + 1, 0) == -1){
      printf("Send Failed: %s\n", strerror(errno));
      return NULL;
  }
  return 0;
}

void *handle_not_found(void *c){
  char *res = "HTTP/1.1 404 Not Found\r\n\r\n";
  int len = strlen(res);
  long long int client_fd = ((struct client_info*)c)->client_fd;

  if (send(client_fd, res, len, 0) == -1){
      printf("Send Failed: %s\n", strerror(errno));
      return NULL;
  }
  return 0;
}
