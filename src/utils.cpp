/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   utils.cpp                                          :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: christian <christian@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/08/03 06:41:01 by christian         #+#    #+#             */
/*   Updated: 2025/08/20 16:48:49 by christian        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "utils.hpp"
#include <fstream>

std::string get_timestamp()
{
	time_t rawtime;
	struct tm * timeinfo;
	char buffer[80];

	time(&rawtime);
	timeinfo = localtime(&rawtime);

	strftime(buffer, sizeof(buffer), "%H:%M:%S", timeinfo);
	return std::string(buffer);
}

void log_debug(const std::string &message)
{
	std::string timestamp = get_timestamp();
	std::cout << "[" << timestamp << "]:[\033[36mDEBUG\033[0m]   " << message << std::endl;
}

void log_info(const std::string &message)
{
	std::string timestamp = get_timestamp();
	std::cout << "[" << timestamp << "]:[\033[32mINFO\033[0m]    " << message << std::endl;
}

void log_error(const std::string &message)
{
	std::string timestamp = get_timestamp();
	std::cerr << "[" << timestamp << "]:[\033[31mERROR\033[0m]   " << message << std::endl;
}

std::string int_to_string(int value){
	std::ostringstream oss;
	oss << value;
	return oss.str();
}

std::string size_t_to_string(size_t value){
	std::ostringstream oss;
	oss << value;
	return oss.str();
}

bool fileExists(const std::string& path) {
	std::ifstream file(path.c_str());
	bool exists = file.good();
	file.close();
	return exists;
}
