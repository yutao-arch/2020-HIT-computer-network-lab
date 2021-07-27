#include <stdio.h>
#include <iostream>
#include <Windows.h>
#include <winsock.h>
#include <process.h>
#include <string.h>
#include <cstring>
#include <tchar.h>
#include <map>
#include <cstdlib>
#include <set>
#pragma comment(lib,"Ws2_32.lib")

using namespace std;

#define MAXSIZE 65507 //发送数据报文的最大长度
#define HTTP_PORT 80 //http 服务器端口

//钓鱼网站引导表：将用户对前一个网站的访问引导至后一个网站
map<string, string> Fishing_site_guide_table =
{
	{ "hitgs.hit.edu.cn", "today.hit.edu.cn" },
	{ "", "" }
};

//禁止访问的网站表
set<string> No_access_web_table =
{
	"www.enshi.gov.cn",
	//"www.badong.net",
};

//禁止访问网站的用户表
set<string> No_access_user_table =
{
	"127.0.0.0"
};


//cache缓存 存储数据结构
map<string, char*>cache;
struct HttpCache {
	char url[1024];  //储存的url
	char host[1024]; //目标主机
	char last_modified[200]; //记录上次的修改时间戳
	char status[4]; //状态字
	char buffer[MAXSIZE]; //数据
	HttpCache() {
		ZeroMemory(this, sizeof(HttpCache));
	}
};
HttpCache Cache[1024];
int cached_quantities = 0;//初始化已经缓存的url数
int last_cache_location = 0;//初始化上一次缓存的索引

//Http 重要头部数据
struct HttpHeader {
	char method[4]; // POST 或者 GET，注意有些为 CONNECT，本实验暂不考虑
	char url[1024]; // 请求的 url
	char host[1024]; // 目标主机
	char cookie[1024 * 10]; //cookie
	HttpHeader() {
		ZeroMemory(this, sizeof(HttpHeader));
	}
};

BOOL InitSocket();
int ParseHttpHead(char* buffer, HttpHeader* httpHeader, char sendBuffer[]);
BOOL ConnectToServer(SOCKET* serverSocket, char* host);
unsigned int __stdcall ProxyThread(LPVOID lpParameter);
void ParseCacheHead(char* buffer, char* status, char* last_modified);

//代理相关参数
SOCKET ProxyServer;
sockaddr_in ProxyServerAddr;
const int ProxyPort = 10240;

//由于新的连接都使用新线程进行处理，对线程的频繁的创建和销毁特别浪费资源
//可以使用线程池技术提高服务器效率
//const int ProxyThreadMaxNum = 20;
//HANDLE ProxyThreadHandle[ProxyThreadMaxNum] = {0};
//DWORD ProxyThreadDW[ProxyThreadMaxNum] = {0};
struct ProxyParam {
	SOCKET clientSocket;
	SOCKET serverSocket;
};

int _tmain(int argc, _TCHAR* argv[])
{
	printf("代理服务器正在启动\n");
	printf("初始化...\n");
	if (!InitSocket()) {
		printf("socket 初始化失败\n");
		return -1;
	}
	printf("代理服务器正在运行，监听端口 %d\n", ProxyPort);
	SOCKET acceptSocket = INVALID_SOCKET;
	ProxyParam* lpProxyParam;
	HANDLE hThread;
	DWORD dwThreadID;

	SOCKET com_Sock;
	SOCKADDR_IN addr_conn;
	int nSize = sizeof(addr_conn);
	//通过memset函数初始化内存块
	memset((void*)& addr_conn, 0, sizeof(addr_conn));

	//代理服务器不断监听
	while (true) {
		acceptSocket = accept(ProxyServer, NULL, NULL);
		com_Sock = acceptSocket;
		getpeername(com_Sock, (SOCKADDR*)& addr_conn, &nSize); //获取与addr_conn套接字关联的远程协议地址

		//禁止访问网站的用户跳过本次循环，执行下一次监听
		if (No_access_user_table.find(string(inet_ntoa(addr_conn.sin_addr))) != No_access_user_table.end())
		{
			printf("用户 %s没有权限，禁止访问该网站 \n", inet_ntoa(addr_conn.sin_addr));
			continue;
		}

		lpProxyParam = new ProxyParam;
		if (lpProxyParam == NULL) {
			continue;
		}
		lpProxyParam->clientSocket = acceptSocket;
		hThread = (HANDLE)_beginthreadex(NULL, 0, &ProxyThread, (LPVOID)lpProxyParam, 0, 0);
		CloseHandle(hThread);
		Sleep(200);
	}
	closesocket(ProxyServer);
	WSACleanup();
	return 0;
}

//************************************
// Method: InitSocket 
// FullName: InitSocket
// Access: public
// Returns: BOOL
// Qualifier: 初始化套接字
//************************************
BOOL InitSocket() {
	//加载套接字库（必须）
	WORD wVersionRequested;
	WSADATA wsaData;
	//套接字加载时错误提示
	int err;
	//版本 2.2
	wVersionRequested = MAKEWORD(2, 2);
	//加载 dll 文件 Scoket 库
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		//找不到 winsock.dll
		printf("加载 winsock 失败，错误代码为: %d\n", WSAGetLastError());
		return FALSE;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("不能找到正确的 winsock 版本\n");
		WSACleanup();
		return FALSE;
	}
	ProxyServer = socket(AF_INET, SOCK_STREAM, 0);
	if (INVALID_SOCKET == ProxyServer) {
		printf("创建套接字失败，错误代码为：%d\n", WSAGetLastError());
		return FALSE;
	}
	ProxyServerAddr.sin_family = AF_INET;
	ProxyServerAddr.sin_port = htons(ProxyPort);
	ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	if (bind(ProxyServer, (SOCKADDR*)& ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		printf("绑定套接字失败\n");
		return FALSE;
	}
	if (listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR) {
		printf("监听端口%d 失败", ProxyPort);
		return FALSE;
	}
	return TRUE;
}

//************************************
// Method: ProxyThread  
// FullName: ProxyThread
// Access: public
// Returns: unsigned int __stdcall
// Qualifier: 线程执行函数
// Parameter: LPVOID lpParameter
//************************************
unsigned int __stdcall ProxyThread(LPVOID lpParameter) {
	char Buffer[MAXSIZE];
	ZeroMemory(Buffer, MAXSIZE);

	//char sendBuffer[MAXSIZE];
	//ZeroMemory(sendBuffer, MAXSIZE);
	

	char* CacheBuffer;
	SOCKADDR_IN clientAddr;
	int length = sizeof(SOCKADDR_IN);
	int recvSize;
	int ret;
	HttpHeader* httpHeader = new HttpHeader();

	//cache缓存定义变量
	int whether_exist_cache;
	char* cacheBuffer0 = new char[MAXSIZE];
	char* p;
	map<string, char*>::iterator iter;
	string sp;

	//接收客户端的请求
	recvSize = recv(((ProxyParam*)lpParameter)->clientSocket, Buffer, MAXSIZE, 0);
	if (recvSize <= 0) {
		goto error;
	}
	printf("请求内容为：\n");
	printf(Buffer);
	//memcpy(sendBuffer, Buffer, recvSize);

	CacheBuffer = new char[recvSize + 1];
	ZeroMemory(CacheBuffer, recvSize + 1);
	memcpy(CacheBuffer, Buffer, recvSize);
	whether_exist_cache = ParseHttpHead(CacheBuffer, httpHeader, Buffer); //对请求报文的头部文件进行解析，得到请求报文中的method, url, host等，返回url是否存在于缓存中，用于ConnectToServer函数与目标服务器建立连接
	delete CacheBuffer;
	if (!ConnectToServer(&((ProxyParam*)lpParameter)->serverSocket, httpHeader->host)) {  //connect连接至目标服务器
		goto error;
	}
	printf("代理连接主机 %s成功\n", httpHeader->host);

	//对于请求有缓存的情况下
	if (whether_exist_cache)
	{
		char cached_buffer[MAXSIZE];
		ZeroMemory(cached_buffer, MAXSIZE);
		memcpy(cached_buffer, Buffer, recvSize);

		//构造一个用于缓存的请求报文头
		char* pr = cached_buffer + recvSize;
		memcpy(pr, "If-modified-since: ", 19);  //标准的HTTP请求头标签
		pr += 19;
		int lenth = strlen(Cache[last_cache_location].last_modified);
		memcpy(pr, Cache[last_cache_location].last_modified, lenth);
		pr += lenth;

		
		//将客户端发送的 HTTP 数据报文直接转发给目标服务器
		ret = send(((ProxyParam*)lpParameter)->serverSocket, cached_buffer, strlen(cached_buffer) + 1, 0);
		//等待目标服务器返回数据
		recvSize = recv(((ProxyParam*)lpParameter)->serverSocket, cached_buffer, MAXSIZE, 0);
		if (recvSize <= 0) {
			goto error;
		}

		//解析包含缓存信息的HTTP报文头
		CacheBuffer = new char[recvSize + 1];
		ZeroMemory(CacheBuffer, recvSize + 1);
		memcpy(CacheBuffer, cached_buffer, recvSize);
		char last_status[4]; //用于记录服务器主机返回的状态码(包括304和200)
		char last_modified[30];//用于记录记住返回的页面修改的时间
		ParseCacheHead(CacheBuffer, last_status, last_modified);
		delete CacheBuffer;

		//分析cache的状态码
		if (strcmp(last_status, "304") == 0) {//如果页面没有被修改，状态码为304
			printf("\n页面没有修改过\n缓存的url为:%s\n", Cache[last_cache_location].url);
			//将缓存的数据直接转发给客户端
			ret = send(((ProxyParam*)lpParameter)->clientSocket, Cache[last_cache_location].buffer, sizeof(Cache[last_cache_location].buffer), 0);
			if (ret != SOCKET_ERROR)
			{
				printf("页面来自未修改过的缓存\n");
			}
		}
		else if (strcmp(last_status, "200") == 0) {//如果页面已经已经修改了缓存中的内容，状态码为200
			printf("\n页面被修改过\n缓存的url为:%s\n", Cache[last_cache_location].url);
			memcpy(Cache[last_cache_location].buffer, cached_buffer, strlen(cached_buffer));
			memcpy(Cache[last_cache_location].last_modified, last_modified, strlen(last_modified));
			//将目标服务器返回的数据直接转发给客户端
			ret = send(((ProxyParam*)lpParameter)->clientSocket, cached_buffer, sizeof(cached_buffer), 0);
			if (ret != SOCKET_ERROR)
			{
				printf("页面来自修改过的缓存\n");
			}
		}
	}
	//请求没有缓存的情况下
	else
	{
		//将客户端发送的 HTTP 数据报文直接转发给目标服务器
		ret = send(((ProxyParam*)lpParameter)->serverSocket, Buffer, strlen(Buffer) + 1, 0);
		//等待目标服务器返回数据
		recvSize = recv(((ProxyParam*)lpParameter)->serverSocket, Buffer, MAXSIZE, 0);
		if (recvSize <= 0) {
			goto error;
		}
		//将目标服务器返回的数据直接转发给客户端
		ret = send(((ProxyParam*)lpParameter)->clientSocket, Buffer, sizeof(Buffer), 0);
	}
	//错误处理
error:
	printf("关闭套接字\n");
	Sleep(200);
	closesocket(((ProxyParam*)lpParameter)->clientSocket);
	closesocket(((ProxyParam*)lpParameter)->serverSocket);
	delete lpParameter;
	_endthreadex(0);
	return 0;
}

//*************************
//Method: ParseCacheHead
//FullName: ParseCacheHead
//Access: public
//Returns: void
//Qualifier: 在cache命中的时候，解析cache中TCP报文中的HTTP头部
//Parameter: char * buffer
//Parameter: char * status
//Parameter: HttpHeader *httpHeader
//*************************
void ParseCacheHead(char* buffer, char* status, char* last_modified) {
	char* p;
	char* ptr;
	const char* delim = "\r\n";
	p = strtok_s(buffer, delim, &ptr);//提取第一行
	printf(p, "提取第一行 \n");
	memcpy(status, &p[9], 3);
	status[3] = '\0';
	p = strtok_s(NULL, delim, &ptr);
	while (p) {
		if (strstr(p, "Last-Modified") != NULL) {
			memcpy(last_modified, &p[15], strlen(p) - 15);
			break;
		}
		p = strtok_s(NULL, delim, &ptr);
	}
}

//对禁止访问的网站表和钓鱼网站引导表进行处理
void replace(char buffer_c[], const string& oldstr, const string& newstr)
{
	string buffer = string(buffer_c);
	while (buffer.find(oldstr) != string::npos)  //如果buffer找到了oldstr循环
	{
		int m = buffer.find(oldstr);
		buffer = buffer.substr(0, m) + newstr + buffer.substr(m + oldstr.length());
	}
	memcpy(buffer_c, buffer.c_str(), buffer.length() + 1); //用新的网站地址替换原buffer_c
}

//************************************
// Method: ParseHttpHead
// FullName: ParseHttpHead
// Access: public
// Returns: void
// Qualifier: 解析TCP报文中的HTTP头部
// Parameter: char * buffer
// Parameter: HttpHeader * httpHeader
//************************************
int ParseHttpHead(char* buffer, HttpHeader* httpHeader, char sendBuffer[]) {
	char* p;
	char* ptr;
	const char* delim = "\r\n"; //回车换行符	
	int flag = 0; //作为表示Cache是否命中的标志，命中为1，不命中为0
	p = strtok_s(buffer, delim, &ptr);  //提取第一行
	//printf("%s\n", p);
	if (p[0] == 'G') {	//GET方式
		memcpy(httpHeader->method, "GET", 3);
		memcpy(httpHeader->url, &p[4], strlen(p) - 13);
		//printf("url：%s\n", httpHeader->url);//url										
		for (int i = 0; i < 1024; i++) { //依次搜索缓存cache，确定当前访问的url是否已经存在cache中
			if (strcmp(Cache[i].url, httpHeader->url) == 0) { //当前url在已经存在cache中
				flag = 1;
				break;
			}
		}
		if (!flag && cached_quantities != 1023) {//当前url没有存在cache中，cache还存在空闲空间, 往cache中存入url
			memcpy(Cache[cached_quantities].url, &p[4], strlen(p) - 13);
			last_cache_location = cached_quantities;
		}
		else if (!flag && cached_quantities == 1023) {//当前url没有存在cache中，但是cache已满,用该url覆盖第一个cache
			memcpy(Cache[0].url, &p[4], strlen(p) - 13);
			last_cache_location = 0;
		}
	}
	else if (p[0] == 'P') {	//POST方式
		memcpy(httpHeader->method, "POST", 4);
		memcpy(httpHeader->url, &p[5], strlen(p) - 14);
		for (int i = 0; i < 1024; i++) { //依次搜索缓存cache，确定当前访问的url是否已经存在cache中
			if (strcmp(Cache[i].url, httpHeader->url) == 0) {
				flag = 1;
				break;
			}
		}
		if (!flag && cached_quantities != 1023) { //当前url没有存在cache中，cache还存在空闲空间, 往cache中存入url
			memcpy(Cache[cached_quantities].url, &p[5], strlen(p) - 14);
			last_cache_location = cached_quantities;
		}
		else if (!flag && cached_quantities == 1023) { //当前url没有存在cache中，但是cache已满,用该url覆盖第一个cache
			memcpy(Cache[0].url, &p[4], strlen(p) - 13);
			last_cache_location = 0;
		}
	}
	//printf("%s\n", httpHeader->url);
	p = strtok_s(NULL, delim, &ptr);
	while (p) {
		switch (p[0]) {
		case 'H'://HOST
			memcpy(httpHeader->host, &p[6], strlen(p) - 6);
			if (!flag && cached_quantities != 1023) {
				memcpy(Cache[last_cache_location].host, &p[6], strlen(p) - 6);
				cached_quantities++;
			}
			else if (!flag && cached_quantities == 1023) {
				memcpy(Cache[last_cache_location].host, &p[6], strlen(p) - 6);
			}
			break;
		case 'C'://Cookie
			if (strlen(p) > 8) {
				char header[8];
				ZeroMemory(header, sizeof(header));
				memcpy(header, p, 6);
				if (!strcmp(header, "Cookie")) {
					memcpy(httpHeader->cookie, &p[8], strlen(p) - 8);
				}
			}
			break;
		default:
			break;
		}
		p = strtok_s(NULL, delim, &ptr);
	}
	//如果httpHeader的host属于禁止访问的网站表
	if (No_access_web_table.find(string(httpHeader->host)) != No_access_web_table.end())
	{
		printf("该网站 %s 禁止访问 \n", httpHeader->host);
		memset(httpHeader->host, 0, sizeof(httpHeader->host)); //把需要访问的host全改为0
	}
	//如果httpHeader的host属于钓鱼网站引导表
	else if (Fishing_site_guide_table.find(string(httpHeader->host)) != Fishing_site_guide_table.end())
	{
		printf("引导至钓鱼网站 %s 成功\n", httpHeader->host);
		string target = Fishing_site_guide_table[string(httpHeader->host)];
		const char* target_c = target.c_str();
		replace(sendBuffer, string(httpHeader->host), target);  //用后一个host代替前一个host
		memcpy(httpHeader->host, target_c, target.length() + 1);
	}
	return flag;
}

//************************************
// Method: ConnectToServer
// FullName: ConnectToServer
// Access: public
// Returns: BOOL
// Qualifier: 根据主机创建目标服务器套接字，并连接
// Parameter: SOCKET * serverSocket
// Parameter: char * host
//************************************
BOOL ConnectToServer(SOCKET* serverSocket, char* host) {
	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(HTTP_PORT);
	HOSTENT* hostent = gethostbyname(host);
	if (!hostent) {
		return FALSE;
	}
	//printf(host);
	in_addr Inaddr = *((in_addr*)* hostent->h_addr_list);
	serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));
	*serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (*serverSocket == INVALID_SOCKET) {
		return FALSE;
	}
	if (connect(*serverSocket, (SOCKADDR*)& serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		closesocket(*serverSocket);
		return FALSE;
	}
	return TRUE;
}



