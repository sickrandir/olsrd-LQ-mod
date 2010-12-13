




/**
 * Initialize plugin
 * Called after all parameters are passed
 */
int
olsrd_plugin_init(void)
{
	if ((pid = fork()) == -1)
		perror("fork error");
	else if (pid == 0) {
		execlp("./faifa_proxy", "faifa_proxy", NULL);
		printf("Return not expected. Must be an execlp error.n");
	}



}
