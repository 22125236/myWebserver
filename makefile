CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2

endif

server: main.cpp http_conn/http_conn.cpp instance/instance.cpp instance/sql_conn/sql_conn_pool.cpp server/server.cpp timer/lst_timer.cpp log/log.cpp
	$(CXX) -o webserver $^ $(CXXFLAGS) -pthread -lmysqlclient -std=c++11
clean:
	rm  -r webserver

