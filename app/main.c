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

#define BUFF_LEN 		1024
#define SERVER_PORT 	51000		/* 服务器端口号 */
#define BACKLOG 		5			/* 确定connection队列可以增长的最大长度 */

typedef struct _client_info
{
	int fd; 					/* 客户端socket套接字 */
	unsigned short sin_port;	/* 客户端port */
    struct in_addr sin_addr;	/* 客户端IP */
} CLIENT_INFO_t;

typedef struct _client_list
{
	CLIENT_INFO_t info;	
	struct _client_list *pnext;
} CLIENT_LIST_t;


/* 
 * 可连接客户端的文件描述符数组 
 */
static CLIENT_LIST_t *pclient = NULL;
static int s_server;	/* 服务器套接字文件描述符 */

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
			while(plistlast->pnext != NULL) 	/* 永远指向最后一个链表成员 */
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
	while(plistlast != NULL) 	/* 永远指向最后一个链表成员 */
	{
		printf("%d ", plistlast->info.fd);
		plistlast = plistlast->pnext;
	}
	printf("\n");
	
	return ret;
}

/*
 * sock: 需要移除的套接字
 */
static int client_unregister(const int sock)
{
	CLIENT_LIST_t *index = pclient;
	CLIENT_LIST_t *rmobj = NULL;	/* 需要移除的节点 */
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
				/* 如果移除的是链表最后一个成员 */
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
}

/*
 * 处理客户端服务请求
 */
static void *handle_request(void *argv)
{	
	CLIENT_LIST_t *index = NULL;
	time_t now;									/* 时间 */
	char rcv_data[BUFF_LEN];						/* 收发数据缓冲区 */
	int nByte = 0;
	int maxfd = -1;								/* 最大侦听文件描述符 */
	fd_set scan_fd;								/* 侦听描述符集合 */
	/* 阻塞1s后超时返回 */ 
	struct timeval timeout = {
		.tv_sec = 1, 
		.tv_usec = 0}; 					     
	int err  = -1;
	
	for(;;)
	{   
		/* 最大文件描述符值初始化为-1 */		
		maxfd = -1;
		FD_ZERO(&scan_fd);									/* 清零文件描述符集合 */
		for(index = pclient; index != NULL; index = index->pnext)			/* 将文件描述符放入集合 */
		{
			FD_SET(index->info.fd, &scan_fd);	/* 放入集合 */
			if(maxfd < index->info.fd)			/* 更新最大文件描述符值 */
				maxfd = index->info.fd;
		}
		/* select等待 */
		timeout.tv_sec = 1;
		err = select(maxfd + 1, &scan_fd, (fd_set *)NULL, (fd_set *)NULL, (struct timeval *)&timeout);		
		switch(err)
		{
			case 0:			/* 超时 */
				break;
			case -1:		/* 错误发生 */
				break;
			default:		/* 有可读套接字文件描述符 */
				for(index = pclient; index != NULL; index = index->pnext)
				{
					/* 查找激活的文件描述符 */
					if(FD_ISSET(index->info.fd, &scan_fd))
					{
						nByte = recv(index->info.fd, rcv_data, BUFF_LEN, 0);
						/* 接收发送方数据 */
						if(nByte > 0)
						{
							rcv_data[nByte] = '\0';
							printf("Data %d:%s:%d -> [%s]\n", index->info.fd, inet_ntoa(index->info.sin_addr), index->info.sin_port, rcv_data);
							
							if(!strncmp(rcv_data, "TIME", 4))
							{
								now = time(NULL);		/* 当前时间 */
								/* 将时间复制入缓冲区 */
								sprintf(rcv_data, "%24s", ctime(&now));
								/* 发送数据 */
								send(index->info.fd, rcv_data, strlen(rcv_data), 0);
							}
						}
						else if(nByte <= 0)
						{
							printf("Exit %d:%s:%d\n", index->info.fd, inet_ntoa(index->info.sin_addr), index->info.sin_port);

							/* 客户端退出，释放其占用的内存资源 */
							client_unregister(index->info.fd);
							/* 关闭客户端 */
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
 * 处理客户端连接请求
 */
static void *handle_connect(void *argv)
{	
	int s_s = *((int*)argv);			/* 获得服务器侦听套接字文件描述符 */
	struct sockaddr_in from;
	socklen_t len = sizeof(from);
	int index = 0;
	int s_c;
	
	/* 接收客户端连接 */
	for(;;)
	{
		s_c = accept(s_server, (struct sockaddr*)&from, &len);
		/* 接收客户端的请求 */
		printf("Connect from %d:%s:%d\n", s_c, inet_ntoa(from.sin_addr), from.sin_port);
		
		/* 注册新的客户端到链表 */
		if(new_client_register(s_c, from.sin_port, from.sin_addr) < 0)
			printf("New client register faild!\n");	
	}	
	return NULL;
}

void server_exit(int signo)
{  
    printf("Goodbye!\n");  
	close(s_server);
    _exit(0);  
}

int main(int argc, char *argv[])
{ 
	struct sockaddr_in local;				/* 本地地址 */	
	int index = 0;
	pthread_t  thread_do[2];				/* 线程ID */
	struct linger so_linger = {
		.l_onoff = 1,
		.l_linger = 0,
	};
	signal(SIGINT, server_exit);
	/* 建立TCP套接字 */
	s_server = socket(AF_INET, SOCK_STREAM, 0);
	
	/* 初始化地址 */
	memset(&local, 0, sizeof(local));			/* 清零 */
	local.sin_family = AF_INET;					/* AF_INET协议族 */
	local.sin_addr.s_addr = htonl(INADDR_ANY);	/* 任意本地地址 */
	local.sin_port = htons(SERVER_PORT);		/* 服务器端口 */
	printf("Server listen port %d ...\n", SERVER_PORT);
	
	setsockopt(s_server, SOL_SOCKET, SO_LINGER, &so_linger, sizeof so_linger);
	/* 将套接字文件描述符绑定到本地地址和端口 */
	bind(s_server, (struct sockaddr*)&local, sizeof(local));
	
	/* 
	 * listen()用来等待参数s_s的socket连线。
	 * 参数backlog指定同时能处理的最大连接要求，
	 * 如果连接数目达此上限则client端将收到ECONNREFUSED的错误 
	 */
	listen(s_server, BACKLOG);					/* 侦听 */
	
	/* 创建线程处理客户端连接 */
	pthread_create(&thread_do[0],				/* 线程ID */
					NULL,						/* 属性 */
					handle_connect,				/* 线程回调函数 */
					(void*)&s_server);			/* 线程参数 */
	/* 创建线程处理客户端请求 */					
	pthread_create(&thread_do[1],				/* 线程ID */
					NULL,						/* 属性 */
					handle_request,				/* 线程回调函数 */
					NULL);						/* 线程参数 */
	/* 等待线程结束 */
	for(index = 0; index < 2; index++)
		pthread_join(thread_do[index], NULL);

	return 0;
}