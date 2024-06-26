/*********************************************************************************
 *      Copyright:  (C) 2024 linuxer<linuxer@email.com>
 *                  All rights reserved.
 *
 *       Filename:  main.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(10/05/24)
 *         Author:  Liao Shengli <2928382441@qq.com>
 *      ChangeLog:  1, Release initial version on "10/05/24 13:41:55"
 *                 
 ********************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include <signal.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/util.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <event2/bufferevent_ssl.h>


#include "database.h"
#include "ds18b20.h"
#include "logger.h"
#include "packet.h"
#include "socket_event.h"



void print_usage(char *progname)
{
	printf("Usage: %s [OPTION]...\n",progname);

	printf("-i(--ipaddr): sepcify server IP address.\n");
	printf("-p(--port): sepcify server port.\n");
	printf("-t(--interval): sepcify sample time.\n");
	printf("-d(--debug): running in debug mode.\n");
	printf("-b(--daemon): set program running on background.\n");
	printf("-h(--help): print this help information.\n");

	printf("\nExample: %s -i 192.168.2.40 -p 8888\n", progname);
	return ;
}



int main (int argc, char **argv)
{
	int                     daemon_run = 0;
	int                     interval = 60;
	int                     rv = -1;

	char                    *logfile = "client.log";
	int                     loglevel = LOG_LEVEL_DEBUG;
	int                     logsize = 10;

	socket_event_t			*sock_ev = (socket_event_t *)malloc(sizeof(socket_event_t));
	char                    *servip = NULL;
	int                     port = 0;
	struct timeval 			tv;
	struct event 			*ev;

	int                     opt = -1;
	const char              *optstring = "i:p:t:dbh";
	struct option           opts[] = {
		{"ipaddr", required_argument, NULL, 'i'},
		{"port", required_argument, NULL, 'p'},
		{"interval", required_argument, NULL, 't'},
		{"debug", no_argument, NULL, 'd'},
		{"daemon", no_argument, NULL, 'b'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	while( (opt = getopt_long(argc, argv, optstring, opts, NULL)) != -1 )
	{
		switch( opt )
		{
			case 'i':
				servip = optarg;
				break;

			case 'p':
				port = atoi(optarg);
				break;

			case 't':
				interval = atoi(optarg);
				break;

			case 'd':
				daemon_run = 0;
				logfile = "console";
				loglevel = LOG_LEVEL_INFO;
				break;

			case 'b':
				daemon_run = 1;
				break;

			case 'h':
				print_usage(argv[0]);
				return 0;

			default:
				break;
		}
	}

	if( !servip || !port || !interval )
	{
		print_usage( argv[0] );
		return 0;
	}

	if( log_open(logfile, loglevel, logsize, THREAD_LOCK_NONE) < 0 )
	{
		fprintf(stderr, "initial log system failure\n");
		return -1;
	}

	if( daemon_run )
	{
		daemon(1, 0);
	}


	rv = socket_ev_init(sock_ev, servip, port);
	if( rv < 0 )
	{
		log_error("init socket failure\n");
		free(sock_ev);
		return -2;
	}
	log_info("init socket successfully\n");
	log_info("[%s:%d]\n", sock_ev->servip, sock_ev->port);


	/*Initialize ssl environment*/
	SSL_library_init();
	OpenSSL_add_all_algorithms();
	ERR_load_crypto_strings();
	SSL_load_error_strings();
	
	sock_ev->ssl_ctx = SSL_CTX_new(TLS_client_method());
	if( !sock_ev->ssl_ctx )
	{
		log_error("SSL_CTX_new() failure\n");
		return -3;
	}

	sock_ev->ssl = SSL_new(sock_ev->ssl_ctx);

	sock_ev->base = event_base_new();
	if( !sock_ev->base )
	{
		log_error("event_base_new() failure\n");
		return -4;
	}

	rv = socket_ev_connect(sock_ev);
	if( rv < 0 )
	{
		log_error("connect to server failure\n");
		//free(sock_ev);
	}
	log_info("connect successfully\n");

	tv.tv_sec = interval;
	tv.tv_usec = 0;
	ev = event_new(sock_ev->base, -1, EV_PERSIST, send_data, sock_ev);
	event_add(ev, &tv);

	sock_ev->sig = evsignal_new(sock_ev->base, SIGINT, signal_cb, sock_ev);
	if( !sock_ev->sig || event_add(sock_ev->sig, NULL) < 0 )
	{
		log_error("set signal failure\n");
		//socket_ev_close(sock_ev);
		free(sock_ev);
		return -5;
	}

	event_base_dispatch(sock_ev->base);

	event_free(ev);
	socket_ev_close(sock_ev);
	log_close();

	return 0;
} 

