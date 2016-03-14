CC=gcc
TASK=hello_world

all: $(TASK)

hello_world:
	$(CC) $(TASK).c -o $(TASK).o -c
	$(CC) -s $(TASK).o -o $(TASK)

clean:
	rm -rf *.o $(TASK)
