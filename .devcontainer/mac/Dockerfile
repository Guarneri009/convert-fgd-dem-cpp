#FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04
FROM nvidia/cuda:12.2.2-devel-ubuntu22.04


#シェル指定 SHELL命令を使うとDockerfile内のRUNがどのシェル+引数によって実行されるかを指定できる。
# bashは最初に/etc/profileを読み取ります。
SHELL ["/usr/bin/bash", "-l", "-c"]

ENV DEBIAN_FRONTEND=noninteractive
# ロケール環境変数
ENV TZ=Asia/Tokyo
ENV LC_ALL=ja_JP.UTF-8

RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get -y install --no-install-recommends \
    curl cmake ninja-build git sudo wget ca-certificates openssh-client clang-format unzip \
    pkg-config libzip-dev libgdal-dev gdal-bin libtbb-dev libboost-all-dev

# claude-codeのインストール
RUN curl -fsSL https://deb.nodesource.com/setup_22.x | bash -
RUN apt-get install nodejs -y
RUN npm install -g @anthropic-ai/claude-code

# justのインストール
RUN curl --proto '=https' --tlsv1.2 -sSf https://just.systems/install.sh | bash -s -- --to /usr/local/bin

# create vscode user
ARG username=vscode
ARG wkdir=/home/vscode

RUN echo "root:root" | chpasswd && \
    adduser --disabled-password --gecos "" "${username}" && \
    echo "${username}:${username}" | chpasswd && \
    echo "%${username}    ALL=(ALL)   NOPASSWD:    ALL" >> /etc/sudoers.d/${username} && \
    chmod 0440 /etc/sudoers.d/${username}

WORKDIR ${wkdir}
RUN chown ${username}:${username} ${wkdir}
USER ${username}

# ワークディレクトリーをセーフに設定
RUN git config --global --add safe.directory /workspace
RUN git config --global user.name "Guarneri009"
RUN git config --global user.email "driftinghide@gmail.com"
