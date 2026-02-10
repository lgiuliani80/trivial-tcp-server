# Stage 1: Build
FROM gcc:latest AS builder

WORKDIR /app

COPY tcp_server.c .

RUN gcc -o tcp_server tcp_server.c

# Stage 2: Runtime
FROM debian:bookworm-slim

WORKDIR /app

COPY --from=builder /app/tcp_server .

EXPOSE 2323

CMD ["./tcp_server"]
