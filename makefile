build:
	gcc main.c -o scd30

clean:
	rm -f scd30 data.txt

run:
	./scd30

# startup:
	# echo "hello, world"
	# ln -s $(PWD)/startup/scd30.service /etc/systemd/system/scd30.service
	# systemctl daemon-reload 
	# systemctl enable scd30.service

all: build run

