#define MAX_NAME_LEN 1000

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>

typedef struct {
    char name[MAX_NAME_LEN + 1];
    int32_t num;
    void* handler;
} data_t;

typedef struct node node_t;

struct node {
	data_t data;
	node_t *next;
};

typedef struct{
    node_t *head;
    node_t *foot;
    int count;
} list_t;


// Malloc an empty linked list
list_t *make_empty_list(void);

// Insert into a linked list
void *insert_into_list(list_t *list, data_t value);

// Free all nodes of the list
void free_list(list_t *list);