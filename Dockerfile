FROM gcc:13
WORKDIR /app
COPY server.c .
RUN gcc -O2 -o attendance server.c
CMD ["./attendance"]
