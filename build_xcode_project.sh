#!/usr/bin/env bash

BUILD_PATH=cmake-build-xcode

function build_xcode_project() {
  if [ -r $BUILD_PATH ]; then
    rm -rf $BUILD_PATH
  fi

  mkdir $BUILD_PATH
  cd $BUILD_PATH

  cmake -G "Xcode" ..
}

build_xcode_project

# 打开xcode工程
open Mixer.xcodeproj




