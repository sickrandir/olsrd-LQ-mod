/***********************************************************************
* Code listing from "Advanced Linux Programming," by CodeSourcery LLC  *
* Copyright (C) 2001 by New Riders Publishing                          *
* See COPYRIGHT for license information.                               *
***********************************************************************/

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h>

/* Write TEXT to the socket given by file descriptor SOCKET_FD.  */

struct station_data {
    u_int8_t mac[6];
    u_int8_t lq;	
};

void write_text (int socket_fd, const char* text)
{
  /* Write the number of bytes in the string, including
     NUL-termination.  */
  int length = strlen (text) + 1;
  write (socket_fd, &length, sizeof (length));
  /* Write the string.  */
  write (socket_fd, text, length);
}

void test_buffer (unsigned char *msg, int n)
{
  int tot = n*7;
  //printf("tot: %d\n",tot);
  printf("msg[10]: %x\n",msg[9]);
}


void send_msg (int socket_fd, unsigned char* msg, int n)
{
  /* Write the number of bytes in the string, including
     NUL-termination.  */
  int tot = n*7;
  write (socket_fd, &tot, sizeof (tot));
  /* Write the string.  */
  printf("tot: %d\n",tot);
  printf("msg[10]: %x\n",msg[10]);
  write (socket_fd, msg, tot);
}

int serialize_station_data (unsigned char *buff, void *ptr,int n)
{
    int i;
    for (i=0; i < n; i++)
    {
	struct station_data *sd = ptr + 7*i;        
	buff[0+7*i] = sd->mac[0];
        buff[1+7*i] = sd->mac[1];
        buff[2+7*i] = sd->mac[2];
        buff[3+7*i] = sd->mac[3];
        buff[4+7*i] = sd->mac[4];
        buff[5+7*i] = sd->mac[5];
        buff[6+7*i] = sd->lq;       
    }	
}

struct station_data * deserialize_station_data (unsigned char *buff, int n)
{
    struct station_data *sd = (struct station_data *) malloc(n * sizeof(struct station_data));
    int i;
    for (i=0; i < n; i++)
    {
		sd[i].mac[0] = buff[0+7*i];
		sd[i].mac[1] = buff[1+7*i];
		sd[i].mac[2] = buff[2+7*i];
		sd[i].mac[3] = buff[3+7*i]; 
		sd[i].mac[4] = buff[4+7*i];
		sd[i].mac[5] = buff[5+7*i];
		sd[i].lq = buff[6+7*i];
    }
    return sd; 	
}



int main (int argc, char* const argv[])
{
  const char* const socket_name = argv[1];
  const char* const message = argv[2];
  printf("argv1: %s , argv2: %s", socket_name, message); 
  int socket_fd;
  struct sockaddr_un name;
  /* Create the socket.  */
  socket_fd = socket (PF_LOCAL, SOCK_STREAM, 0);
  /* Store the server's name in the socket address.  */
  name.sun_family = AF_LOCAL;
  strcpy (name.sun_path, socket_name);
  /* Connect the socket.  */
  connect (socket_fd, &name, SUN_LEN (&name));
  /* Write the text on the command line to the socket.  */
  //write_text (socket_fd, message);
  	
  struct station_data sd[2];
  sd[0].mac[0] = 0xA0;
  sd[0].mac[1] = 0xB0;
  sd[0].mac[2] = 0xC0;
  sd[0].mac[3] = 0xD0;
  sd[0].mac[4] = 0xE0;
  sd[0].mac[5] = 0xF0;
  sd[0].lq = 0xA9; 
  

  sd[1].mac[0] = 0xA7;
  sd[1].mac[1] = 0xB7;
  sd[1].mac[2] = 0xC7;
  sd[1].mac[3] = 0xD7;
  sd[1].mac[4] = 0xE7;
  sd[1].mac[5] = 0xF7;
  sd[1].lq = 0xCB; 

  int n = 2;

  unsigned char mes[n*sizeof(struct station_data)]; 
  serialize_station_data(&mes, &sd, n);
  printf("mes[2]= %x\n", mes[2]); 
  printf("mes[10]= %x\n", mes[10]);
  printf("sizeofmes: %d\n", sizeof(mes));
  send_msg (socket_fd, mes, n);
  //test_buffer(mes,n);
  struct station_data *deserialized = deserialize_station_data (mes, n);
  //printf("deserialized[1]: %x\n", deserialized[1].lq);
  //printf("mes[10]: %x\n",mes[10]);
  close (socket_fd);
  return 0;
}
