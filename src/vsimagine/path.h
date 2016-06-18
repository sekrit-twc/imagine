#pragma once

#ifndef PATH_H_
#define PATH_H_

#include <string>

bool path_exists(const std::string &path);

std::string path_canonicalize(const std::string &path);

#endif PATH_H_
