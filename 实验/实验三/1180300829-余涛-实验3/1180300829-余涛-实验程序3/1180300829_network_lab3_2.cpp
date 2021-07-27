/*
IPV4 分组转发实验
*/
#include "sysInclude.h"
#include <stdio.h>
#include <vector>
using std::vector;

// system support
extern void fwd_LocalRcv(char* pBuffer, int length);

extern void fwd_SendtoLower(char* pBuffer, int length, unsigned int nexthop);

extern void fwd_DiscardPkt(char* pBuffer, int type);

extern unsigned int getIpv4Address();

// implemented by students

vector<stud_route_msg> route;  //设置遍历器结构作为路由表

/*
路由表初始函数
*/
void stud_Route_Init()
{
	//在创建全局变量route时已经进行了初始化，无需再初始化
	return;
}

/*
用路由表添加路由的函数
输入：proute ：指向需要添加路由信息的结构体头部，
    其数据结构
	stud_route_msg 的定义如下：
    typedef struct stud_route_msg
    {
      unsigned int dest;  //目的地址
      unsigned int masklen;  //子网掩码长度
      unsigned int nexthop;  //下一跳
    } stud_route_msg;
*/
void stud_route_add(stud_route_msg* proute)
{

	stud_route_msg temp; //定义一个temp变量，用来将待添加的路由表项转化为本地字节序
	unsigned int dest = ntohl(proute->dest); 
	unsigned int masklen = ntohl(proute->masklen);   
	unsigned int nexthop = ntohl(proute->nexthop); //依次将proute的所有字段转化为字节序储存
	temp.dest = dest;
	temp.masklen = masklen;
	temp.nexthop = nexthop;  //为temp赋值
	route.push_back(temp);  //将temp加入到路由表中
	return;
}

/*
系统处理收到的IP分组的函数
输入：pBuffer：指向接收到的IPv4 分组头部
      length：IPv4 分组的长度
返回：0 为成功，1 为失败；
*/
int stud_fwd_deal(char* pBuffer, int length)
{
	int version = pBuffer[0] >> 4;							   // pBuffer第0个字节内的最开始4个bits为版本号
	int head_length = pBuffer[0] & 0xf;						   // pBuffer第0个字节内的紧接着4个bits为头部长度
	short ttl = (unsigned short)pBuffer[8];					   // pBuffer第8个字节内的8个bits为生存时间ttl
	short checksum = ntohs(*(unsigned short*)(pBuffer + 10));  // pBuffer第10个字节后的16bits为头检验和（short int）
	int destination = ntohl(*(unsigned int*)(pBuffer + 16));   // pBuffer第10个字节后的16bits为头检验和（short int）
	// ttl -= 1;
	if (ttl <= 0)
	{
		//如果出现TTL的错误，调用ip_DiscardPkt并报道错误类型
		fwd_DiscardPkt(pBuffer, STUD_FORWARD_TEST_TTLERROR);
		return 1;
	}



	if (destination == getIpv4Address())
	{
		//如果出现目的地址为本机地址，则调用fwd_LocalRcv提交给上层协议处理
		fwd_LocalRcv(pBuffer, length);
		return 0;
	}
	stud_route_msg* ans_route = NULL;  //定义匹配位置
	int temp_dest = destination;   //temp_dest为目的地址
	for (int i = 0; i < route.size(); i++)  //遍历路由表
	{
		unsigned int temp_sub_net = route[i].dest & ((1 << 31) >> (route[i].masklen - 1)); //对于路由表中的每一个表项
		if (temp_sub_net == temp_dest) //如果目的地址与路由表的某一个表项匹配
		{
			ans_route = &route[i];  //记录匹配的位置
			break;
		}
	}
	if (!ans_route)
	{
		//如果出现没有匹配地址，调用ip_DiscardPkt并报道错误类型，直接返回1
		fwd_DiscardPkt(pBuffer, STUD_FORWARD_TEST_NOROUTE);
		return 1;
	}
	else  //对于匹配成功的情况
	{
		char* buffer = new char[length];  //定义一个buffer储存ip分组pBuffer
		memcpy(buffer, pBuffer, length);
		buffer[8] = ttl - 1;   //将ttl减1
		memset(buffer + 10, 0, 2);
		unsigned long sum = 0;
		unsigned long temp = 0;
		int i;
		//重新计算校验和
		//首先对IPv4的数据包包头以两字节为单位，两两相加，计算相加后的和sum
		for (i = 0; i < head_length * 2; i++)
		{
			temp += (unsigned char)buffer[i * 2] << 8;
			temp += (unsigned char)buffer[i * 2 + 1];
			sum += temp;
			temp = 0;
		}
		unsigned short low_of_sum = sum & 0xffff; //取出低16位
		unsigned short high_of_sum = sum >> 16; //取出高16位
		unsigned short checksum = low_of_sum + high_of_sum; //低16位与高16位相加得到校验和(未取反)
		checksum = ~checksum; //取反得到校验和
		unsigned short header_checksum = htons(checksum);
		memcpy(buffer + 10, &header_checksum, 2); //更新校验和
		fwd_SendtoLower(buffer, length, ans_route->nexthop); //将封装完成的IP分组通过链路层发送出去
	}
	return 0;
}