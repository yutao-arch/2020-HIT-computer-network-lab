import socket
from random import random
import select
import datetime


class Data:  # 定义SR的分组格式
    def __init__(self, is_ack, seq, data):
        self.is_ack = is_ack  # 分组类型，0表示数据分组，1表示确认分组
        self.seq = seq  # 分组标记的序号
        self.data = data  # 数据字段

    def __str__(self):
        return str(self.is_ack) + str(self.seq) + str(self.data)


class DataGram:  # 封装Data类
    def __init__(self, pkt, timer=0, is_acked=False):
        self.pkt = pkt
        self.timer = timer  # 计时器
        self.is_acked = is_acked  # 判断该组是否已经被ack


class SRServer:
    def __init__(self):
        self.send_window_size = 5   # 发送窗口大小
        self.receive_window_size = 5  # 接收窗口大小
        self.max_send_time = 5  # 发送超时时间
        self.max_receive_time = 15  # 接收超时时间
        self.address = ('127.0.0.1', 8765)  # 发送方地址
        self.client_address = ('127.0.0.1', 5678)   # 接收方地址
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)  # 新建一个socket
        self.socket.bind(self.address)  # 绑定到本地固定窗口
        self.send_window = []   # 发送窗口
        self.receive_data = []  # 有序数据
        self.receive_window = {}  # 接收窗口
        self.buffer_size = 1024  # 缓冲区大小

    '''
    发送和接收功能
    '''
    def send_and_receive(self, buffer):
        send_base = 0  # 窗口此时起点的序列号
        next_seq_num = send_base  # 下一个待发送的序列号
        expected_num = 0  # 希望收到的序列号
        receive_timer = 0  # 接收计时器
        total = len(buffer)  # 缓冲区大小
        while True:
            if not self.send_window and receive_timer > self.max_receive_time:  # 如果没有超过接收超时时间
                with open('server_receive.txt', 'w') as f:
                    for data in self.receive_data:  # 将receive_data的数据全部写入文件
                        f.write(data)
                break

            # 发送阶段
            # 当发送窗口未满且有数据还需要发送时，发送数据
            while next_seq_num < send_base + self.send_window_size and next_seq_num < total:
                pkt = Data(0, '%8d' % next_seq_num, buffer[next_seq_num])  # 从缓冲区取出需要发送的数据
                self.socket.sendto(str(pkt).encode(), self.client_address)  # server发送数据
                print('server发送了pkt' + str(next_seq_num))  # 打印发送信息
                self.send_window.append(DataGram(pkt))  # 给每个分组计时器初始化为0，把数据报加入发送窗口
                next_seq_num = next_seq_num + 1  # 将next_seq_num序列号加一

            # 遍历所有已发送但未确认的分组，如果有超时的分组，则重发该分组
            for dgram in self.send_window:
                if dgram.timer > self.max_send_time and not dgram.is_acked:  # 如果已经超时且该组没有被接收
                    self.socket.sendto(str(dgram.pkt).encode(), self.client_address)  # 重发该分组
                    print('server重传的数据包为' + str(dgram.pkt.seq))

            # 接收阶段
            rs, ws, es = select.select([self.socket, ], [], [], 0.01)  # 非阻塞监听
            while len(rs) > 0:  # 若有可读的socket
                rcv_pkt, address = self.socket.recvfrom(self.buffer_size)  # 接收数据
                # 模拟丢包
                message = rcv_pkt.decode()
                ack_num = int(message[1:9])
                if random() < 0.1:  # 随机产生一个0到1之间的浮点数，若小于0.1，则说明丢包率小于10%，则不处理该数据
                    for dgram in self.send_window:  # 对所有分组计时器加1
                        dgram.timer += 1
                    receive_timer += 1
                    print('server丢包 ', str(ack_num))
                    rs, ws, es = select.select([self.socket, ], [], [], 0.01)
                    continue
                message = rcv_pkt.decode()  # 将rcv_pkt解码为字符串编码
                receive_timer = 0  # 接收计时器重新置0

                if message[0] == '1':  # 如果收到的分组是确认分组
                    ack_num = int(message[1:9])  # 得到确认序列号(8位)
                    print('server收到ack' + str(ack_num))
                    for dgram in self.send_window:   # 在窗口中找序号为ack_num的分组，并把is_acked设为True
                        if int(dgram.pkt.seq) == ack_num:  # 如果找到了ack_num
                            dgram.timer = 0  # 计时器置0
                            dgram.is_acked = True  # 标记为已收到
                            if self.send_window.index(dgram) == 0:  # 如果ack的是发送窗口中的第一个分组
                                idx = -1  # 用idx表示窗口中最后一个已确认的的分组的下标
                                for i in self.send_window:  # 依次遍历发送窗口中已确认的分组的个数
                                    if i.is_acked:  # 记录从第一个分组开始连续分组的个数
                                        idx += 1
                                    else:  # 不连续则退出
                                        break

                                send_base = int(self.send_window[idx].pkt.seq) + 1
                                print("此时窗口起点位置为：", send_base)
                                self.send_window = self.send_window[idx+1:]  # 窗口滑动到最后一个连续已确认的分组序号之后
                            break
                elif message[0] == '0':  # 如果收到的分组是数据分组
                    for dgram in self.send_window:  # 发送窗口中所有分组计时器+1
                        dgram.timer += 1
                    rcv_seq_num = message[1:9]  # 得到收到分组的的序列号(8位)
                    if int(rcv_seq_num) == expected_num:  # 如果是期望收到的分组
                        print('server收到了正确分组，向发送方发送ack' + str(rcv_seq_num))    # 打印信息
                        ack_pkt = Data(1, '%8d ' % expected_num, '')  # 创建该pkt的ack分组
                        self.socket.sendto(str(ack_pkt).encode(), self.client_address)  # 向发送方发送ack分组
                        # 再看它后面有没有能合并的分组
                        self.receive_window[int(rcv_seq_num)] = rcv_pkt  # 将最新收到的数据序列号加给接收窗口
                        tmp = [(k, self.receive_window[k]) for k in sorted(self.receive_window.keys())]  # 按照序列号对接收窗口进行排序
                        idx = 0   # idx为接受窗口中能合并到的最后一个分组，为连续数对的个数
                        for i in range(len(tmp) - 1):
                            if tmp[i + 1][0] - tmp[i][0] == 1:  # 如果两个序列号相差为1
                                idx += 1
                            else:  # 相差不为1跳出（不连续）
                                break
                        for i in range(idx + 1):
                            self.receive_data.append(tmp[i][1].decode()[9:])   # 把接收窗口中的数据提交给receive_data
                        # 记录提交的分组的序号
                        base = int(tmp[0][1].decode()[1:9])  # 写入receive_data的序列号起点
                        end = int(tmp[idx][1].decode()[1:9])  # 写入receive_data的序列号终点
                        if base != end:
                            print('server 向上层提交数据: ' + str(base) + ' 到 ' + str(end))
                        else:
                            print('server 向上层提交数据: ' + str(base))
                        expected_num = tmp[idx][0]+1  # 下一次期望分组的序列号加1
                        tmp = tmp[idx + 1:]   # 接收窗口滑动
                        self.receive_window = dict(tmp)
                    else:  # 如果不是期望收到的分组
                        if expected_num <= int(rcv_seq_num) < expected_num + self.receive_window_size - 1:  # 如果rcv_seq_num在接收窗口内，即若在rcv_base~rcv_base + N -1之内，则加入接收窗口
                            self.receive_window[int(rcv_seq_num)] = rcv_pkt  # 加入接收窗口
                            ack_pkt = Data(1, '%8d ' % int(rcv_seq_num), '')  # 创建该pkt的ack分组
                            print('server收到的分组不是期望的分组，但在接收窗口内，向发送方发送该分组对应的ack' + str(rcv_seq_num))  # 打印信息
                            self.socket.sendto(str(ack_pkt).encode(), self.client_address)  # 向发送方发送ack分组
                        elif int(rcv_seq_num) < expected_num:  # 如果收到的分组不在接收窗口内（窗口前）
                            ack_pkt = Data(1, '%8d ' % int(rcv_seq_num), '')  # 创建该pkt的ack分组
                            print('server收到的分组不是期望的分组，但不在接收窗口内，向发送方发送该分组对应的ack' + str(rcv_seq_num))
                            self.socket.sendto(str(ack_pkt).encode(), self.client_address)  # 发送ack分组
                        else:
                            pass
                else:
                    pass
                rs, ws, es = select.select([self.socket, ], [], [], 0.01)  # 进行非阻塞监听
            else:  # 非阻塞监听未收到
                receive_timer += 1
                for dgram in self.send_window:
                    dgram.timer += 1


def start():
    server_socket = SRServer()
    data = []
    with open('server_send.txt', 'r') as f:  # 打开发送内容的文件
        while True:
            pkt = f.read(10)
            if len(pkt) > 0:  # 读取文件并储存在data中，data是需要发送的文件内容
                data.append(pkt)
            else:
                break
    timer = 0
    while True:
        # 服务器计时器，如果收不到客户端的请求则退出
        if timer > 20:  # 超时则退出
            return
        rs, ws, es = select.select([server_socket.socket, ], [], [], 1)  # 进行非阻塞监听
        if len(rs) > 0:  # 若有可读的socket
            message, address = server_socket.socket.recvfrom(server_socket.buffer_size)  # 服务器接收数据
            if message.decode() == '-time':  # 如果信息为-testgbn，则执行
                print(datetime.datetime.now())
            if message.decode() == '-testsr':  # 如果信息为-testsr，则执行
                server_socket.send_and_receive(data)
            if message.decode() == '-quit':  # 如果信息为-testgbn，则停止
                print('Good bye!')
                return
        timer += 1


if __name__ == '__main__':
    start()
