#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>

/* Linked list object (initial structure definition) */
typedef struct s_word_object word_object;
struct s_word_object {
    char *word;
    word_object *next;
};

/* list_head: Shared between two threads, must be accessed with list_lock */
static word_object *list_head;
static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t list_data_ready = PTHREAD_COND_INITIALIZER;
static pthread_cond_t list_data_flush = PTHREAD_COND_INITIALIZER;

/* Add object to list */
static void add_to_list(char *word) {
    word_object *last_object, *tmp_object;

    char *tmp_string = strdup(word); //create a pointer to the 
    tmp_object = malloc(sizeof(word_object)); //create a new tmp_object

    pthread_mutex_lock(&list_lock);

    if (list_head == NULL) {  //initialisation of the first object
	last_object = tmp_object; //assigns the last object pointer to the temp object	
	list_head = last_object; //assigns the head of the list pointer to the temp object
    } else {
	last_object = list_head; //set Last object pointer to the start of the structure
	while (last_object->next) { //while the current objects next does not point to NULL
	    last_object = last_object->next; //change the last object pointer to the next object
	}
	last_object->next = tmp_object; //set the next pointer to the temp object
	last_object = last_object->next; // set the last object position to the temp object
    }
    last_object->word = tmp_string; //assign the pointer to the input string to the temp object
    last_object->next = NULL; //assign the next of the temp object to a NULL pointer

    pthread_mutex_unlock(&list_lock);
    pthread_cond_signal(&list_data_ready);
}

static word_object *list_get_first(void) { //grabs the current header object and assigns the next object in the structure and the new header
    word_object *first_object;

    first_object = list_head; // grab the current list header

    list_head = list_head->next; //set the next element as the list header

    return first_object; //return the list header obtained above
}

static void *print_func(void *arg) {
    word_object *current_object;

    fprintf(stderr, "Print thread starting\n");

    while(1) {
	pthread_mutex_lock(&list_lock);

	while (list_head == NULL) {
	    pthread_cond_wait(&list_data_ready, &list_lock);
	}

	current_object = list_get_first(); //use the function to grab the next element of the list

	pthread_mutex_unlock(&list_lock);

	printf("Print thread: %s\n", current_object->word); //print the word stored in the structure
	free(current_object->word); //free the memorry location for the word object
	free(current_object); //free the memory location for the current object

	pthread_cond_signal(&list_data_flush);
    }

    /* Silence compiler warning */
    return arg;
}

static void list_flush(void) {
    pthread_mutex_lock(&list_lock);

    while (list_head != NULL) {
	pthread_cond_signal(&list_data_ready);
	pthread_cond_wait(&list_data_flush, &list_lock);
    }

    pthread_mutex_unlock(&list_lock);
}

int main(int argc, char **argv) {
    char input_word[256];
    int c;
    int option_index = 0;
    int count = -1;
    pthread_t print_thread;
    static struct option long_options[] = {
	{"count",   required_argument, 0, 'c'},
	{0,         0,                 0,  0 }
    };

    while (1) {
	c = getopt_long(argc, argv, "c:", long_options, &option_index);
	if (c == -1)
	    break;

	switch (c) {
	    case 'c':
		count = atoi(optarg);
		break;
	}
    }

    /* Start new thread for printing */
    pthread_create(&print_thread, NULL, print_func, NULL);

    fprintf(stderr, "Accepting %i input strings\n", count);

    while (scanf("%256s", input_word) != EOF) {
	add_to_list(input_word);
	if (!--count) break;
    }

    list_flush();

    return 0;
}
