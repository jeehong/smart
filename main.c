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
#define SERVER_PORT 	8888		/* 服务器端口号 */
#define BACKLOG 		5
#define CLIENT_NUM 		20			/* 最大支持客户端数量 */

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
 * 可连接客户端的文件描述符数组 
 */
static struct _client_list client;

static void *handle_request(void *argv)
{	
	time_t now;									/* 时间 */
	char buff[BUFF_LEN];						/* 收发数据缓冲区 */
	int nByte = 0;
	int maxfd = -1;								/* 最大侦听文件描述符 */
	fd_set scan_fd;								/* 侦听描述符集合 */
	/* 阻塞1s后超时返回 */ 
	struct timeval timeout = {
		.tv_sec = 10, 
		.tv_usec = 0}; 					     
	int index = 0;
	int err  = -1;
	
	for(;;)
	{   
		/* 最大文件描述符值初始化为-1 */		
		maxfd = -1;
		FD_ZERO(&scan_fd);									/* 清零文件描述符集合 */
		for(index = 0; index < CLIENT_NUM; index++)			/* 将文件描述符放入集合 */
		{
			if(client.list[index].fd != -1)					/* 合法的文件描述符 */
			{
				FD_SET(client.list[index].fd, &scan_fd);	/* 放入集合 */
				if(maxfd < client.list[index].fd)			/* 更新最大文件描述符值 */
				{
					maxfd = client.list[index].fd;
				}
			}
		}
		/* select等待 */
		err = select(maxfd + 1, &scan_fd, (fd_set *)NULL, (fd_set *)NULL, &timeout);		
		switch(err)
		{
			case 0:			/* 超时 */
				break;
			case -1:		/* 错误发生 */
				break;
			default:		/* 有可读套接字文件描述符 */
				if(client.num <= 0)
					break;
				for(index = 0; index < CLIENT_NUM; index++)
				{
					/* 查找激活的文件描述符 */
					if(client.list[index].fd != -1 
						&& FD_ISSET(client.list[index].fd, &scan_fd))
					{
						nByte = recv(client.list[index].fd, buff, BUFF_LEN, 0);
						/* 接收发送方数据 */
						if(nByte > 0 && !strncmp(buff, "TIME", 4))
						{
							now = time(NULL);		/* 当前时间 */
							/* 将时间复制入缓冲区 */
							sprintf(buff, "%24s\r\n", ctime(&now));
							/* 发送数据 */
							send(client.list[index].fd, buff, strlen(buff), 0);
						}
						else if(nByte <= 0)
						{
							printf("Disconnect from %s:%d\n",inet_ntoa(client.list[index].sin_addr), client.list[index].sin_port);
							/* 关闭客户端 */
							close(client.list[index].fd);
							client.num--;	/* 客户端计数器减1 */	
							/* 更新文件描述符在数组中的值 */
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
	int s_s = *((int*)argv);			/* 获得服务器侦听套接字文件描述符 */
	struct sockaddr_in from;
	socklen_t len = sizeof(from);
	int index = 0;
	int s_c;
	
	/* 接收客户端连接 */
	for(;;)
	{
		s_c = accept(s_s, (struct sockaddr*)&from, &len);
		/* 接收客户端的请求 */
		printf("Connect from %s:%d\n",inet_ntoa(from.sin_addr), from.sin_port);
		/* 查找合适位置，将客户端的文件描述符放入 */				
		for(index = 0; index < CLIENT_NUM; index++)
		{
			if(client.list[index].fd == -1)			/* 找到 */
			{
				/* 放入 */
				client.list[index].fd = s_c;
				memcpy(&client.list[index].sin_addr, &from.sin_addr, sizeof(struct in_addr));
				client.list[index].sin_port = from.sin_port;
				/* 客户端计数器加1 */
				client.num ++;
				/* 继续轮询等待客户端连接 */
				break;						
			}	
		}		
	}	
	return NULL;
}


int main(int argc, char *argv[])
{
	int s_s;								/* 服务器套接字文件描述符 */
	struct sockaddr_in local;				/* 本地地址 */	
	int index = 0;
	pthread_t  thread_do[2];				/* 线程ID */
	
	memset(&client.list[0], -1, sizeof(client.list[0]) * CLIENT_NUM);
	
	/* 建立TCP套接字 */
	s_s = socket(AF_INET, SOCK_STREAM, 0);
	
	/* 初始化地址 */
	memset(&local, 0, sizeof(local));			/* 清零 */
	local.sin_family = AF_INET;					/* AF_INET协议族 */
	local.sin_addr.s_addr = htonl(INADDR_ANY);	/* 任意本地地址 */
	local.sin_port = htons(SERVER_PORT);		/* 服务器端口 */
	printf("Server listen port %d...\n", SERVER_PORT);
	/* 将套接字文件描述符绑定到本地地址和端口 */
	bind(s_s, (struct sockaddr*)&local, sizeof(local));
	
	/* listen()用来等待参数s_s的socket连线。
	 * 参数backlog指定同时能处理的最大连接要求，
	 * 如果连接数目达此上限则client端将收到ECONNREFUSED的错误 
	 */
	listen(s_s, BACKLOG);						/* 侦听 */
	
	/* 创建线程处理客户端连接 */
	pthread_create(&thread_do[0],				/* 线程ID */
					NULL,						/* 属性 */
					handle_connect,				/* 线程回调函数 */
					(void*)&s_s);				/* 线程参数 */
	/* 创建线程处理客户端请求 */					
	pthread_create(&thread_do[1],				/* 线程ID */
					NULL,						/* 属性 */
					handle_request,				/* 线程回调函数 */
					NULL);						/* 线程参数 */
	/* 等待线程结束 */
	for(index = 0; index < 2; index++)
		pthread_join(thread_do[index], NULL);
	
	close(s_s);
	
	return 0;
}

