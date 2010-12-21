/*
 * Copyright (c) 2005, Bruno Randolf <bruno.randolf@4g-systems.biz>
 * Copyright (c) 2004, Andreas Tonnesen(andreto-at-olsr.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the UniK olsr daemon nor the names of its contributors
 *   may be used to endorse or promote products derived from this software
 *   without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * Example plugin for olsrd.org OLSR daemon
 * Only the bare minimum
 */

#include <stdio.h>
#include <string.h>

#include "../../../src/olsrd_plugin.h"

#include "olsrd_plugin.h"
#include "olsr.h"
#include "defs.h"
#include "scheduler.h"

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>


#define PLUGIN_INTERFACE_VERSION 5

struct plc_data {
	u_int8_t mac[6];
	u_int8_t lq;
	u_int8_t nlq;
};

static void update_plc_data(void);
static void deserialize_stations_data(unsigned char *buff);
static void print_plc_data(void);

struct plc_data *p_data;
u_int8_t p_size_t;
int socket_fd;
struct sockaddr_un name;

/****************************************************************************
 *                Functions that the plugin MUST provide                    *
 ****************************************************************************/

/**
 * Plugin interface version
 * Used by main olsrd to check plugin interface version
 */
int olsrd_plugin_interface_version(void) {
	return PLUGIN_INTERFACE_VERSION;
}

static int set_plugin_test(const char *value, void *data __attribute__ ((unused)),
		set_plugin_parameter_addon addon __attribute__ ((unused))) {
	printf("\n*** Hybrid PLC: parameter test: %s\n", value);
	return 0;
}

/**
 * Register parameters from config file
 * Called for all plugin parameters
 */
static const struct olsrd_plugin_parameters plugin_parameters[] = { { .name =
		"test", .set_plugin_parameter = &set_plugin_test, .data = NULL }, };

void olsrd_get_plugin_parameters(const struct olsrd_plugin_parameters **params,
		int *size) {
	*params = plugin_parameters;
	*size = sizeof(plugin_parameters) / sizeof(*plugin_parameters);
}

/**
 * Initialize plugin
 * Called after all parameters are passed
 */
int olsrd_plugin_init(void) {
	int pid;
	printf("*** Hybrid PLC: plugin_init\n");
	char current_path[FILENAME_MAX];
	getcwd(current_path, sizeof(current_path));
	printf ("The current working directory is %s", current_path);

	/* call a function from main olsrd */
	olsr_printf(2, "*** Hybrid PLC: printed this with olsr_printf\n");
	if ((pid = fork()) == -1)
		perror("fork error");
	else if (pid == 0) {
		execlp("./faifa_proxy/faifa_proxy.o", "faifa_proxy", NULL);
		printf("Return not expected. Must be an execlp error.\n");
	} else {
		printf("sono nel PLUGIN!!!\n");
		olsr_start_timer(2 * MSEC_PER_SEC, 0, OLSR_TIMER_PERIODIC, &update_plc_data, NULL, 0);
	}
	return 1;
}

static void deserialize_stations_data(unsigned char *buff) {
	printf("Alloco: %d\n", p_size_t * sizeof(*p_data));
	p_data = (struct plc_data *) malloc(p_size_t * sizeof(*p_data));
	int i;
//	for (i = 0; i < p_size_t * sizeof(*p_data); i++) {
//		printf("Buff[%d]: %x\n",i,buff[i]);
//	}


	for (i = 0; i < p_size_t; i++) {
		p_data[i].mac[0] = buff[0 + 8 * i];
		p_data[i].mac[1] = buff[1 + 8 * i];
		p_data[i].mac[2] = buff[2 + 8 * i];
		p_data[i].mac[3] = buff[3 + 8 * i];
		p_data[i].mac[4] = buff[4 + 8 * i];
		p_data[i].mac[5] = buff[5 + 8 * i];
		p_data[i].lq = buff[6 + 8 * i];
		p_data[i].nlq = buff[7 + 8 * i];
	}
}

static void print_plc_data(void) {
	int i;
	for (i = 0; i < p_size_t; i++) {
		printf("Station MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n",
				p_data[i].mac[0], p_data[i].mac[1], p_data[i].mac[2],
				p_data[i].mac[3], p_data[i].mac[4], p_data[i].mac[5]);
		printf("LQ: %d\n", p_data[i].lq);
		printf("NLQ: %d\n\n", p_data[i].nlq);
	}
}

static void update_plc_data(void) {
	const char* const socket_name = "faifaproxy";
	/* Create the socket.  */
	socket_fd = socket(PF_LOCAL, SOCK_STREAM, 0);
	/* Store the server's name in the socket address.  */
	name.sun_family = AF_LOCAL;
	strcpy(name.sun_path, socket_name);
	printf("socket plugin: %s\n", name.sun_path);

	/* Connect the socket.  */
	int error;
	error = 1;
	while (error != 0) {
		error = connect(socket_fd, &name, SUN_LEN (&name));
		if (error == 0) {
			printf("socket ok!\n");
			printf("update PLC data!\n");
			char m = 'p';
			u_int8_t n_stas;
			int length;
			unsigned char *buff;
			send(socket_fd, &m, sizeof(m), 0);
			printf("Poll mandato!\n");
			if (recv(socket_fd, &p_size_t, sizeof(p_size_t), 0) == 0) {
				printf("Non ricevo p_size_t!\n");
				//return;
			}
			printf("Numero di stazioni presenti ricevuto: %d\n", p_size_t);
			length = p_size_t * sizeof(struct plc_data);
			buff = (unsigned char*) malloc(length);
			if (recv(socket_fd, buff, length, 0) == 0) {
				printf("Non ricevo buff!\n");
				//return;
			}
			recv(socket_fd, buff, length, 0);
			deserialize_stations_data(buff);
			//print_plc_data();
			close(socket_fd);

		} else {
			printf("ERROR: %s\n", strerror(errno));
		}
	}
}

/****************************************************************************
 *       Optional private constructor and destructor functions              *
 ****************************************************************************/

/* attention: make static to avoid name clashes */

static void my_init(void) __attribute__ ((constructor));
static void my_fini(void) __attribute__ ((destructor));

/**
 * Optional Private Constructor
 */
static void my_init(void) {
	printf("*** Hybrid PLC: constructor\n");
}

/**
 * Optional Private Destructor
 */
static void my_fini(void) {
	char m = 'q';
	unsigned char *buff;
	send(socket_fd, &m, sizeof(m), 0);
	//unlink(name.sun_path);
	printf("*** Hybrid PLC: destructor\n");
}

/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */

