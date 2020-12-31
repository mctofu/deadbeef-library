# Setup an environment that can successfully run the build.sh script
FROM ubuntu:latest

RUN apt-get update && DEBIAN_FRONTEND="noninteractive" apt-get install -y git curl gcc golang libgtk-3-dev

RUN mkdir -P /usr/local/include/deadbeef && \
  cd /usr/local/include/deadbeef && \
  curl -O https://raw.githubusercontent.com/DeaDBeeF-Player/deadbeef/master/plugins/gtkui/gtkui_api.h && \
  curl -O https://raw.githubusercontent.com/DeaDBeeF-Player/deadbeef/master/deadbeef.h

RUN mkdir /build && \
  cd /build && \
  git clone --depth 1 https://github.com/mctofu/musiclib-grpc

WORKDIR /build
