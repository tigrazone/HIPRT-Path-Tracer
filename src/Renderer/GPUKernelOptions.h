/*
 * Copyright 2024 Tom Clabault. GNU GPL3 license.
 * GNU GPL3 license copy: https://www.gnu.org/licenses/gpl-3.0.txt
 */

#ifndef GPU_KERNEL_OPTIONS_H
#define GPU_KERNEL_OPTIONS_H

#include <string>
#include <unordered_map>
#include <vector>

class GPUKernelOptions
{
public:
	static const std::string INTERIOR_STACK_STRATEGY;
	static const std::string DIRECT_LIGHT_SAMPLING_STRATEGY;
	static const std::string ENVMAP_SAMPLING_STRATEGY;
	static const std::string RIS_USE_VISIBILITY_TARGET_FUNCTION;

	GPUKernelOptions();

	/**
	 * Gets a list of compiler options of the form { "-D InteriorStackStrategy=1", ... }
	 * that can directly be passed to the kernel compiler
	 */
	std::vector<std::string> get_as_std_vector_string();

	/**
	 * Replace the value of the macro if it has already been added previous to this call
	 * 
	 * The @name parameter is expected to be given without the '-D' macro prefix.
	 * For example, if you want to define a macro "MyMacro" equal to 1, you simply
	 * call set_macro("MyMacro", 1).
	 * 
	 * The addition of the -D prefix will be added internally
	 */
	void set_macro(const std::string& name, int value);

	/**
	 * Removes a macro from the list given to the compiler
	 */
	void remove_macro(const std::string& name);

	/**
	 * Returns true if the given macro is defined. False otherwise
	 */
	bool has_macro(const std::string& name);

	/** 
	 * Gets the value of a macro or -1 if the macro isn't set
	 */
	int get_macro_value(const std::string& name);

	/**
	 * Returns a pointer to the value of a macro given its name.
	 * 
	 * Useful for use with ImGui for example.
	 * 
	 * nullptr is returned if the option doesn't exist (set_macro() wasn't called yet)
	 */
	int* get_pointer_to_macro_value(const std::string& name);

private:
	std::unordered_map<std::string, int> m_options_map;
};


#endif