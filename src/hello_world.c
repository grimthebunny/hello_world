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

#include <libbacnet/address.h>
#include <libbacnet/device.h>
#include <libbacnet/handlers.h>
#include <libbacnet/datalink.h>
#include <libbacnet/bvlc.h>
#include <libbacnet/client.h>
#include <libbacnet/txbuf.h>
#include <libbacnet/tsm.h>
#include <libbacnet/bactext.h>
#include <libbacnet/ai.h>
#include "bacnet_namespace.h"

#define BACNET_INSTANCE_NO 120
#define BACNET_PORT 0xBAC1
#define BACNET_INTERFACE "lo"
#define BACNET_DATALINK_TYPE "bvlc"
#define BACNET_SELECT_TIMEOUT_MS 1	/* ms */
#define RUN_AS_BBMD_CLIENT 1

#define RUN_AS_BBMD_CLIENT	1
#if RUN_AS_BBMD_CLIENT
#define BACNET_BBMD_PORT	0xBAC0
#define BACNET_BBMD_ADDRESS	"127.0.0.1"
#define BACNET_BBMD_TTL	90
#endif

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
static pthread_mutex_t timer_lock = PTHREAD_MUTEX_INITIALIZER;

static bacnet_object_functions_t server_objects[] = {
	{bacnet_OBJECT_DEVICE,
		NULL,
		bacnet_Device_Count,
		bacnet_Device_Index_To_Instance,
		bacnet_Device_Valid_Object_Instance_Number,
		bacnet_Device_Object_Name,
		bacnet_Device_Read_Property_Local,
		bacnet_Device_Write_Property_Local,
		bacnet_Device_Property_Lists,
		bacnet_DeviceGetRRInfo,
		NULL, /* Iterator */
		NULL, /* Value_Lists */
		NULL, /* COV */
		NULL, /* COV Clear */
		NULL /* Intrinsic Reporting */
	},
	{bacnet_OBJECT_ANALOG_INPUT,
		bacnet_Analog_Input_Init,
		bacnet_Analog_Input_Count,
		bacnet_Analog_Input_Index_To_Instance,
		bacnet_Analog_Input_Valid_Instance,
		bacnet_Analog_Input_Object_Name,
		bacnet_Analog_Input_Read_Property,
		bacnet_Analog_Input_Write_Property,
		bacnet_Analog_Input_Property_Lists,
		NULL /* ReadRangeInfo */ ,
		NULL /* Iterator */ ,
		bacnet_Analog_Input_Encode_Value_List,
		bacnet_Analog_Input_Change_Of_Value,
		bacnet_Analog_Input_Change_Of_Value_Clear,
		bacnet_Analog_Input_Intrinsic_Reporting},
		{MAX_BACNET_OBJECT_TYPE}
	};

static void register_with_bbmd(void) {
#if RUN_AS_BBMD_CLIENT
	/* Thread safety: Shares data with datalink_send_pdu */
	bacnet_bvlc_register_with_bbmd(
	bacnet_bip_getaddrbyname(BACNET_BBMD_ADDRESS),
	htons(BACNET_BBMD_PORT),
	BACNET_BBMD_TTL);
	#endif
}

static void *minute_tick(void *arg) {
	while (1) {
		pthread_mutex_lock(&timer_lock);
		/* Expire addresses once the TTL has expired */
		bacnet_address_cache_timer(60);
		/* Re-register with BBMD once BBMD TTL has expired */
		register_with_bbmd();
		/* Update addresses for notification class recipient list
		 * * Requred for INTRINSIC_REPORTING
		 * * bacnet_Notification_Class_find_recipient(); */
		/* Sleep for 1 minute */
		pthread_mutex_unlock(&timer_lock);
		sleep(60);
	}
	return arg;
}

static void *second_tick(void *arg) {
	while (1) {
		pthread_mutex_lock(&timer_lock);
		/* Invalidates stale BBMD foreign device table entries */
		bacnet_bvlc_maintenance_timer(1);
		/* Transaction state machine: Responsible for retransmissions and ack
		 * * checking for confirmed services */
		bacnet_tsm_timer_milliseconds(1000);
		/* Re-enables communications after DCC_Time_Duration_Seconds
		 * * Required for SERVICE_CONFIRMED_DEVICE_COMMUNICATION_CONTROL
		 * * bacnet_dcc_timer_seconds(1); */
		/* State machine for load control object
		 * * Required for OBJECT_LOAD_CONTROL
		 * * bacnet_Load_Control_State_Machine_Handler(); */
		/* Expires any COV subscribers that have finite lifetimes
		 * * Required for SERVICE_CONFIRMED_SUBSCRIBE_COV
		 * * bacnet_handler_cov_timer_seconds(1); */
		/* Monitor Trend Log uLogIntervals and fetch properties
		 * * Required for OBJECT_TRENDLOG
		 * * bacnet_trend_log_timer(1); */
		/* Run [Object_Type]_Intrinsic_Reporting() for all objects in device
		 * * Required for INTRINSIC_REPORTING
		 * * bacnet_Device_local_reporting(); */
		/* Sleep for 1 second */
		pthread_mutex_unlock(&timer_lock);
		sleep(1);
	}
	return arg;
}

static void ms_tick(void) {
/* Updates change of value COV subscribers.
 * * Required for SERVICE_CONFIRMED_SUBSCRIBE_COV
 * * bacnet_handler_cov_task(); */
}

#define BN_UNC(service, handler) \
	bacnet_apdu_set_unconfirmed_handler( \
	SERVICE_UNCONFIRMED_##service, \
	bacnet_handler_##handler)
	
#define BN_CON(service, handler) \
	bacnet_apdu_set_confirmed_handler( \
	SERVICE_CONFIRMED_##service, \
	bacnet_handler_##handler)


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
