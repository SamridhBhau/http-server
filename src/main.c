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
void *handle_not_found(void *);
char *read_file(char *);

// TODO: Clean useless code
struct client_info {
  long long int client_fd;
  char **args;      
  int arg_len;
  char *filename;   // requested file
  char *request;
};

struct request_data {
  char *request_type; // GET/POST
  char *request_path; 
  char *agent_header;
  char *body;
};

void *handle_file_request(void *, struct request_data*);
void *write_file(char *, char *);
void parse_request_data(char *buf, struct request_data*);

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

  char client_req[MAX_LENGTH];

  int bytes_received;
  if ( (bytes_received = recv(client_fd, client_req, MAX_LENGTH, 0)) <= 0){
    printf("Recv Failed: %s\n", strerror(errno));
    return NULL;
  }

  struct request_data *req_data = (struct request_data *)malloc(sizeof(struct request_data));
  parse_request_data(client_req, req_data);

  if (strcmp(req_data->request_path, "/") == 0){
    char *buf = "HTTP/1.1 200 OK\r\n\r\n";
    int len = strlen(buf);

    if (send(client_fd, buf, len, 0) == -1){
      printf("Send Failed: %s\n", strerror(errno));
      return NULL;
    }
  }
  else if (strncmp(req_data->request_path,"/echo/", 6) == 0){
    char *str = req_data->request_path + 6;

    // Content-Length
    char res[MAX_LENGTH];
    sprintf(res, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %ld\r\n\r\n%s",strlen(str), str);

    if (send(client_fd, res, strlen(res) + 1, 0) == -1){
      printf("Send Failed: %s\n", strerror(errno));
      return NULL;
    }
  }
  else if (strncmp(req_data->request_path, "/user-agent", 11) == 0){

    char res[MAX_LENGTH];
    sprintf(res,"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %ld\r\n\r\n%s",
            strlen(req_data->agent_header), req_data->agent_header);

    if (send(client_fd, res, strlen(res) + 1, 0) == -1){
      printf("Send failed: %s\n", strerror(errno));
      return NULL;
    }
  }
  else if (strncmp(req_data->request_path, "/files/", 7) == 0){
    char *filename = req_data->request_path + 7;
    c_info->filename = filename;
    handle_file_request(c_info, req_data);
  }
  else{
    handle_not_found((void *)c_info);
  }

  free(c_info);
  free(req_data);
  shutdown(client_fd, SHUT_WR);
  close(client_fd);
  return 0;
}


void parse_request_data(char *buf, struct request_data* req){
  // TODO: Parse Headers
  char *data = strdup(buf);
  size_t str_len = strlen(data);

  memset(req, 0, sizeof(*req));

  size_t method_len = strcspn(data, " ");
  req->request_type = calloc(sizeof(char), method_len+1);
  memcpy(req->request_type, data, &data[method_len] - data);
  data += method_len + 1;

  size_t path_len = strcspn(data, " ");
  req->request_path = calloc(sizeof(char), path_len+1);
  memcpy(req->request_path, data, &data[path_len] - data);
  data += path_len + 1;

  size_t ver_len = strcspn(data, "\r\n");
  data += ver_len + 2;

  // Skip Headers
  size_t header_len;
  while ( (header_len = strcspn(data, "\r\n")) != 0){
    // Parse User-Agent
    if (strncmp(data, "User-Agent", 10) == 0){
      req->agent_header = calloc(sizeof(char), header_len - 12 + 1);
      char *val = data + 12;
      memcpy(req->agent_header, val, &data[header_len] - val);
    }
    data += header_len + 2;
  }

  data += 2;  // skip CRLF
  req->body = strdup(data);
}



void *handle_file_request(void *c, struct request_data* req){
  struct client_info *c_info = (struct client_info *)c;
  long long int client_fd = c_info->client_fd;
  char *filename = c_info->filename;
  char **args = c_info->args;
  int arg_len = c_info->arg_len;
  char *dir;

  // HACK: implement proper error handling
  if (arg_len > 1 && strcmp(args[1], "--directory") == 0){
    dir = args[2];
  }
  if (dir == NULL)
    return NULL;

  int len = strlen(dir) + strlen(filename) + 2;
  char path[len];
  snprintf(path, len,"%s/%s", dir, filename);


  if (strcmp(req->request_type, "GET") == 0){
    char *s = read_file(path);

    if (s == NULL){
      handle_not_found(c_info);
      return 0;
    }

    char res[MAX_LENGTH];
    sprintf(res, "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %ld\r\n\r\n%s", strlen(s), s);

    if (send(client_fd, res, strlen(res) + 1, 0) == -1){
      printf("Send Failed: %s\n", strerror(errno));
      return NULL;
    }

    free(s);
  }
  else if(strcmp(req->request_type, "POST") == 0){
    char *saveptr;
    char *msg = req->body;

    write_file(path, msg);

    char res[] = "HTTP/1.1 201 Created\r\n\r\n";

    if (send(client_fd, res, sizeof(res), 0) == -1){
      printf("Send Failed: %s\n", strerror(errno));
      return NULL;
    }
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

char *read_file(char *path){
  char *s = (char *)malloc(MAX_LENGTH);

  // TODO: Add error handling
  pthread_mutex_t lock;
  int rc = pthread_mutex_init(&lock, NULL);

  FILE *fptr = fopen(path, "r");

  if (fptr == NULL){
    return NULL;
  }

  // Critical Section
  pthread_mutex_lock(&lock);

  while ((fgets(s, MAX_LENGTH, fptr)) != NULL){
    continue;
  }
  fclose(fptr);

  pthread_mutex_unlock(&lock);

  return s;
}

void *write_file(char *path, char *msg){
  FILE *fptr = fopen(path, "w");

  if (fptr == NULL){
    return NULL;
  }

  pthread_mutex_t lock;
  int rc = pthread_mutex_init(&lock, NULL);

  // Critical Section
  pthread_mutex_lock(&lock);

  fputs(msg, fptr);
  fclose(fptr);

  pthread_mutex_unlock(&lock);

  return 0;
}
