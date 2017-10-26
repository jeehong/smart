#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>  
#include <sys/types.h> 

#define PROJECT_NAME	"Smart"
#define VERSION			"0.0.1"
#define BUFF_LEN 		1024
#define SERVER_PORT_DEF 51000		/* �������˿ں� */
#define BACKLOG 		5			/* ȷ��connection���п�����������󳤶� */

struct _client_info_t
{
	int fd; 					/* �ͻ���socket�׽��� */
	unsigned short sin_port;	/* �ͻ���port */
    struct in_addr sin_addr;	/* �ͻ���IP */
};

struct server_cfg_t
{
	struct sockaddr_in local;
	unsigned short s_port;
	int fd;
};

struct _client_list_t
{
	struct _client_info_t info;	
	struct _client_list_t *pnext;
};


/* 
 * �����ӿͻ��˵��ļ����������� 
 */
static struct _client_list_t *pclient = NULL;
static struct server_cfg_t server;	/* �������׽����ļ������� */

/*
 * 0: success
 * -1: error
 */
static int client_register(int fd, unsigned short port, struct in_addr ip)
{
    static struct _client_list_t *plistlast = NULL;	
    struct _client_list_t *plistnew = NULL;
    int ret = -1;
	
	plistnew = (struct _client_list_t *)malloc(sizeof(struct _client_list_t));
	if(plistnew != NULL)
	{
		if(pclient == NULL)
			pclient = plistnew;
		else
		{
			plistlast = pclient;	
			while(plistlast->pnext != NULL) 	/* ��Զָ�����һ��������Ա */
				plistlast = plistlast->pnext;

			plistlast->pnext = plistnew;
		}
		plistnew->info.fd = fd;
		plistnew->info.sin_addr = ip;
		plistnew->info.sin_port = port;
		plistnew->pnext = NULL;
		ret = 0;
	}
	printf("Current member: ");
	plistlast = pclient;	
	while(plistlast != NULL) 	/* ��Զָ�����һ��������Ա */
	{
		printf("%d ", plistlast->info.fd);
		plistlast = plistlast->pnext;
	}
	printf("\n");
	
	return ret;
}

/*
 * sock: ��Ҫ�Ƴ����׽���
 */
static int client_unregister(const int sock)
{
	struct _client_list_t *index = pclient;
	struct _client_list_t *rmobj = NULL;	/* ��Ҫ�Ƴ��Ľڵ� */
	struct _client_list_t *pre = pclient;

	for(; index != NULL; index = index->pnext)
	{
		if(index->info.fd == sock)
		{
			rmobj = index;

			if(rmobj == pclient)
			{
				if(rmobj->pnext != NULL)
					pclient = rmobj->pnext;
				else
					pclient = NULL;
			}
			else
			{
				/* ����Ƴ������������һ����Ա */
				if(rmobj->pnext == NULL)
					pre->pnext = NULL;
				else
					pre->pnext = rmobj->pnext;				
			}
			free((struct _client_list_t *)rmobj);
			break;
		}
		pre = index;
	}
}

/*
 * �����ͻ��˷�������
 */
static void *handle_request(void *argv)
{	
	struct _client_list_t *index = NULL;
	time_t now;								/* ʱ�� */
	char rcv_data[BUFF_LEN];				/* �շ����ݻ����� */
	int nByte = 0;
	int maxfd = -1;							/* ��������ļ������� */
	fd_set scan_fd;							/* �������������� */
	/* ����1s��ʱ���� */ 
	struct timeval timeout = {
		.tv_sec = 1,
		.tv_usec = 0};	     
	int err  = -1;
	
	for(;;)
	{   
		/* ����ļ�������ֵ��ʼ��Ϊ-1 */		
		maxfd = -1;
		FD_ZERO(&scan_fd);									/* �����ļ����������� */
		for(index = pclient; index != NULL; index = index->pnext)			/* ���ļ����������뼯�� */
		{
			FD_SET(index->info.fd, &scan_fd);	/* ���뼯�� */
			if(maxfd < index->info.fd)			/* ��������ļ�������ֵ */
				maxfd = index->info.fd;
		}
		/* select�ȴ� */
		timeout.tv_sec = 1;
		err = select(maxfd + 1, &scan_fd, (fd_set *)NULL, (fd_set *)NULL, (struct timeval *)&timeout);		
		switch(err)
		{
			case 0:			/* ��ʱ */
				break;
			case -1:		/* ������ */
				break;
			default:		/* �пɶ��׽����ļ������� */
				for(index = pclient; index != NULL; index = index->pnext)
				{
					/* ���Ҽ�����ļ������� */
					if(FD_ISSET(index->info.fd, &scan_fd))
					{
						nByte = recv(index->info.fd, rcv_data, BUFF_LEN, 0);
						/* ���շ��ͷ����� */
						if(nByte > 0)
						{
							rcv_data[nByte] = '\0';
							printf("Data %d:%s:%d -> [%s]\n", index->info.fd, inet_ntoa(index->info.sin_addr), index->info.sin_port, rcv_data);
							
							if(!strncmp(rcv_data, "TIME", 4))
							{
								now = time(NULL);		/* ��ǰʱ�� */
								/* ��ʱ�临���뻺���� */
								sprintf(rcv_data, "%24s", ctime(&now));
								/* �������� */
								send(index->info.fd, rcv_data, strlen(rcv_data), 0);
							}
						}
						else if(nByte <= 0)
						{
							printf("Exit %d:%s:%d\n", index->info.fd, inet_ntoa(index->info.sin_addr), index->info.sin_port);

							/* �ͻ����˳����ͷ���ռ�õ��ڴ���Դ */
							client_unregister(index->info.fd);
							/* �رտͻ��� */
							close(index->info.fd);
						}
					}
				}
				break; 	
		}		  
	} 
	
	return NULL;
}

/*
 * �����ͻ�����������
 */
static void *handle_connect(void *argv)
{	
	int s_s = *((int*)argv);			/* ��÷����������׽����ļ������� */
	struct sockaddr_in from;
	socklen_t len = sizeof(from);
	int index = 0;
	int s_c;
	
	/* ���տͻ������� */
	for(;;)
	{
		s_c = accept(server.fd, (struct sockaddr*)&from, &len);
		/* ���տͻ��˵����� */
		printf("Connect from %d:%s:%d\n", s_c, inet_ntoa(from.sin_addr), from.sin_port);
		
		/* ע���µĿͻ��˵����� */
		if(client_register(s_c, from.sin_port, from.sin_addr) < 0)
			printf("New client register faild!\n");	
	}	
	return NULL;
}

void server_exit(int signo)
{  
    printf("Goodbye!\n");  
	close(server.fd);
    _exit(0);
}



void param_init_check()
{
	if(server.s_port == 0)
		server.s_port = SERVER_PORT_DEF;
}

static void usage(void)
{
    printf("  -p <port>      set service port.\n");
	printf("  -h             list all the parame.\n");
}

static int config_prase(int argc, char *argv[])
{
    int  c;
	
    opterr = 0;
    while (-1 != (c = getopt(argc, argv,
         "p:"
         "h"  /* help, licence info */
         "v"  /* version */
        )))
	{
        switch (c) {
            case 'p':
				server.s_port = atoi(optarg);
				break;
            case 'v':
                printf ("%s version %s\n", PROJECT_NAME, VERSION);
				return -1;
			case 'h':
				usage();
				return -1;
            default:
                fprintf(stderr, "%s: illegal argument \"%c\"\n", PROJECT_NAME, optopt);
				printf("Input '-h' to view available functions.\n");
				return -1;
        }
    }
	return 0;
}

static void server_init(void)
{
	struct linger so_linger = {
		.l_onoff = 1,
		.l_linger = 0,
	};

	signal(SIGINT, server_exit);
	/* ����TCP�׽��� */
	server.fd = socket(AF_INET, SOCK_STREAM, 0);
	/* ��ʼ����ַ */
	memset(&server.local, 0, sizeof(server.local));		/* ���� */
	server.local.sin_family = AF_INET;					/* AF_INETЭ���� */
	server.local.sin_addr.s_addr = htonl(INADDR_ANY);	/* ���Ȿ�ص�ַ */
	server.local.sin_port = htons(server.s_port);		/* �������˿� */
	printf("Server listen port %d ...\n", server.s_port);
	setsockopt(server.fd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof so_linger);
	/* ���׽����ļ��������󶨵����ص�ַ�Ͷ˿� */
	bind(server.fd, (struct sockaddr*)&server.local, sizeof(server.local));
	/* 
	 * listen()�����ȴ�����s_s��socket���ߡ�
	 * ����backlogָ��ͬʱ�ܴ������������Ҫ��
	 * ���������Ŀ���������client�˽��յ�ECONNREFUSED�Ĵ��� 
	 */
	listen(server.fd, BACKLOG);					/* ���� */	
}

void app_init(void)
{
	param_init_check();
	server_init();
}

int main(int argc, char *argv[])
{ 
	int index = 0;
	pthread_t  thread_do[2];				/* �߳�ID */
	
	if(config_prase(argc, argv) == -1) {
		return -1;
	}
	app_init();
	
	/* �����̴߳����ͻ������� */
	pthread_create(&thread_do[0],			/* �߳�ID */
					NULL,					/* ���� */
					handle_connect,			/* �̻߳ص����� */
					(void*)&server.fd);		/* �̲߳��� */
	/* �����̴߳����ͻ������� */
	pthread_create(&thread_do[1],			/* �߳�ID */
					NULL,					/* ���� */
					handle_request,			/* �̻߳ص����� */
					NULL);					/* �̲߳��� */
	/* �ȴ��߳̽��� */
	for(index = 0; index < 2; index++)
		pthread_join(thread_do[index], NULL);

	return 0;
}

