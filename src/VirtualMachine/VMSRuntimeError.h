#pragma once

#include <exception>

/**
 * @class VMSRuntimeError
 * @author steven piantadosi
 * @date 03/02/20
 * @file Instruction.h
 * @brief This is an error type that is returned if we get a runtime error (e.g. string length, etc.)
 * 		  and it is handled in VMS
 */
class VMSRuntimeError : public std::exception {};
