.PHONY: clean build

build: server subscriber

server: connection.cpp topics.cpp server.cpp main_server.cpp
	g++ connection.cpp topics.cpp server.cpp main_server.cpp -o server

subscriber: client.cpp connection.cpp
	g++ client.cpp connection.cpp -o subscriber

clean:
	rm -rf subscriber server
