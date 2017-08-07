#ifndef __FUNCTION_TRACING_HPP_
#define __FUNCTION_TRACING_HPP_
#pragma once

#include "et-process.hpp" // ET_Process class

void ft_load_callbacks(std::shared_ptr<ET_Process> process,
                       std::shared_ptr<ET_Program> program);
void ft_register_callbacks(void);

#endif
