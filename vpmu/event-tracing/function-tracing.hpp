#ifndef __FUNCTION_TRACING_HPP_
#define __FUNCTION_TRACING_HPP_
#pragma once

#include "et-process.hpp" // ET_Process class

void ft_register_callbacks(std::shared_ptr<ET_Process> process);

#endif
