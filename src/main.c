#include <ctype.h>
#include <stdio.h>
#include <netdb.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <zlib.h>

#define MAX_LENGTH 4096
// size of memory used by zlib functions
#define CHUNK 16384

static char* directory;

struct client_info {
  long long int client_fd;
};

struct header {
  char *name;
  char *value;
};

struct header_data {
  int length;
  struct header** headers;
};

struct request_data {
  char *request_type; // GET/POST
  char *request_path; 
  char *body;
  struct header_data* header_data;
};

void *handle_file_request(void *, struct request_data*);
void *write_file(char *, char *);
void parse_request_data(char *buf, struct request_data*);
void *handle_request_thread(void *);
void *handle_not_found(void *);
char *read_file(char *);
char **split_string(char *, char *,int *);
void send_message(long long int, char *);
unsigned char *compress_zlib(char *, int*);

int main(int argc, char *argv[]) {
  for (int i = 0; i < argc; i++){
    if (strcmp(argv[i], "--directory") == 0 && argc > i + 1){
      directory = argv[i + 1];
    }
  }

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

  pthread_t thread;
  pthread_attr_t thread_attr;
  pthread_attr_init(&thread_attr);
  pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);

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

    pthread_create(&thread, &thread_attr, handle_request_thread, c_info);
  }

  pthread_attr_destroy(&thread_attr);
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
    size_t headers_len = req_data->header_data->length;
    struct header **headers = req_data->header_data->headers;

    char *client_encodings;
    for (int i = 0; i < headers_len; i++){
      if (strcmp(headers[i]->name, "Accept-Encoding") == 0){
        client_encodings = headers[i]->value;
      }
    }

    char res[MAX_LENGTH] = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n";

    if (client_encodings == NULL){
      char s[MAX_LENGTH];
      sprintf(s, "Content-Length: %ld\r\n\r\n%s", strlen(str), str);
      strcat(res, s);
      send_message(client_fd, res);
      return 0;
    }

    int len;
    unsigned char* c_str = compress_zlib(str, &len);

    int clients_len;
    char **client_vals = split_string(client_encodings, ",", &clients_len);

    char *encoding;
    for (int i = 0; i < clients_len; i++){
      encoding = client_vals[i];

      if (strcmp(encoding, "gzip") == 0){
        char s[MAX_LENGTH];
        sprintf(s, "Content-Encoding: gzip\r\nContent-Length: %d\r\n\r\n", len);
        strcat(res, s);
        send_message(client_fd, res);
        send(client_fd, c_str, len, 0);
        return 0;
      }
    }
    strcat(res, "\r\n");

    send_message(client_fd, res);
    return 0;
  }
  else if (strncmp(req_data->request_path, "/user-agent", 11) == 0){
    char res[MAX_LENGTH];
    size_t headers_len = req_data->header_data->length;
    struct header **headers = req_data->header_data->headers;

    char *agent_header;
    int i;
    for (i = 0; i < headers_len; i++){
      if (strcmp(headers[i]->name, "user-agent") == 0 || strcmp(headers[i]->name, "User-Agent") == 0)
      {
        agent_header = headers[i]->value;
      }
    }
    sprintf(res,"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %ld\r\n\r\n%s",
            strlen(agent_header), agent_header);

    if (send(client_fd, res, strlen(res)+1, 0) == -1){
      printf("Send failed: %s\n", strerror(errno));
      return NULL;
    }
  }
  else if (strncmp(req_data->request_path, "/files/", 7) == 0){
    char *filename = req_data->request_path + 7;
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
  size_t header_len = 0;
  struct header** headers = malloc(0);
  while (data[0] != '\r' || data[1] != '\n'){
    header_len++;
    void *new_headers = realloc(headers, sizeof(struct header *) * header_len);
    headers = (struct header **)new_headers;

    size_t name_len = strcspn(data, ":");
    char *name = calloc(sizeof(char), name_len+1);
    memcpy(name, data, &data[name_len] - data);
    name[name_len] = '\0';
    data += name_len + 1;

    while(isspace(*data)){
      data++; 
    }
    size_t val_len = strcspn(data, "\r\n");
    char *value = calloc(sizeof(char), val_len+1);
    memcpy(value, data, &data[val_len] - data);
    value[val_len] = '\0';
    data += val_len + 2;

    headers[header_len - 1] = malloc(sizeof(struct header *));
    headers[header_len - 1]->name = name;
    headers[header_len - 1]->value = value;
  }

  struct header_data* header_data = malloc(sizeof(struct header_data));
  header_data->length = header_len;
  header_data->headers = headers;
  req->header_data = header_data;
  req->body = strdup(data) + 2;
}



void *handle_file_request(void *c, struct request_data* req){
  struct client_info *c_info = (struct client_info *)c;
  long long int client_fd = c_info->client_fd;
  char *filename = req->request_path + 7;

  int len = strlen(directory) + strlen(filename) + 2;
  char path[len];
  snprintf(path, len,"%s/%s", directory, filename);

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

  pthread_mutex_t lock;
  pthread_mutex_init(&lock, NULL);

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

char **split_string(char *string, char* delim,int *clients_len){
  char *data = strdup(string);
  char **vals = malloc(0);
  int client_len = 0;

  while (data[0] != '\0'){
    client_len++;
    vals = (char **)realloc(vals, sizeof(char *)*client_len);

    size_t val_len = strcspn(data, delim);
    char *val = calloc(sizeof(char), val_len+1);
    memcpy(val, data, &data[val_len] - data);
    val[val_len] = '\0';
    data += val_len + 1;

    while(isspace(*data)){
      data++;
    }

    vals[client_len - 1] = val;
  }
  *clients_len = client_len;
  return vals;
}

void send_message(long long int client_fd, char *msg){
  if (send(client_fd, msg, strlen(msg), 0) == -1){
    printf("Send Failed: %s\n", strerror(errno));
  }
}

unsigned char *compress_zlib(char *msg, int* len){
  z_stream zstrm;
  int have;
  int ret;
  unsigned char *out = malloc(CHUNK);
  memset(out, 0, CHUNK);

  zstrm.zalloc = Z_NULL;
  zstrm.zfree = Z_NULL;
  zstrm.opaque = Z_NULL;
  ret = deflateInit2(&zstrm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 0x1F, 8, Z_DEFAULT_STRATEGY);

  zstrm.next_in = (unsigned char *) msg;
  zstrm.avail_in = strlen(msg);

  do {
    zstrm.avail_out = CHUNK;
    zstrm.next_out = out;
    deflate(&zstrm, Z_FINISH);
    have = CHUNK - zstrm.avail_out;
    *len = have;
  }while(zstrm.avail_out == 0);
  deflateEnd(&zstrm);

  return out;
}
