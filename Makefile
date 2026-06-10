.c.o:
	cc -c $< -O

quality: quality.o
	cc -o $@ $<

install:
	cp quality /usr/local/bin

