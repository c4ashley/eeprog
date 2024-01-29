eeprog: main.c
	cc -Wall -Wextra -Og -g3 -o $@ $^

install: eeprog
	install -s eeprog /usr/local/bin/

clean:
	rm eeprog
