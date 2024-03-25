import socket as sk
from time import sleep
import sys

def main1():
    s =  sk.socket(sk.AF_INET, sk.SOCK_STREAM)
    s.connect(("localhost", 11202))
    validtest1(s) # get 200
    while data := s.recv(1024):
        print(data)
        print("")

    s.close()

    s =  sk.socket(sk.AF_INET, sk.SOCK_STREAM)
    s.connect(("localhost", 11202))
    validtest2(s) # head 200
    while data := s.recv(1024):
        print(data)
        print("")

    s.close()

    s =  sk.socket(sk.AF_INET, sk.SOCK_STREAM)
    s.connect(("localhost", 11202))
    illegaltest1(s) # get 505
    while data := s.recv(1024):
        print(data)
        print("")
    s.close()

    s =  sk.socket(sk.AF_INET, sk.SOCK_STREAM)
    s.connect(("localhost", 11202))
    illegaltest2(s) # Post 501
    while data := s.recv(1024):
        print(data)
        print("")
    s.close()

        
def validtest1(s):
    s.sendall(b"GE")
    sleep(1)
    s.sendall(b"T / HTTP/1.1")
    sleep(0.5)
    s.sendall(b"\r\n")
    s.sendall(b"Host: %s\r\n" % bytes(sys.argv[1], 'ascii'))
    sleep(0.5)
    s.sendall(b"Connection: close\r\n\r\n")
        
def validtest2(s):
    s.sendall(b"HEAD ")
    sleep(1)
    s.sendall(b"/ HTTP/1.1")
    sleep(0.5)
    s.sendall(b"\r\n")
    s.sendall(b"Host: %s\r\n" % bytes(sys.argv[1], 'ascii'))
    sleep(2)
    s.sendall(b"Connection: close\r\n\r\n")


def illegaltest1(s):
    s.sendall(b"HEAD ")
    sleep(1)
    s.sendall(b"/ HTTP/1.12")
    sleep(0.5)
    s.sendall(b"\r\n")
    s.sendall(b"Host: %s\r\n" % bytes(sys.argv[1], 'ascii'))
    sleep(0.5)
    s.sendall(b"Connection: close\r\n\r\n")

def illegaltest2(s):
    s.sendall(b"POST ")
    sleep(1)
    s.sendall(b"/ HTTP/1.1")
    sleep(0.5)
    s.sendall(b"\r\n")
    s.sendall(b"Host: %s\r\n" % bytes(sys.argv[1], 'ascii'))
    sleep(0.5)
    s.sendall(b"Connection: close\r\n\r\n")


main1()