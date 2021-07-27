import socket
from random import random
import select
import datetime


class Data:  # 定义GBN的分组格式
    def __init__(self, is_ack, seq, data):
        self.is_ack = is_ack  # 分组类型，0表示数据分组，1表示确认分组
        self.seq = seq  # 分组标记的序号
        self.data = data  # 数据字段

    def __str__(self):
        return str(self.is_ack) + str(self.seq) + str(self.data)


class GBNServer:
    def __init__(self):
        self.window_size = 5  # 窗口大小
        self.max_send_time = 3  # 发送超时时间
        self.max_receive_time = 10  # 接收超时时间
        self.address = ('127.0.0.1', 4321)  # 发送方地址
        self.client_address = ('127.0.0.1', 1234)  # 接收方地址
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)  # 新建一个socket
        self.socket.bind(self.address)  # 绑定到本地固定窗口
        self.send_window = []  # 发送窗口
        self.receive_buffer = []  # 接收缓冲区
        self.buffer_size = 1024  # 缓冲区大小

    '''
    发送和接收功能
    '''
    def send_and_receive(self, buffer):
        send_timer = 0  # 发送计时器
        send_base = 0  # 窗口此时起点的序列号
        next_seq_num = send_base  # 下一个待发送的序列号
        expected_num = 0  # 希望收到的序列号
        receive_timer = 0  # 接收计时器
        last_ack = -1  # 上一个ack序号
        total = len(buffer)  # 缓冲区大小
        while True:
            if not self.send_window and receive_timer > self.max_receive_time:  # 如果没有超过接收超时时间
                with open('server_receive.txt', 'w') as f:
                    for data in self.receive_buffer:  # 将接收缓冲区的数据全部写入文件
                        f.write(data)
                break

            # 发送阶段
            # 当发送窗口未满且有数据还需要发送时，发送数据
            while next_seq_num < send_base + self.window_size and next_seq_num < total:
                pkt = Data(0, '%8d' % next_seq_num, buffer[next_seq_num])  # 从缓冲区取出需要发送的数据
                self.socket.sendto(str(pkt).encode(), self.client_address)  # server发送数据
                print('server发送了pkt ' + str(next_seq_num))  # 打印发送信息
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
                    print('server重传的数据包为 ' + str(pkt.seq))

            # 接收阶段
            rs, ws, es = select.select([self.socket, ], [], [], 0.1)  # 进行非阻塞监听

            while len(rs) > 0:  # 若有可读的socket
                rcv_pkt, address = self.socket.recvfrom(self.buffer_size)  # 接收数据
                # 模拟丢包
                message = rcv_pkt.decode()
                ack_num = int(message[1:9])
                if random() < 0.1:  # 随机产生一个0到1之间的浮点数，若小于0.1，则说明丢包率小于10%，则不处理该数据
                    send_timer += 1
                    receive_timer += 1
                    print('server丢包'+str(ack_num))
                    rs, ws, es = select.select([self.socket, ], [], [], 0.1)
                    continue
                message = rcv_pkt.decode()  # 将rcv_pkt解码为字符串编码
                receive_timer = 0  # 接收计时器重新置0

                if message[0] == '1':  # 如果收到的分组是确认分组
                    ack_num = int(message[1:9])  # 得到确认序列号(8位)
                    print('server收到ack: ' + str(ack_num))
                    for i in range(len(self.send_window)):  # 遍历发送窗口
                        if ack_num == int(self.send_window[i].seq):  # 如果发送窗口找到了这个确认序列号
                            self.send_window = self.send_window[i + 1:]  # 将窗口后移
                            break
                    send_base = ack_num + 1  # 窗口起点序列号更新
                    print("此时窗口起点位置为：", send_base)
                    send_timer = 0  # 发送计时器置0
                elif message[0] == '0':  # 如果收到的分组是数据分组
                    rcv_seq_num = message[1:9]  # 得到收到分组的的序列号(8位)
                    if int(rcv_seq_num) == expected_num:  # 如果是期望收到的分组
                        print('server收到了正确分组，向发送方发送ack' + rcv_seq_num)  # 打印信息
                        self.receive_buffer.append(rcv_pkt.decode()[9:])  # 将分组中的数据放入接收缓冲区中(9位以后的数据)
                        ack_pkt = Data(1, '%8d ' % expected_num, '')  # 创建该pkt的ack分组
                        self.socket.sendto(str(ack_pkt).encode(), self.client_address)  # 向发送方发送ack分组
                        last_ack = expected_num  # 储存最新发送的ack号
                        expected_num += 1  # 下一次期望分组的序列号加1
                    else:  # 如果不是期望收到的分组
                        print('server收到的分组不是期望的分组, 向发送方重发期望收到的分组的上一个ack', last_ack)  # 打印信息
                        ack_pkt = Data(1, '%8d ' % last_ack, '')  # 创建该pkt的ack分组
                        self.socket.sendto(str(ack_pkt).encode(), self.client_address)  # 向发送方重新发送ack分组
                else:
                    pass
                rs, ws, es = select.select([self.socket, ], [], [], 0.1)  # 进行非阻塞监听
            else:  # 非阻塞监听未收到
                receive_timer += 1
                send_timer += 1


def start():
    server_socket = GBNServer()
    data = []
    with open('server_send.txt', 'r') as f:  # 打开发送内容的文件
        while True:
            pkt = f.read(10)
            if len(pkt) > 0:  # 读取文件并储存在data中，data是需要发送的文件内容
                data.append(pkt)
            else:
                break
    timer = 0  # 服务器计时器置0
    while True:
        # 服务器计时器，如果收不到客户端的请求则退出
        if timer > 20:  # 超时则退出
            return
        rs, ws, es = select.select([server_socket.socket, ], [], [], 1)  # 进行非阻塞监听
        if len(rs) > 0:    # 若有可读的socket
            message, address = server_socket.socket.recvfrom(server_socket.buffer_size)  # 服务器接收数据
            if message.decode() == '-time':  # 如果信息为-testgbn，则执行
                print(datetime.datetime.now())
            if message.decode() == '-testgbn':  # 如果信息为-testgbn，则执行
                server_socket.send_and_receive(data)
            if message.decode() == '-quit':  # 如果信息为-testgbn，则停止
                print('Good bye!')
                return
        timer += 1


if __name__ == '__main__':
    start()

