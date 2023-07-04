#include "linkedlists.h"

// Malloc an empty linked list
list_t *make_empty_list(void) {
	list_t* list;
	list = (list_t*)malloc(sizeof(*list));
	assert(list!=NULL);
	list->head = NULL;
    list->foot = NULL;
    list->count = 0;
	return list;
}

// Insert into a linked list
void *insert_into_list(list_t *list, data_t value) {
    node_t *temp;
	temp = (node_t*)malloc(sizeof(*temp));
	assert(list!=NULL && temp!=NULL);
    temp->data = value;
	temp->next = NULL;
    if (list->foot == NULL) {
        list->head = list->foot = temp;
        list->count++;
    }
    else {
        node_t *curr = list->head;
        while(curr) {
            if(strcmp(curr->data.name, temp->data.name) == 0) {
                curr->data.handler = temp->data.handler;
                free(temp);
                break;
            }
            else if(curr->next != NULL) {
                curr = curr->next;
            }
            else {
                curr->next = temp;
                list->foot = temp;
                curr = curr->next->next;
                list->count++;
            }
        }
    }
    return 0;
}

// Free all nodes of the list
void free_list(list_t *list) {
	node_t *curr, *prev;
	assert(list!=NULL);
	curr = list->head;
	while (curr != NULL) {
		prev = curr;
		curr = curr->next;
		free(prev);
	}
}