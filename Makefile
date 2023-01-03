CFLAGS = -Wall -O2 -g -std=c++11

CC = c++
#CC = clang++

OBJSDIR = objs
$(shell mkdir -p $(OBJSDIR))

OBJS = $(OBJSDIR)/test_fcgi.o \
	$(OBJSDIR)/create_socket.o

test_fcgi: $(OBJS)
	$(CC) $(CFLAGS) -o $@  $(OBJS) -lpthread

$(OBJSDIR)/test_fcgi.o: test_fcgi.cpp fcgi_server.h
	$(CC) $(CFLAGS) -c test_fcgi.cpp -o $@

$(OBJSDIR)/create_socket.o: create_socket.cpp
	$(CC) $(CFLAGS) -c create_socket.cpp -o $@

clean:
	rm -f test_fcgi
	rm -f $(OBJSDIR)/*.o
