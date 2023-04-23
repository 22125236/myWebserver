a.out: main.o http_conn.o
	g++ main.o http_conn.o -o a.out -pthread
main.o: main.cpp
	g++ main.cpp -o main.o -pthread
http_conn.o: ./http_conn/http_conn.cpp
	g++ ./http_conn/http_conn.cpp -o http_conn.o -pthread
