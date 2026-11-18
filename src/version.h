// Pikulix のバージョン。リリースのときは、ここだけを書き換える。
//
// main.cpp（--version）と pikulix.rc（exe のファイルのプロパティ）が使う。
// Rakefile も、zip の名前を決めるためにここを読み、文字列と数値が食い違っていないかを
// 確かめる。リリースのタグ（v0.1.0 など）は、この文字列と一致させる。
#pragma once

#define PIKULIX_VERSION "0.1.1"
#define PIKULIX_VERSION_MAJOR 0
#define PIKULIX_VERSION_MINOR 1
#define PIKULIX_VERSION_PATCH 1
