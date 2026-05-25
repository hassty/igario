FROM alpine:3.23 AS builder
WORKDIR /igario

RUN apk update && \
    apk add --no-cache gcc make musl-dev
COPY base/ ./base/
COPY vendor/ ./vendor/
COPY Makefile igario_server.c game.h  ./
RUN make server

FROM alpine:3.23
COPY --from=builder /igario/build/igario_server /igario_server
CMD ["/igario_server", "1337"]
