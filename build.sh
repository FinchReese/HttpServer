#!/bin/bash

build_path="./build"
output_path="./output"

if [ ! -d "$build_path" ]; then
    mkdir $build_path
else
    rm -rf $build_path/*
fi
if [ ! -d "$output_path" ]; then
    mkdir $output_path
else
    rm -rf $output_path/*
fi

cd ${build_path}
cmake -DCMAKE_BUILD_TYPE=Debug ..
make