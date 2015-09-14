#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <modbus.h>
#include <errno.h>

#define SERVER_ADDR "127.0.0.1"
#define SERVER_PORT 10000
#define DATA_LENGTH 256

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

	printf("%s\n", current_object->word); //print the word stored in the structure
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

static void start_server(void) {
	#define QUIT_STRING "exit"
	int socket_fd;
	struct sockaddr_in server_address;
	struct sockaddr_in client_address;
	socklen_t client_address_len;
	int want_quit = 0;
	fd_set read_fds;
	int bytes;
	char data[DATA_LENGTH];
	pthread_t print_thread;
	
	fprintf(stderr, "Starting server\n");

	pthread_create(&print_thread, NULL, print_func, NULL);
	
	socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
	
	memset(&server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(SERVER_PORT);
	server_address.sin_addr.s_addr = INADDR_ANY;

	if (bind(socket_fd, (struct sockaddr *) &server_address,
	sizeof(server_address)) < 0) {
		fprintf(stderr, "Bind failed\n");
		exit(1);
	}

	FD_ZERO(&read_fds);
	FD_SET(socket_fd, &read_fds);
	while (!want_quit) {
	/* Wait until data has arrived */
		if (select(socket_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
			fprintf(stderr, "Select failed\n");
			exit(1);
		}
		if (!FD_ISSET(socket_fd, &read_fds)) continue;
		/* Read input data */
		bytes = recvfrom(socket_fd, data, sizeof(data), 0,
		(struct sockaddr *) &client_address,
		&client_address_len);
		if (bytes < 0) {
			fprintf(stderr, "Recvfrom failed\n");
			exit(1);
		}
		/* Process data */
		if(!strcmp(data, QUIT_STRING)) want_quit=1;
		else add_to_list(data);
	}
	list_flush();
}

static void start_client(int count) {
	int sock_fd;
	struct sockaddr_in addr;
	char input_word[DATA_LENGTH];
	fprintf(stderr, "Accepting %i input strings\n", count);
	if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		fprintf(stderr, "Socket failed\n");
		exit(1);
	}
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_PORT);
	addr.sin_addr.s_addr = inet_addr(SERVER_ADDR);
	if (connect(sock_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		fprintf(stderr, "Connect failed\n");
		exit(1);
	}
	while (scanf("%256s", input_word) != EOF) {
		if (send(sock_fd, input_word, strlen(input_word) + 1, 0) < 0) {
			fprintf(stderr, "Send failed\n");
			exit(1);
		}
		else
			if(strcmp(input_word, "exit")==0) exit(0);
		if (!--count) break;
	}
}

int modb(void){
	int i;
	int rc;
	uint16_t tab_reg[128];
	char sending[64];
	modbus_t *ctx;
	
	ctx = modbus_new_tcp("140.159.153.159", 502);
	if (ctx == NULL) {
    		fprintf(stderr, "Unable to allocate libmodbus context\n");
        	return -1;
	}

	if (modbus_connect(ctx) == -1) {
  	printf("con failed");
	fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
	        modbus_free(ctx);
		return -1;
	}

	rc = modbus_read_registers(ctx, 0, 3, tab_reg);
	if (rc == -1) {
    		fprintf(stderr, "%s\n", modbus_strerror(errno));
        	return -1;
	}

	for (i=0; i < rc; i++) {
	   sprintf(sending,"reg[%d]=%d (0x%X)", i, tab_reg[i], tab_reg[i]);
	   add_to_list(sending);
	   // printf("reg[%d]=%d (0x%X)\n", i, tab_reg[i], tab_reg[i]);
	}

	    modbus_close(ctx);
	    modbus_free(ctx);

}

int main(int argc, char **argv) {
	int c;
	int option_index = 0;
	int count = -1;
	int server = 0;
	static struct option long_options[] = {
		{"count", required_argument, 0, 'c'},
		{"server", no_argument, 0, 's'},
		{0, 0, 0, 0 }
	};
	pthread_t print_thread;
	pthread_create(&print_thread, NULL, print_func, NULL);
	while (1) {
		/* c = getopt_long(argc, argv, "c:s", long_options, &option_index);
		if (c == -1)
			break;
		switch (c) {
			case 'c':
				count = atoi(optarg);
				break;
			case 's':
				server = 1;
				break;
		}*/
	modb();	
	sleep(1);
	}
	//if (server) start_server();
	//else start_client(count);
	return 0;
}
