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
#define SERVER_PORT 	8888		/* �������˿ں� */
#define BACKLOG 		5
#define CLIENT_NUM 		20			/* ���֧�ֿͻ������� */

typedef struct _client_info
{
	int fd; 
	unsigned short sin_port;
    struct in_addr sin_addr;
} CLIENT_INFO_t;

typedef struct _client_list
{
	struct _client_info list[CLIENT_NUM];	
	unsigned short num;
} CLIENT_LIST_t;

/* 
 * �����ӿͻ��˵��ļ����������� 
 */
static struct _client_list client;

static void *handle_request(void *argv)
{	
	time_t now;									/* ʱ�� */
	char buff[BUFF_LEN];						/* �շ����ݻ����� */
	int nByte = 0;
	int maxfd = -1;								/* ��������ļ������� */
	fd_set scan_fd;								/* �������������� */
	/* ����1s��ʱ���� */ 
	struct timeval timeout = {
		.tv_sec = 10, 
		.tv_usec = 0}; 					     
	int index = 0;
	int err  = -1;
	
	for(;;)
	{   
		/* ����ļ�������ֵ��ʼ��Ϊ-1 */		
		maxfd = -1;
		FD_ZERO(&scan_fd);									/* �����ļ����������� */
		for(index = 0; index < CLIENT_NUM; index++)			/* ���ļ����������뼯�� */
		{
			if(client.list[index].fd != -1)					/* �Ϸ����ļ������� */
			{
				FD_SET(client.list[index].fd, &scan_fd);	/* ���뼯�� */
				if(maxfd < client.list[index].fd)			/* ��������ļ�������ֵ */
				{
					maxfd = client.list[index].fd;
				}
			}
		}
		/* select�ȴ� */
		err = select(maxfd + 1, &scan_fd, (fd_set *)NULL, (fd_set *)NULL, &timeout);		
		switch(err)
		{
			case 0:			/* ��ʱ */
				break;
			case -1:		/* ������ */
				break;
			default:		/* �пɶ��׽����ļ������� */
				if(client.num <= 0)
					break;
				for(index = 0; index < CLIENT_NUM; index++)
				{
					/* ���Ҽ�����ļ������� */
					if(client.list[index].fd != -1 
						&& FD_ISSET(client.list[index].fd, &scan_fd))
					{
						nByte = recv(client.list[index].fd, buff, BUFF_LEN, 0);
						/* ���շ��ͷ����� */
						if(nByte > 0 && !strncmp(buff, "TIME", 4))
						{
							now = time(NULL);		/* ��ǰʱ�� */
							/* ��ʱ�临���뻺���� */
							sprintf(buff, "%24s\r\n", ctime(&now));
							/* �������� */
							send(client.list[index].fd, buff, strlen(buff), 0);
						}
						else if(nByte <= 0)
						{
							printf("Disconnect from %s:%d\n",inet_ntoa(client.list[index].sin_addr), client.list[index].sin_port);
							/* �رտͻ��� */
							close(client.list[index].fd);
							client.num--;	/* �ͻ��˼�������1 */	
							/* �����ļ��������������е�ֵ */
							client.list[index].fd = -1;
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
		printf("Connect from %s:%d\n",inet_ntoa(from.sin_addr), from.sin_port);
		/* ���Һ���λ�ã����ͻ��˵��ļ����������� */				
		for(index = 0; index < CLIENT_NUM; index++)
		{
			if(client.list[index].fd == -1)			/* �ҵ� */
			{
				/* ���� */
				client.list[index].fd = s_c;
				memcpy(&client.list[index].sin_addr, &from.sin_addr, sizeof(struct in_addr));
				client.list[index].sin_port = from.sin_port;
				/* �ͻ��˼�������1 */
				client.num ++;
				/* ������ѯ�ȴ��ͻ������� */
				break;						
			}	
		}		
	}	
	return NULL;
}


int main(int argc, char *argv[])
{
	int s_s;								/* �������׽����ļ������� */
	struct sockaddr_in local;				/* ���ص�ַ */	
	int index = 0;
	pthread_t  thread_do[2];				/* �߳�ID */
	
	memset(&client.list[0], -1, sizeof(client.list[0]) * CLIENT_NUM);
	
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

