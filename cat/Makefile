CC=gcc
TASK=cat

all: $(TASK)

cat:
	$(CC) $(TASK).c -o $(TASK).o -c
	$(CC) -s $(TASK).o -o $(TASK)

clean:
	rm -rf *.o $(TASK)
