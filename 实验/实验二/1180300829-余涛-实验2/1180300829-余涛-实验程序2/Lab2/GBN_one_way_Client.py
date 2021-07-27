import socket
from random import random
import select
# 单向数据传输GBN协议


class ACK:  # 定义ACK
    def __init__(self, seq):
        self.seq = seq  # 分组标记的序号

    def __str__(self):
        return str(self.seq)


class GBNClient:
    def __init__(self):
        self.window_size = 5  # 窗口大小
        self.max_receive_time = 10  # 接收超时时间
        self.address = ('127.0.0.1', 1234)  # 发送方地址
        self.server_address = ('127.0.0.1', 4321)  # 接收方地址
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)  # 新建一个socket
        self.socket.bind(self.address)  # 绑定到本地固定窗口
        self.receive_buffer = []  # 接收缓冲区
        self.buffer_size = 1024  # 缓冲区大小

    def receive(self):
        expected_num = 0  # 希望收到的序列号
        receive_timer = 0  # 接收计时器
        last_ack = -1  # 上一个ack序号
        while True:
            if receive_timer > self.max_receive_time:    # 如果没有超过接收超时时间
                with open('client_receive.txt', 'w') as f:
                    for data in self.receive_buffer:  # 将接收缓冲区的数据全部写入文件
                        f.write(data)
                break

            rs, ws, es = select.select([self.socket, ], [], [], 0.1)  # 进行非阻塞监听

            while len(rs) > 0:  # 若有可读的socket
                rcv_pkt, address = self.socket.recvfrom(self.buffer_size)  # 接收数据
                # 模拟丢包
                message = rcv_pkt.decode()
                ack_num = int(message[0:8])
                if random() < 0.1:
                    receive_timer += 1
                    print('client丢包', str(ack_num))
                    rs, ws, es = select.select([self.socket, ], [], [], 0.1)
                    continue
                message = rcv_pkt.decode()  # 将rcv_pkt解码为字符串编码
                receive_timer = 0  # 接收计时器重新置0

                rcv_seq_num = message[0:8]
                if int(rcv_seq_num) == expected_num:  # 得到收到分组的的序列号(8位)
                    print('client收到了正确分组，向发送方发送ack ' + str(int(rcv_seq_num)) + ' ')  # 打印信息
                    self.receive_buffer.append(rcv_pkt.decode()[8:])  # 将分组中的数据放入接收缓冲区中(8位以后的数据)
                    ack_pkt = ACK('%8d ' % expected_num)  # 创建该pkt的ack分组
                    self.socket.sendto(str(ack_pkt).encode(), self.server_address)  # 向发送方发送ack分组
                    last_ack = expected_num  # 储存最新发送的ack号
                    expected_num += 1  # 下一次期望分组的序列号加1
                else:  # 如果不是期望收到的分组
                    print('client收到的分组不是期望的分组, 向发送方重发期望收到的分组的上一个ack ', str(int(last_ack)) + ' ')  # 打印信息
                    ack_pkt = ACK('%8d ' % last_ack)  # 创建该pkt的ack分组
                    self.socket.sendto(str(ack_pkt).encode(), self.server_address)  # 向发送方重新发送ack分组

                rs, ws, es = select.select([self.socket, ], [], [], 0.1)  # 进行非阻塞监听
            else:  # 非阻塞监听未收到
                receive_timer += 1


def client_start():
    client_socket = GBNClient()
    client_socket.socket.sendto('-testgbn'.encode(), client_socket.server_address)
    client_socket.receive()


if __name__ == '__main__':
    client_start()
