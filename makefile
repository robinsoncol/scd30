build:
	gcc main.c -o scd30

clean:
	rm -f scd30 data.txt CO2.txt humidity.txt temp.txt

rebuild:
	rm -f scd30 data.txt CO2.txt humidity.txt temp.txt
	gcc main.c -o scd30

run:
	./scd30

all: build run