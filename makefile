build:
	gcc main.c -o scd30

clean:
	rm -f scd30 data.txt

run:
	./scd30

all: build run

