chat-server:
	 gcc -Wall -Wextra -std=c99 -o chat-server chat-server.c http-server.c
clean:
	rm -f chat-server a.out

