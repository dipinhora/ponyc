FROM ubuntu:18.04

ENV PATH "/root/.local/share/ponyup/bin:$PATH"

RUN apt-get update \
 && apt-get install -y \
    curl \
    g++ \
    git

RUN curl -s --proto '=https' --tlsv1.2 -sSf https://raw.githubusercontent.com/ponylang/ponyup/master/ponyup-init.sh | sh \
 && ponyup update ponyc nightly --libc=gnu \
 && ponyup update stable nightly \
 && ponyup update corral nightly \
 && ponyup update changelog-tool nightly

WORKDIR /src/main

CMD ponyc
