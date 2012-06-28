all:
	gcc -g -shared -fPIC -ldl bind_public.c -o bind_public.so

test:
	gcc -g test.c -o test

clean:
	rm -f bind_public.so test
