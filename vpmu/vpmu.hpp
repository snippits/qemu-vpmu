#ifndef __VPMU_HPP_
#define __VPMU_HPP_
#pragma once

extern "C" {
#include "vpmu.h" // VPMU common header
}
#include <vector>   // std::vector
#include <string>   // std::string
#include <thread>   // std::thread
#include <iostream> // Basic I/O related C++ header
#include <fstream>  // File I/O
#include <sstream>  // String buffer
#include <cerrno>   // Readable error messages

#include "vpmu-log.hpp" // VPMU_Log
#include "json.hpp"     // nlohmann::json

#ifndef TD
// Used for debugging C++ type deduction
template <typename T> // Declaration for TD
class TD;             // TD == "Type Displayer"
#define VPMU_DEBUG_SHOW_T_TYPE(_TYPE) TD<_TYPE>           The_T_type_is;
#define VPMU_DEBUG_SHOW_VAR_TYPE(_VAR) TD<decltype(_VAR)> The_variable_type_is;
#endif

void VPMU_async(std::function<void(void)> task);

#endif
