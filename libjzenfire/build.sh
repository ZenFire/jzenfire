#!/bin/bash


if test -z "$ZENFIRE_DIR"; then
  echo please point environment variable ZENFIRE_DIR to source dir
  exit 1
fi

zf_inc_dir=$ZENFIRE_DIR/include
zf_lib=$ZENFIRE_DIR/lib/linux/lib32/libzenfire.a

if test ! -e $zf_inc_dir/zenfire/zenfire.hpp; then
  echo Failed to find zenfire headers
  exit 1
fi
if test ! -e $zf_lib; then
  echo Failed to find zenfire library
  exit 1
fi

g++ -m32 -g -c src/libjzenfire.cpp -o src/libjzenfire.o -I$zf_inc_dir -I$JAVA_HOME/include -I$JAVA_HOME/include/linux
g++ -m32 -shared -o libjzenfire.so  src/libjzenfire.o $zf_lib -lpthread -lrt

