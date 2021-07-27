/*
IPV4 分组收发实验
*/
#include "sysInclude.h"
#include <stdio.h>
#include <malloc.h>
extern void ip_DiscardPkt(char* pBuffer, int type);

extern void ip_SendtoLower(char* pBuffer, int length);

extern void ip_SendtoUp(char* pBuffer, int length);

extern unsigned int getIpv4Address();

/*
接收接口函数
输入：pBuffer为指向接收缓冲区的指针，指向IPv4 分组头部
	  length为IPv4为分组长度
返回：0：成功接收IP 分组并交给上层处理
	  1：IP 分组接收失败
*/
int stud_ip_recv(char* pBuffer, unsigned short length)
{
	int version = pBuffer[0] >> 4;                             // pBuffer第0个字节内的最开始4个bits为版本号
	int head_length = pBuffer[0] & 0xf;                        // pBuffer第0个字节内的紧接着4个bits为头部长度
	short ttl = (unsigned short)pBuffer[8];                    // pBuffer第8个字节内的8个bits为生存时间ttl
	short checksum = ntohs(*(unsigned short*)(pBuffer + 10));  // pBuffer第10个字节后的16bits为头检验和（short int）
	int destination = ntohl(*(unsigned int*)(pBuffer + 16));   // pBuffer第16个字节后的32bits为目的ip地址（long int）

	if (ttl <= 0)
	{
		//如果出现TTL<=0的错误，调用ip_DiscardPkt并报道错误类型
		ip_DiscardPkt(pBuffer, STUD_IP_TEST_TTL_ERROR);
		return 1;
	}
	if (version != 4)
	{
		//如果出现版本号不为4的错误，调用ip_DiscardPkt并报道错误类型
		ip_DiscardPkt(pBuffer, STUD_IP_TEST_VERSION_ERROR);
		return 1;
	}
	if (head_length < 5)
	{
		//如果出现头部长度小于20个字节的错误(由于头部长度字段以4字节为单位，故只需与5比较即可)，调用ip_DiscardPkt并报道错误类型
		ip_DiscardPkt(pBuffer, STUD_IP_TEST_HEADLEN_ERROR);
		return 1;
	}
	if (destination != getIpv4Address() && destination != 0xffff)
	{
		//如果出现错误目的地址的错误，调用ip_DiscardPkt并报道错误类型
		ip_DiscardPkt(pBuffer, STUD_IP_TEST_DESTINATION_ERROR);
		return 1;
	}
	//对于校验和的错误，需要首先计算校验和
	unsigned long sum = 0;
	unsigned long temp = 0;
	int i;
	//首先对IPv4的数据包包头以两字节为单位，两两相加，计算相加后的和sum
	for (i = 0; i < head_length * 2; i++)
	{
		temp += (unsigned char)pBuffer[i * 2] << 8;
		temp += (unsigned char)pBuffer[i * 2 + 1];
		sum += temp;
		temp = 0;
	}
	unsigned short low_of_sum = sum & 0xffff; //取出低16位
	unsigned short high_of_sum = sum >> 16; //取出高16位
	if (low_of_sum + high_of_sum != 0xffff) //低16位与高16位相加
	{
		//如果出现首部校验和的错误(计算结果不为0xffff)，调用ip_DiscardPkt并报道错误类型
		ip_DiscardPkt(pBuffer, STUD_IP_TEST_CHECKSUM_ERROR);
		return 1;
	}
	ip_SendtoUp(pBuffer, length); //提交给上层协议
	return 0;
}

/*
发送接口函数
输入：pBuffer为指向接收缓冲区的指针，指向IPv4 分组头部
	  len为IPv4上层协议数据长度
	  srcAddr为源IPv4地址
	  dstAddr为目的IPv4地址
	  protocol为IPv4上层协议号
	  ttl为生存时间
返回：0：成功发送IP分组
	  1：发送IP分组失败
*/
int stud_ip_Upsend(char* pBuffer, unsigned short len, unsigned int srcAddr,
	unsigned int dstAddr, byte protocol, byte ttl)
{
	//一般默认头部长度为字节
	short ip_length = len + 20; //得到这层的数据长度
	char* buffer = (char*)malloc(ip_length * sizeof(char));
	memset(buffer, 0, ip_length);
	buffer[0] = 0x45; //规定版本号和首部长度为4和5×4=20
	buffer[8] = ttl;  //规定生存时间ttl
	buffer[9] = protocol;  //规定协议号
	// 将数据长度转换为网络字节序
	unsigned short network_length = htons(ip_length);
	// buffer[2] = network_length >> 8;
	// buffer[3] = network_length & 0xff;
	memcpy(buffer + 2, &network_length, 2);
	unsigned int src = htonl(srcAddr);  //解析源IPv4地址
	unsigned int dst = htonl(dstAddr);  //解析目的IPv4地址

	memcpy(buffer + 12, &src, 4);
	memcpy(buffer + 16, &dst, 4);
	//计算校验和
	unsigned long sum = 0;
	unsigned long temp = 0;
	int i;
	//首先对IPv4的数据包包头以两字节为单位，两两相加，计算相加后的和sum
	for (i = 0; i < 20; i += 2)
	{
		temp += (unsigned char)buffer[i] << 8;
		temp += (unsigned char)buffer[i + 1];
		sum += temp;
		temp = 0;
	}
	unsigned short low_of_sum = sum & 0xffff; //取出低16位
	unsigned short high_of_sum = sum >> 16; //取出高16位
	unsigned short checksum = low_of_sum + high_of_sum; //低16位与高16位相加得到校验和(未取反)
	checksum = ~checksum; //取反得到校验和
	unsigned short header_checksum = htons(checksum);  //将校验和更新
	// buffer[10] = header_checksum >> 8;
	// buffer[11] = header_checksum & 0xff;
	memcpy(buffer + 10, &header_checksum, 2);
	memcpy(buffer + 20, pBuffer, len);
	// ip_SendtoLower(buffer, ip_length);
	ip_SendtoLower(buffer, len + 20); //发送分组
	return 0;
}

