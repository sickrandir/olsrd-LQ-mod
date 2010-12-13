/***********************************************************************
* Code listing from "Advanced Linux Programming," by CodeSourcery LLC  *
* Copyright (C) 2001 by New Riders Publishing                          *
* See COPYRIGHT for license information.                               *
***********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>


struct station_data {
    u_int8_t mac[6];
    u_int8_t lq;	
};

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

int server2 (int client_socket)
{
  while (1) {
    int length;
    unsigned char* mes;

    /* First, read the length of the text message from the socket.  If
       read returns zero, the client closed the connection.  */
    if (read (client_socket, &length, sizeof (length)) == 0)
      return 0;
    printf ("lunghezza: %d\n", length);
    ///* Allocate a buffer to hold the text.  */
    mes = (unsigned char*) malloc (length);
    ///* Read the text itself, and print it.  */
    read (client_socket, mes, length);

    int n_stas = length / 7;
    printf ("n_stas: %d\n", n_stas);
    
    struct station_data *sd= deserialize_station_data (mes, n_stas);    
    int i;
    for (i=0; i<n_stas; i++)
    {
		printf("lq: %x\n", sd[i].lq);
    }
    
    /* If the client sent the message "quit", we're all done.  */
    if (!strcmp (mes, "quit"))
    {
        /* Free the buffer.  */
        free (mes);      
	return 1;
    }
    free (mes); 	
  }
}


/* Read text from the socket and print it out.  Continue until the
   socket closes.  Return non-zero if the client sent a "quit"
   message, zero otherwise.  */

int server (int client_socket)
{
  while (1) {
    int length;
    char* mes;

    /* First, read the length of the text message from the socket.  If
       read returns zero, the client closed the connection.  */
    if (read (client_socket, &length, sizeof (length)) == 0)
      return 0;
    printf ("lunghezza: %d\n", length);
    /* Allocate a buffer to hold the text.  */
    mes = (char*) malloc (length);
    /* Read the text itself, and print it.  */
    read (client_socket, mes, length);
    printf ("%s\n", mes);
    /* If the client sent the message "quit", we're all done.  */
    if (!strcmp (mes, "quit"))
    {
        /* Free the buffer.  */
        free (mes);      
	return 1;
    }
    free (mes); 	
  }
}

int main (int argc, char* const argv[])
{
  const char* const socket_name = argv[1];
  int socket_fd;
  struct sockaddr_un name;
  int client_sent_quit_message;

  /* Create the socket.  */
  socket_fd = socket (PF_LOCAL, SOCK_STREAM, 0);
  /* Indicate this is a server.  */
  name.sun_family = AF_LOCAL;
  strcpy (name.sun_path, socket_name);
  bind (socket_fd, &name, SUN_LEN (&name));
  /* Listen for connections.  */
  listen (socket_fd, 5);

  /* Repeatedly accept connections, spinning off one server() to deal
     with each client.  Continue until a client sends a "quit" message.  */
  do {
    struct sockaddr_un client_name;
    socklen_t client_name_len;
    int client_socket_fd;

    /* Accept a connection.  */
    client_socket_fd = accept (socket_fd, &client_name, &client_name_len);
    /* Handle the connection.  */
    client_sent_quit_message = server2 (client_socket_fd);
    /* Close our end of the connection.  */
    close (client_socket_fd);
  }
  while (!client_sent_quit_message);

  /* Remove the socket file.  */
  close (socket_fd);
  unlink (socket_name);

  return 0;
}
