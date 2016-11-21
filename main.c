#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/select.h>


#define BUFF_LEN 		1024
#define SERVER_PORT 	51000		/* �������˿ں� */
#define BACKLOG 		5

typedef struct _client_info
{
	int fd; 					/* �ͻ���socket�׽��� */
	unsigned short sin_port;	/* �ͻ���port */
    struct in_addr sin_addr;	/* �ͻ���IP */
} CLIENT_INFO_t;

typedef struct _client_list
{
	CLIENT_INFO_t info;	
	struct _client_list *pnext;
} CLIENT_LIST_t;

/* 
 * �����ӿͻ��˵��ļ����������� 
 */
static CLIENT_LIST_t *pclient = NULL;

/*
 * 0: success
 * -1: error
 */
static int new_client_register(int fd, unsigned short port, struct in_addr ip)
{
    static CLIENT_LIST_t *plistlast = NULL;	
    CLIENT_LIST_t *plistnew = NULL;
    int ret = -1;
	
	plistnew = (CLIENT_LIST_t *)malloc(sizeof(CLIENT_LIST_t));
	if(plistnew != NULL)
	{
		if(pclient == NULL)
			pclient = plistnew;
		else
		{
			plistlast = pclient;	
			while(plistlast->pnext != NULL) 	/* ��Զָ�����һ�������Ա */
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
	while(plistlast != NULL) 	/* ��Զָ�����һ�������Ա */
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
	CLIENT_LIST_t *index = pclient;
	CLIENT_LIST_t *rmobj = NULL;	/* ��Ҫ�Ƴ��Ľڵ� */
	CLIENT_LIST_t *pre = pclient;

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
	
			free((CLIENT_LIST_t *)rmobj);
			break;
		}
		
		pre = index;
	}
	printf("Current member: ");
	index = pclient;	
	while(index != NULL) 	/* ��Զָ�����һ�������Ա */
	{
		printf("%d ", index->info.fd);
		index = index->pnext;
	}
	printf("\n");
}

static void *handle_request(void *argv)
{	
	CLIENT_LIST_t *index = NULL;
	time_t now;									/* ʱ�� */
	char buff[BUFF_LEN];						/* �շ����ݻ����� */
	int nByte = 0;
	int maxfd = -1;								/* ��������ļ������� */
	fd_set scan_fd;								/* �������������� */
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
						nByte = recv(index->info.fd, buff, BUFF_LEN, 0);
						/* ���շ��ͷ����� */
						if(nByte > 0)
						{
							buff[nByte] = '\0';
							printf("Connect %d:%s:%d ->[%s]\n", index->info.fd, inet_ntoa(index->info.sin_addr), index->info.sin_port, buff);
							if(!strncmp(buff, "TIME", 4))
							{
								now = time(NULL);		/* ��ǰʱ�� */
								/* ��ʱ�临���뻺���� */
								sprintf(buff, "%24s\r\n", ctime(&now));
								/* �������� */
								send(index->info.fd, buff, strlen(buff), 0);
							}
						}
						else if(nByte <= 0)
						{
							printf("Disconnect %d:%s:%d\n", index->info.fd, inet_ntoa(index->info.sin_addr), index->info.sin_port);

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
		s_c = accept(s_s, (struct sockaddr*)&from, &len);
		/* ���տͻ��˵����� */
		printf("Connect from %d:%s:%d\n", s_c, inet_ntoa(from.sin_addr), from.sin_port);
		
		/* ע���µĿͻ��˵����� */
		if(new_client_register(s_c, from.sin_port, from.sin_addr) < 0)
			printf("New client register faild!\n");	
	}	
	return NULL;
}


int main(int argc, char *argv[])
{
	int s_s;								/* �������׽����ļ������� */
	struct sockaddr_in local;				/* ���ص�ַ */	
	int index = 0;
	pthread_t  thread_do[2];				/* �߳�ID */
	
	/* ����TCP�׽��� */
	s_s = socket(AF_INET, SOCK_STREAM, 0);
	
	/* ��ʼ����ַ */
	memset(&local, 0, sizeof(local));			/* ���� */
	local.sin_family = AF_INET;					/* AF_INETЭ���� */
	local.sin_addr.s_addr = htonl(INADDR_ANY);	/* ���Ȿ�ص�ַ */
	local.sin_port = htons(SERVER_PORT);		/* �������˿� */
	printf("Server listen port %d...\n", SERVER_PORT);
	/* ���׽����ļ��������󶨵����ص�ַ�Ͷ˿� */
	bind(s_s, (struct sockaddr*)&local, sizeof(local));
	
	/* listen()�����ȴ�����s_s��socket���ߡ�
	 * ����backlogָ��ͬʱ�ܴ�����������Ҫ��
	 * ���������Ŀ���������client�˽��յ�ECONNREFUSED�Ĵ��� 
	 */
	listen(s_s, BACKLOG);						/* ���� */
	
	/* �����̴߳���ͻ������� */
	pthread_create(&thread_do[0],				/* �߳�ID */
					NULL,						/* ���� */
					handle_connect,				/* �̻߳ص����� */
					(void*)&s_s);				/* �̲߳��� */
	/* �����̴߳���ͻ������� */					
	pthread_create(&thread_do[1],				/* �߳�ID */
					NULL,						/* ���� */
					handle_request,				/* �̻߳ص����� */
					NULL);						/* �̲߳��� */
	/* �ȴ��߳̽��� */
	for(index = 0; index < 2; index++)
		pthread_join(thread_do[index], NULL);
	
	close(s_s);
	
	return 0;
}



