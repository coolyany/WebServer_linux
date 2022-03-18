#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/syscall.h>  // for SYS_xxx definitions
#include <errno.h>
#include <iostream>
#include <pthread.h>

#define BUF_SIZE 1024
#define SMALL_BUF 100

#define SERVER_ADDR "127.0.0.1"

using namespace std;

void *request_handler(void *arg);                    //线程入口函数
void send_data(FILE *fp, char *ct, char *file_name); //向浏览器客服端发送数据
char *content_type(char *file);                      //数据类型
void send_error(FILE *fp);                           //发送错误处理数据
void error_handling(char *message);                  //控制台错误打印

int main(int argc, char *argv[])
{
    int serv_sock, clnt_sock; //创建两个文件描述符，serv_sock为监听套接字，clnt_sock用于数据传输
    struct sockaddr_in serv_adr, clnt_adr;
    memset(&serv_adr, 0, sizeof(serv_adr));//初始化
    memset(&clnt_adr, 0, sizeof(clnt_adr));

    socklen_t clnt_adr_sz;
    char buf[BUF_SIZE];
    pthread_t t_id;
    //主函数参数只有一个时（没有指定端口号），程序退出
    if (argc != 2)
    {
        printf("Usage : %s <port> \n", argv[0]);
        exit(1);
    }

    if((serv_sock = socket(PF_INET, SOCK_STREAM, 0)) == -1 )//创建套接字
    {
        cout<<"creat socket failed : "<< strerror(errno)<<endl;//如果出错则打印错误
		return 0;
    }
    //给服务端的地址结构体赋值
    serv_adr.sin_family = AF_INET;
    serv_adr.sin_addr.s_addr = inet_addr(SERVER_ADDR);//将字符串形式的ip地址转换为点分十进制格式的ip地址
    serv_adr.sin_port = htons(atoi(argv[1]));//将主机上的小端字节序转换为网络传输的大端字节序（如果主机本身就是大端字节序就不用转换了）

    //绑定地址信息到监听套接字上，第二个参数强转是因为形参类型为sockaddr ，而实参类型是sockaddr_in 型的
    if (bind(serv_sock, (struct sockaddr *)&serv_adr, sizeof(serv_adr)) == -1){
        error_handling((char *)("bind() error"));
    }
    //将serv_sock套接字置为监听状态
    if (listen(serv_sock, 1024) == -1){
        error_handling((char *)("listen() error"));
    }
    cout<<"Init Success ! "<<endl;
	cout<<"ip : "<<inet_ntoa(serv_adr.sin_addr)<<"  port : "<<ntohs(serv_adr.sin_port)<<endl;
	cout<<"Waiting for connecting ... "<<endl;

    while (1)
    {
        clnt_adr_sz = sizeof(clnt_adr);
        if((clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_adr, &clnt_adr_sz)) == -1){
            cout << "accept failed : " << strerror(errno) << endl;
		    return 0;
        }
        printf("Connection Request: %s : %d\n", inet_ntoa(clnt_adr.sin_addr), ntohs(clnt_adr.sin_port)); //连接的客服端IP与端口

        //多线程
        pthread_create(&t_id, NULL, request_handler, (void *)&clnt_sock);
        pthread_detach(t_id);
    }
    close(serv_sock);
    return 0;
}

//客服端请求消息处理
void *request_handler(void *arg)
{
    cout << "Enter thread :: " << syscall(SYS_gettid) <<  endl;

    int clnt_sock = *((int *)arg);//类型转换为标志符
    char req_line[SMALL_BUF];
    FILE *clnt_read;
    FILE *clnt_write;

    char method[10];
    char ct[15];
    char file_name[30];

    /*将套接字转换为标准I/O操作*/
    clnt_read = fdopen(clnt_sock, "r");
    clnt_write = fdopen(dup(clnt_sock), "w");
    char *str = fgets(req_line, SMALL_BUF, clnt_read); //保存请求行数据
    cout << "clnt read :: " << str << endl;
    cout << "req_line :: " << req_line << endl;
    char * ptr = strstr(req_line, "HTTP/");
    if (ptr == NULL) //查看是否为HTTP提出的请求
    {
        cout << "error " << endl;
        send_error(clnt_write);
        fclose(clnt_read);
        fclose(clnt_write);
        return NULL;
    }
    cout << "response successful,req line :: " << req_line << "ptr::" << ptr << endl;
    char htmlFileName[20] = "test.html";
    strcpy(method, strtok(req_line, " /")); //请求方式   
    // strcpy(file_name, strtok(htmlFileName, " /"));  //请求的文件名
    strcpy(file_name, htmlFileName);  //请求的文件名
    strcpy(ct, content_type(file_name));    //请求内容类型
    cout << "file name :: " << file_name << endl;
    if (strcmp(method, "GET") != 0)         //是否为GET请求
    {
        send_error(clnt_write);
        fclose(clnt_read);
        fclose(clnt_write);
        return NULL;
    }

    fclose(clnt_read);
    send_data(clnt_write, ct, file_name); //响应给客服端
    return NULL;
}

//服务端响应消息
void send_data(FILE *fp, char *ct, char *file_name)
{
    char protocol[] = "HTTP/1.0 200 OK\r\n";         //状态行(用HTTP1.1版本进行响应，你的请求已经正确处理)
    char server[] = "Server: Linux Web Server \r\n"; //服务端名
    char cnt_len[] = "Content-length: 2048\r\n";     //数据长度不超过2048
    char cnt_type[SMALL_BUF];
    char buf[BUF_SIZE];
    FILE *send_file;

    sprintf(cnt_type, "Content-type: %s\r\n\r\n", ct);
    send_file = fopen(file_name, "r"); //读本地配置文件
    if (send_file == NULL)
    {
        send_error(fp);
        return;
    }

    /*传输头信息*/
    fputs(protocol, fp);
    fputs(server, fp);
    fputs(cnt_len, fp);
    fputs(cnt_type, fp);

    /*传输请求数据*/
    while (fgets(buf, BUF_SIZE, send_file) != NULL)
    {
        fputs(buf, fp);
        fflush(fp);
    }
    fflush(fp);
    fclose(fp); //服务端响应客服端请求后立即断开连接（短链接）
}

//请求数据的类型
char *content_type(char *file)
{
    char extension[SMALL_BUF];
    char file_name[SMALL_BUF];
    strcpy(file_name, file);
    strtok(file_name, ".");
    strcpy(extension, strtok(NULL, "."));

    if (!strcmp(extension, "html") || !strcmp(extension, "htm"))
        return (char *)("text/html"); //html格式的文本数据
    else
        return (char *)("text/plain");
}

//发送客服端错误处理
void send_error(FILE *fp)
{
    char protocol[] = "HTTP/1.0 400 Bad Request\r\n"; //请求文件不存在
    char server[] = "Server: Linux Web Server \r\n";
    char cnt_len[] = "Content-length: 2048\r\n";
    char cnt_type[] = "Content-type: text/html\r\n\r\n";
    char content[] = "发生错误！查看请求文件名和请求方式！";

    fputs(protocol, fp);
    fputs(server, fp);
    fputs(cnt_len, fp);
    fputs(cnt_type, fp);
    fputs(content, fp);
    fflush(fp);
    fclose(fp);
}

//控制台错误打印
void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}