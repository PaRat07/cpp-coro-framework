# ─── Builder Stage ────────────────────────────────────────────────
FROM ubuntu:24.04

RUN apt update && apt upgrade
RUN apt install -y make liburing-dev ninja-build liburing-dev lsb-release wget software-properties-common gnupg
RUN wget https://apt.llvm.org/llvm.sh
RUN chmod +x llvm.sh
RUN apt install -y
RUN ./llvm.sh 20 all
RUN wget https://github.com/Kitware/CMake/releases/download/v4.0.2/cmake-4.0.2-linux-x86_64.sh
RUN mkdir /opt/cmake && sh cmake-4.0.2-linux-x86_64.sh --skip-license --prefix=/opt/cmake
RUN ln -s /opt/cmake/bin/cmake /usr/local/bin/cmake
RUN apt install -y git
RUN mkdir workdir
WORKDIR workdir
COPY . .
CMD ["make", "run-debug"]


