
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>

#include <map>
#include "ngx_c_crc32.h"
#include "json.h"

#include <iostream>
#include <functional>
#include <string>
using namespace std;
using json = nlohmann::json;


enum EnMsgType
{
    LOGIN_MSG = 1, // 登录消息
    LOGIN_MSG_ACK = 2, // 登录响应消息
    LOGINOUT_MSG = 3, // 注销消息
    REG_MSG = 4, // 注册消息
    REG_MSG_ACK = 5, // 注册响应消息
    ONE_CHAT_MSG = 6, // 聊天消息
    ADD_FRIEND_MSG = 7, // 添加好友消息

    CREATE_GROUP_MSG = 8, // 创建群组
    ADD_GROUP_MSG = 9, // 加入群组
    GROUP_CHAT_MSG = 10, // 群聊天
};



typedef struct _COMM_PKG_HEADER
{
	unsigned short pkgLen;    //报文总长度【包头+包体】--2字节，2字节可以表示的最大数字为6万多，我们定义_PKG_MAX_LENGTH 30000，所以用pkgLen足够保存下
	                            //包头中记录着整个包【包头—+包体】的长度

	unsigned short msgCode;   //消息类型代码--2字节，用于区别每个不同的命令【不同的消息】
	int            crc32;     //CRC32效验--4字节，为了防止收发数据中出现收到内容和发送内容不一致的情况，引入这个字段做一个基本的校验用	
} COMM_PKG_HEADER, *LPCOMM_PKG_HEADER;


typedef struct _STRUCT_LOGIN
{
	char          username[56];   //用户名 
	char          password[40];   //密码

} STRUCT_LOGIN, *LPSTRUCT_LOGIN;


static int ntySetNonblock(int fd) {
	int flags;

	flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) return flags;
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0) return -1;
	return 0;
}

static int ntySetReUseAddr(int fd) {
	int reuse = 1;
	return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
}


// g++ -o client client.cxx ngx_c_crc32.cxx 

int main(int argc, char **argv) {
	if (argc <= 2) {
		printf("Usage: %s ip port\n", argv[0]);
		exit(0);
	}

	const char *ip = argv[1];
	int port = atoi(argv[2]);


	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port = htons(port);

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return -1;
    }

    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) < 0) {
        perror("connect");
        return -1;
    }
    printf("connect successfully!\n");

    // ntySetNonblock(sockfd);
    ntySetReUseAddr(sockfd);


#if 0   // 客户端登录

    char buffer[1024] = {0};
    int pkglen = 0;

    // 先填充包体
    // STRUCT_LOGIN pkgbdy;
    // memset(&pkgbdy, 0, sizeof(pkgbdy));

    // sprintf(pkgbdy.username, "fuqiang");
    // sprintf(pkgbdy.password, "123456");

    // 直接序列化一个map容器
    json js;

    js["msgid"] = LOGIN_MSG;
    js["id"] = 22;
    js["password"] = "512215";


    string sendBuf = js.dump(); // json数据对象 =》序列化 json字符串

    pkglen += sendBuf.size();


    // 再填充包头
    COMM_PKG_HEADER pkghdr;
    pkghdr.msgCode = htons(7);

    pkglen += sizeof(COMM_PKG_HEADER);
    pkghdr.pkgLen = htons(pkglen);

    CCRC32 *p_crc32 = CCRC32::GetInstance();
    pkghdr.crc32 = p_crc32->Get_CRC((unsigned char *)sendBuf.c_str(), sendBuf.size());
    pkghdr.crc32 = htonl(pkghdr.crc32);



    // 发送包
    memcpy(buffer, &pkghdr, sizeof(COMM_PKG_HEADER));
    memcpy(buffer + sizeof(COMM_PKG_HEADER), sendBuf.c_str(), sendBuf.size());

    int ret = send(sockfd, buffer, pkglen, 0);
    printf("send %d bytes!\n", ret);


    // 接收包
    memset(buffer, 0, 1024);
    ret = recv(sockfd, buffer, 1024, 0);
    printf("recv %d bytes!\n", ret);

    pkghdr = *((LPCOMM_PKG_HEADER)buffer);
    char *pkgbdy = buffer + sizeof(COMM_PKG_HEADER);
    printf("msgid: %d, msglen: %d\n", ntohs(pkghdr.msgCode), ntohs(pkghdr.pkgLen));
    printf("pkgbdy: %s", pkgbdy);


#endif

#if 0   // 客户端注册

    char buffer[1024] = {0};
    int pkglen = 0;

    json js;

    js["msgid"] = REG_MSG;
    js["name"] = "shencan";
    js["password"] = "123456";

    string sendBuf = js.dump(); // json数据对象 =》序列化 json字符串

    pkglen += sendBuf.size();


    // 再填充包头
    COMM_PKG_HEADER pkghdr;
    pkghdr.msgCode = htons(7);

    pkglen += sizeof(COMM_PKG_HEADER);
    pkghdr.pkgLen = htons(pkglen);

    CCRC32 *p_crc32 = CCRC32::GetInstance();
    pkghdr.crc32 = p_crc32->Get_CRC((unsigned char *)sendBuf.c_str(), sendBuf.size());
    pkghdr.crc32 = htonl(pkghdr.crc32);



    // 发送包
    memcpy(buffer, &pkghdr, sizeof(COMM_PKG_HEADER));
    memcpy(buffer + sizeof(COMM_PKG_HEADER), sendBuf.c_str(), sendBuf.size());

    printf("start to sent %s\n", buffer + sizeof(COMM_PKG_HEADER));


    getchar();
    int ret = send(sockfd, buffer, pkglen, 0);
    printf("send %d bytes!\n", ret);


    // 接收包
    memset(buffer, 0, 1024);
    ret = recv(sockfd, buffer, 1024, 0);
    printf("recv %d bytes!\n", ret);

    pkghdr = *((LPCOMM_PKG_HEADER)buffer);
    char *pkgbdy = buffer + sizeof(COMM_PKG_HEADER);
    printf("msgid: %d, msglen: %d\n", ntohs(pkghdr.msgCode), ntohs(pkghdr.pkgLen));
    printf("pkgbdy: %s", pkgbdy);


#endif


#if 1   // 一对一聊天

    char buffer[1024] = {0};
    int pkglen = 0;

    json js;

    js["msgid"] = ONE_CHAT_MSG;
    js["id"] = 23;
    js["toid"] = 22;
    js["message"] = "nice to meet you.";


    string sendBuf = js.dump(); // json数据对象 =》序列化 json字符串

    pkglen += sendBuf.size();


    // 再填充包头
    COMM_PKG_HEADER pkghdr;
    pkghdr.msgCode = htons(7);

    pkglen += sizeof(COMM_PKG_HEADER);
    pkghdr.pkgLen = htons(pkglen);

    CCRC32 *p_crc32 = CCRC32::GetInstance();
    pkghdr.crc32 = p_crc32->Get_CRC((unsigned char *)sendBuf.c_str(), sendBuf.size());
    pkghdr.crc32 = htonl(pkghdr.crc32);



    // 发送包
    memcpy(buffer, &pkghdr, sizeof(COMM_PKG_HEADER));
    memcpy(buffer + sizeof(COMM_PKG_HEADER), sendBuf.c_str(), sendBuf.size());

    printf("start to sent %s\n", buffer + sizeof(COMM_PKG_HEADER));


    getchar();
    int ret = send(sockfd, buffer, pkglen, 0);
    printf("send %d bytes!\n", ret);


    // 接收包
    memset(buffer, 0, 1024);
    ret = recv(sockfd, buffer, 1024, 0);
    printf("recv %d bytes!\n", ret);

    pkghdr = *((LPCOMM_PKG_HEADER)buffer);
    char *pkgbdy = buffer + sizeof(COMM_PKG_HEADER);
    printf("msgid: %d, msglen: %d\n", ntohs(pkghdr.msgCode), ntohs(pkghdr.pkgLen));
    printf("pkgbdy: %s\n", pkgbdy);


#endif


#if 0   // 添加朋友

    char buffer[1024] = {0};
    int pkglen = 0;

    json js;

    js["msgid"] = ADD_FRIEND_MSG;
    js["id"] = 22;
    js["friendid"] = 23;


    string sendBuf = js.dump(); // json数据对象 =》序列化 json字符串

    pkglen += sendBuf.size();


    // 再填充包头
    COMM_PKG_HEADER pkghdr;
    pkghdr.msgCode = htons(7);

    pkglen += sizeof(COMM_PKG_HEADER);
    pkghdr.pkgLen = htons(pkglen);

    CCRC32 *p_crc32 = CCRC32::GetInstance();
    pkghdr.crc32 = p_crc32->Get_CRC((unsigned char *)sendBuf.c_str(), sendBuf.size());
    pkghdr.crc32 = htonl(pkghdr.crc32);



    // 发送包
    memcpy(buffer, &pkghdr, sizeof(COMM_PKG_HEADER));
    memcpy(buffer + sizeof(COMM_PKG_HEADER), sendBuf.c_str(), sendBuf.size());

    printf("start to sent %s\n", buffer + sizeof(COMM_PKG_HEADER));


    getchar();
    int ret = send(sockfd, buffer, pkglen, 0);
    printf("send %d bytes!\n", ret);


    // 接收包
    memset(buffer, 0, 1024);
    ret = recv(sockfd, buffer, 1024, 0);
    printf("recv %d bytes!\n", ret);

    pkghdr = *((LPCOMM_PKG_HEADER)buffer);
    char *pkgbdy = buffer + sizeof(COMM_PKG_HEADER);
    printf("msgid: %d, msglen: %d\n", ntohs(pkghdr.msgCode), ntohs(pkghdr.pkgLen));
    printf("pkgbdy: %s", pkgbdy);


#endif



    getchar();
	return 0;
}
















