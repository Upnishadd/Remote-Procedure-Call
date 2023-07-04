#define _POSIX_C_SOURCE 200112
#define _DEFAULT_SOURCE
#define MAX_DATA2_LEN 100000
#define MAX_BUFF_LEN 1024
#define MAX_PORT_LEN 5
#define MAX_IP_LEN 16
#define NONBLOCKING

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <stddef.h>
#include <arpa/inet.h>
#include <endian.h>
#include <stdint.h>
#include <math.h>
#include <pthread.h>
#include "linkedlists.h"
#include "rpc.h"

/* Checks if Function Name is Valid */
int valid_name(char *name);

/* Finds the Function in the server */
void find_function(int connfd, rpc_server *srv);

/* Verifies a valid function was sent by the client in a call */
node_t *call_function(int connfd, rpc_server *srv);

/* Read in data from the client */
rpc_data *read_client_data(int connfd);

/* Check if the data provided is valid */
int validate_data(int connfd, rpc_data *response_data, uint8_t size_of_int);

/* Handles all incoming and outgoing traffic with a client */
void* handle_client(void *srver);


struct rpc_server {
    int listenfd;
    struct addrinfo info;
    list_t* functions; 
    int clientfd;
    pthread_mutex_t lock;
};

rpc_server *rpc_init_server(int port) {

    if(!port) {
        perror("Invalid Port");
        return NULL;
    }

    rpc_server *srv = (rpc_server*)malloc(sizeof(*srv));
    assert(srv!=NULL);
    srv->functions = make_empty_list();

    srv->clientfd = -1;

    if (pthread_mutex_init(&srv->lock, NULL) != 0) {
        printf("\n mutex init has failed\n");
        return NULL;
    }

    // Code Adapted from Lecture Slides to create a socket
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    char port_str[MAX_PORT_LEN + 1];
    sprintf(port_str, "%d", port);
    int error = getaddrinfo(NULL, port_str, &hints, &res);
    if (error != 0) {
        perror("getaddrinfo failed");
        return NULL;
    }

    for (struct addrinfo *p = res; p!= NULL; p = p->ai_next) {
        if (p->ai_family == AF_INET6 && (srv->listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) != -1 ) {
            srv->info = *p;
            break;
        }
    }
    
    if (srv->listenfd < 0) {
        perror("listenfd failed");
        return NULL;
    }

    int opt_val = 1;
    if(setsockopt(srv->listenfd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    if (bind(srv->listenfd, srv->info.ai_addr, srv->info.ai_addrlen) < 0) {
        perror("bind");
		exit(EXIT_FAILURE);
    }
    freeaddrinfo(res);
    return srv;
}

int rpc_register(rpc_server *srv, char *name, rpc_handler handler) {

    // Verify Arguments are non Null
    if(srv == NULL || name == NULL || handler == NULL) {
        perror("Arguments are invalid");
        return -1;
    }

    // If name is valid as per business constraints
    if(valid_name(name) == 1) {
        data_t temp;
        strcpy(temp.name, name);
        temp.handler = handler;
        temp.num = srv->functions->count;
        insert_into_list(srv->functions, temp);
        return 1;
    }

    perror("Invalid Name");
    return -1;
}

void rpc_serve_all(rpc_server *srv) {

    // Verify Server Exists
    if(srv == NULL) {
        perror("Invalid Server");
        return;
    }

    // Listen for incoming clients
    if (listen(srv->listenfd, 10) < 0) {
        perror("listen");
		exit(EXIT_FAILURE);
    }

    // Accept clients via multi-threading to implement a persistent and non-blocking connection
    while(1) {

        // Accept a client
        struct sockaddr_in6 client_addr;
        socklen_t client_addr_size = sizeof(client_addr);
        srv->clientfd = accept(srv->listenfd, (struct sockaddr*) &client_addr, &client_addr_size);
        if(srv->clientfd < 0) {
            perror("accept");
            continue;
        }

        // Create a thread to handle a client
        pthread_t conn_thread;
        pthread_create(&conn_thread, NULL, handle_client, (void*) srv);
    }

}

struct rpc_client {
    int listenfd;
    int8_t srv_int_size;
};

struct rpc_handle {
    uint32_t num;
};

rpc_client *rpc_init_client(char *addr, int port) {
    rpc_client *cl = (rpc_client*)malloc(sizeof(*cl));
    assert(cl != NULL);

    // Verify arguments are non-null
    if(!port || !addr) {
        perror("Invalid arguments");
        return NULL;
    }
    cl->srv_int_size = 0;

    // Code Adapted from Week 9 Labs
    int error;
	struct addrinfo hints, *servinfo, *p;

    memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;

    char port_str[MAX_PORT_LEN + 1];
    sprintf(port_str, "%d", port);
    error = getaddrinfo(addr, port_str, &hints, &servinfo);
    if (error != 0) {
        perror("Invalid getaddrinfo");
        return NULL;
    }

    for (p = servinfo; p!= NULL; p = p->ai_next) {
        if (p->ai_family == AF_INET6 && (cl->listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) != -1 ) {
            if (connect(cl->listenfd, p->ai_addr, p->ai_addrlen) != -1) {break;}
            close(cl->listenfd);
        }
    }
    if (p == NULL) {
        perror("Invalid servinfo");
        return NULL;
    }

    freeaddrinfo(servinfo);
    return cl;
}

rpc_handle *rpc_find(rpc_client *cl, char *name) {
    // Find will always be called before Call hence, if the srv_int_size has not been intialised it can first be done in find
    if(cl->srv_int_size == 0) {
        // Read the servers int size
        int x = read(cl->listenfd, &cl->srv_int_size, sizeof(int8_t));
        if (x < 0) {
            perror("Unable to read server integer size");
            return NULL;
        }

        // Send clients int size
        int8_t size_of_int = sizeof(int);
        x = write(cl->listenfd, &size_of_int, sizeof(int8_t));
        if (x < 0) {
            perror("Couldnt send size");
        }
    }
    // Verify non null args
    if(cl == NULL || name == NULL) {
        perror("Invalid Arguments");
        return NULL;
    }
    if(valid_name(name) == -1) {
        perror("Invalid Name");
        return NULL;
    }

    // Tell server a find call is being done
    int n = write(cl->listenfd, "find", 5);
    if (n < 0) {
        perror("Falied to write call");
        return NULL;
    }
    
    // Send name
    n = write(cl->listenfd, name, MAX_BUFF_LEN);
    if (n < 0) {
        perror("Failed to write name");
        return NULL;
    }
    
    rpc_handle* temp = (rpc_handle*)malloc(sizeof(*temp));
    assert(temp!=NULL);
    
    n = read(cl->listenfd, &temp->num, sizeof(uint32_t));
    temp->num = ntohl(temp->num);
    
    if (n < 0) {
        perror("Failed to read function number");
        free(temp);
        return NULL;
    }
    if(temp->num == -1) {
        perror("Couldnt find function");
        free(temp);
        return NULL;
    }
    return temp;
}

rpc_data *rpc_call(rpc_client *cl, rpc_handle *h, rpc_data *payload) {
    if(cl == NULL || h == NULL || payload == NULL) {
        perror("Invalid Arguments");
        return NULL;
    }

    int64_t val = payload->data1;
    if(val >= pow(2, cl->srv_int_size * 8)) {
        perror("data1 is too large for server");
        return NULL;
    }
    uint64_t max_len = payload->data2_len;
    if(max_len >= MAX_DATA2_LEN) {
        perror("data2_len is invalid");
        return NULL;
    }
    uint32_t len = payload->data2_len;
    if(len >= pow(2, (cl->srv_int_size * 8))) {
        perror("data2_len is too large for server");
        return NULL;
    }

    if(len > 0 && payload->data2 == NULL) {
            perror("data2 invalid");
            return NULL;
    }
    if(len == 0 && payload->data2 != NULL) {
        perror("data2 invalid");
        return NULL;
    }
    if(len < 0) {
        perror("data2_len invalid");
        return NULL;
    }

    int n = write(cl->listenfd, "call", 5);
    if (n < 0) {
        perror("Failed to write call");
        return NULL;
    }

    uint32_t num = htonl(h->num);
    n = write(cl->listenfd, &num, sizeof(uint32_t));
    if (n < 0) {
        perror("Failed to function num");
        return NULL;
    }

    int64_t data1 = htobe64((int64_t) payload->data1);
    n = write(cl->listenfd, &data1, sizeof(data1));
    if (n < 0) {
        perror("Failed to write data1");
        return NULL;
    }

    uint32_t data2_len = htonl(payload->data2_len);
    n = write(cl->listenfd, &data2_len, sizeof(data2_len));
    if (n < 0) {
        perror("Failed to write data2_len");
        return NULL;
    }
    
    if(payload->data2_len != 0) {

        void *data2 = payload->data2;
        n = write(cl->listenfd, data2, payload->data2_len);
        if (n < 0) {
        perror("Failed to write data2");
        return NULL;
        }
    }


    uint_least8_t found;
    n = read(cl->listenfd, &found, sizeof(uint_least8_t)); 
    if (n < 0 || found == 0) {
        perror("Failed to read found, or function was not found");
        return NULL;
    }
    found = 0;

    rpc_data *server_data = (rpc_data*) malloc(sizeof(*server_data));
    assert(server_data);
    memset(server_data, 0, sizeof(rpc_data));
    
    int64_t server_data1;
    
    server_data->data2_len = 0;

    n = read(cl->listenfd, &found, sizeof(uint_least8_t)); 
    if (n < 0 || found == 0) {
        perror("Bad Return Data");
        return NULL;
    }
    found = 0;

    n = read(cl->listenfd, &server_data1, sizeof(int64_t));
    if (n < 0) {
        perror("Failed to read response data1");
        return NULL;
    }
    server_data->data1 = (int) be64toh(server_data1);

    n = read(cl->listenfd, &server_data->data2_len, sizeof(uint32_t));
    if (n < 0) {
        perror("Failed to read response data2_len");
        return NULL;
    }
    server_data->data2_len = ntohl(server_data->data2_len);

    if(server_data->data2_len != 0) {
        server_data->data2 = malloc(server_data->data2_len);
        n = read(cl->listenfd, server_data->data2, server_data->data2_len);
        if (n < 0) {
            perror("Failed to read response data2");
            return NULL;
        }
    }
    else {
        server_data->data2 = NULL;
    }
    return server_data;
}

void rpc_close_client(rpc_client *cl) {
    if(cl == NULL) {
        return;
    }
    int n = write(cl->listenfd, "clos", 5);
    if (n < 0) {return;}
    free(cl);
}

void rpc_data_free(rpc_data *data) {
    if (data == NULL) {
        return;
    }
    if (data->data2 != NULL) {
        free(data->data2);
    }
    free(data);
}

int valid_name(char *name) {
    for(int i = 0; i < strlen(name); i++) {
        if((int) name[i] >= 32 && (int) name[i] <= 126) {
            continue;
        }
        else {
            return -1;
        }
    }
    return 1;
}

void find_function(int connfd, rpc_server *srv) {
    char buffer[MAX_BUFF_LEN];
    int n = read(connfd, buffer, MAX_BUFF_LEN);
    uint32_t num = -1;
    if(n < 0) {
        write(connfd, &num, sizeof(uint32_t));
        return;
    }
    buffer[strlen(buffer)] = '\0';
    node_t *curr = srv->functions->head;
    while(curr) {
        if(strcmp(curr->data.name, buffer) == 0) {
            num = curr->data.num;
            break;
        }
        curr = curr->next;
    }
    num = htonl(num);
    write(connfd, &num, sizeof(uint32_t));
}

node_t *call_function(int connfd, rpc_server *srv) {
    uint32_t num = -1;
    int n = read(connfd, &num, sizeof(uint32_t));
    if (n < 0) { 
        perror("Invalid read of function number");
        n = write(connfd, "", 1);
    }
    num = ntohl(num);

    node_t *curr = srv->functions->head;
    while(curr) {
        if(curr->data.num == num) {
            break;
        }
        curr = curr->next;
    }

    uint_least8_t found = 0;
    if(curr) {
        found = 1;
    }
    n = write(connfd, &found, sizeof(uint_least8_t)); 
    return curr;
}

rpc_data *read_client_data(int connfd) {
    rpc_data *client_data = (rpc_data*)malloc(sizeof(*client_data));
    memset(client_data, 0, sizeof(rpc_data));
    int64_t data1 = -1;
    uint32_t data2_len = 0;
    assert(client_data);

    int n = read(connfd, &data1, sizeof(int64_t));
    if (n < 0) { 
        perror("Invaid read of data1");
        n = write(connfd, "", 1);
        rpc_data_free(client_data);
        return NULL;
    }
    client_data->data1 = be64toh(data1);

    n = read(connfd, &data2_len, sizeof(uint32_t));
    if (n < 0) { 
        perror("Invalid read of data2_len");
        n = write(connfd, "", 1);
        rpc_data_free(client_data);
        return NULL;
    }
    client_data->data2_len = ntohl(data2_len);

    if(client_data->data2_len != 0) {
        void *data_2 = malloc(client_data->data2_len);
        assert(data_2);
        n = read(connfd, data_2, client_data->data2_len);
        if (n < 0) { 
            perror("Invalid read of data2");
            n = write(connfd, "", 1);
            rpc_data_free(client_data);
            return NULL;
        }
        client_data->data2 = data_2;
    }
    else {
        client_data->data2 = NULL;
    }
    return client_data;
}

int validate_data(int connfd, rpc_data *response_data, uint8_t size_of_int) {
    uint_least8_t valid_data = 1;
    if(response_data == NULL) {
        perror("invalid response");
        valid_data = 0;
        write(connfd, &valid_data, sizeof(uint_least8_t));
        return -1;
    }

    int64_t val = response_data->data1;
    uint32_t len = response_data->data2_len;
    if(val >= pow(2, size_of_int * 8)) {
        perror("Response Integer Too Large");
        valid_data = 0;
    }
    else if(len >= pow(2, (size_of_int * 8))) {
        perror("Response Data 2 Len Too large");
        valid_data = 0;
    }
    else if(len != 0 && response_data->data2 == NULL) {
        perror("Response Data 2 invalid");
        valid_data = 0;
    } else if(len == 0 && response_data->data2 != NULL) {
        perror("Response Data 2 len invalid");
        valid_data = 0;
    } else if(len < 0) {
        perror("data2_len invalid");
        valid_data = 0;
    }

    write(connfd, &valid_data, sizeof(uint_least8_t)); 
    if(valid_data == 0) {
        return -1;
    }
    return 1;
}

void* handle_client(void *srver) {
    rpc_server *srv = (rpc_server*) srver;
    pthread_mutex_lock(&srv->lock);
    int connfd = srv->clientfd;
    srv->clientfd = -1;
    pthread_mutex_unlock(&srv->lock);
    int8_t size_of_int = sizeof(int);
    int x = write(connfd, &size_of_int, sizeof(int8_t));
    if (x < 0) {
        perror("Couldnt send size");
    }
    x = read(connfd, &size_of_int, sizeof(int8_t));
    if (x < 0) {
        perror("Unable to read client integer size");
    }
    while(1) {
        char buffer[MAX_BUFF_LEN];
        int n = read(connfd, buffer, 5);
        if (n < 0) {
            perror("Invaid read of call type");
            n = write(connfd, "", 1);
        }
        if ( strcmp(buffer, "find") == 0) {
            find_function(connfd, srv);
        } 
        else if (strcmp(buffer, "call") == 0) {

            node_t *curr = call_function(connfd, srv);
            rpc_data *client_data = read_client_data(connfd);

            if(client_data == NULL) {
                continue;
            }

            // Execute Function
            rpc_handler handler = (rpc_handler) curr->data.handler;
            rpc_data *response_data = handler(client_data);

            // Validate Data 
            if(validate_data(connfd, response_data, size_of_int) == -1) {
                rpc_data_free(client_data);
                rpc_data_free(response_data);
                continue;
            }


            // Write to Client
            int64_t client_data1 = htobe64((int64_t) response_data->data1);
            n = write(connfd, &client_data1, sizeof(client_data1));
            if (n < 0) {
                perror("Invalid write of response data1");
                n = write(connfd, "", 1);
            }

            uint32_t client_data2_len = htonl(response_data->data2_len);
            n = write(connfd, &client_data2_len, sizeof(client_data2_len));
            if (n < 0) {
                perror("Invalid write of response data2_len");
                n = write(connfd, "", 1);
            }
            if(response_data->data2_len > 0) {
                n = write(connfd, response_data->data2, response_data->data2_len);
                if (n < 0) {
                    perror("Invalid write of response data2");
                    n = write(connfd, "", 1);
                }
            }

            rpc_data_free(client_data);
            rpc_data_free(response_data);

        }
        else if (strcmp(buffer, "clos") == 0) {
            close(connfd);
            break;
        }
        strcpy(buffer, "");
    }
    return NULL;
}