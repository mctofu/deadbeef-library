#/bin/bash

gcc `pkg-config gtk+-3.0 --cflags` `pkg-config --libs gtk+-3.0` -std=c99 -shared -O2 -g -fPIC -DDEBUG=1 -o ddb_misc_libbrowser_GTK3.so libbrowser.c utils.c support.c client.c $(pwd)/../musiclib-grpc/cgo/build/client.so

mkdir -p ~/.local/lib/deadbeef/
cp ddb_misc_libbrowser_GTK3.so ~/.local/lib/deadbeef/ddb_misc_libbrowser_GTK3.so
