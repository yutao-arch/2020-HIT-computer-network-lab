import threading
from Lab2.GBN_client import start as gbn_client_start
from Lab2.GBN_server import start as gbn_server_start


def main():
    t1 = threading.Thread(target=gbn_server_start, args=())
    t2 = threading.Thread(target=gbn_client_start, args=())
    t1.start()
    t2.start()


if __name__ == '__main__':
    main()