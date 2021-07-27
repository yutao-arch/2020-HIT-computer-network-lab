import datetime
import socket
from random import random
import select
# 单向数据传输GBN协议


class Data:  # 定义pck
    def __init__(self, seq, data):
        self.seq = seq  # 分组标记的序号
        self.data = data  # 数据字段

    def __str__(self):
        return str(self.seq) + str(self.data)


class GBNServer:

    def __init__(self):
        self.window_size = 5  # 窗口大小
        self.max_send_time = 3   # 发送超时时间
        self.address = ('127.0.0.1', 4321)  # 发送方地址
        self.client_address = ('127.0.0.1', 1234)  # 接收方地址
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)  # 新建一个socket
        self.socket.bind(self.address)  # 绑定到本地固定窗口
        self.send_window = []   # 发送窗口
        self.buffer_size = 1024

    def send(self, buffer):
        send_timer = 0  # 发送计时器
        send_base = 0  # 窗口此时起点的序列号
        next_seq_num = send_base  # 下一个待发送的序列号
        while next_seq_num <= len(buffer)-1:
            # 当发送窗口未满且有数据还需要发送时，发送数据
            while next_seq_num < send_base + self.window_size and next_seq_num < len(buffer):
                pkt = Data('%8d' % next_seq_num, buffer[next_seq_num])  # 从缓冲区取出需要发送的数据
                self.socket.sendto(str(pkt).encode(), self.client_address)  # server发送数据
                print('server发送了pkt' + str(next_seq_num) + ' ')  # 打印发送信息
                self.send_window.append(pkt)  # 将数据加入发送窗口
                if send_base == next_seq_num:  # 只要该数据的序列号不是起点就将序列号加一
                    send_timer = 0
                next_seq_num = next_seq_num + 1

            # 若发送的时间超过给定的发送超时时间，则重传发送窗口中的数据
            if send_timer > self.max_send_time and self.send_window:
                print('server发送超时,需要重传')
                send_timer = 0  # 发送计时器重新置0
                for pkt in self.send_window:  # 对于发送窗口中所有pkt数据
                    self.socket.sendto(str(pkt).encode(), self.client_address)  # 重传数据
                    print('server重传的数据包为 ' + str(int(str(pkt.seq))) + ' ')

            rs, ws, es = select.select([self.socket, ], [], [], 0.1)  # 进行非阻塞监听

            while len(rs) > 0:  # 若有可读的socket
                rcv_pkt, address = self.socket.recvfrom(self.buffer_size)  # 接收数据
                # 模拟丢包
                message = rcv_pkt.decode()
                ack_num = int(message[0:8])
                if random() < 0.1:
                    send_timer += 1
                    print('Server丢包ack', str(ack_num))
                    rs, ws, es = select.select([self.socket, ], [], [], 0.1)
                    continue
                print('Server收到了ack', str(ack_num))
                for i in range(len(self.send_window)):  # 遍历发送窗口
                    if ack_num == int(self.send_window[i].seq):  # 如果发送窗口找到了这个确认序列号
                        self.send_window = self.send_window[i + 1:]  # 将窗口后移
                        break
                send_base = ack_num + 1  # 窗口起点序列号更新
                print("此时窗口起点位置为：", send_base)
                send_timer = 0  # 发送计时器置0

                rs, ws, es = select.select([self.socket, ], [], [], 0.1)  # 进行非阻塞监听
            else:  # 非阻塞监听未收到
                send_timer += 1


def server_start():
    server_socket = GBNServer()
    data = []
    with open('server_send.txt', "r") as f:
        while True:
            pkt = f.read(10)
            if len(pkt) > 0:
                data.append(pkt)
            else:
                break
    timer = 0
    while True:
        # 服务器计时器，如果收不到客户端的请求则退出
        if timer > 20:
            return
        rs, ws, es = select.select([server_socket.socket, ], [], [], 1)
        if len(rs) > 0:
            message, address = server_socket.socket.recvfrom(server_socket.buffer_size)
            if message.decode() == '-testgbn':
                server_socket.send(data)
        timer += 1


if __name__ == '__main__':
    server_start()
