// WritersRegistry.hpp — the table of writers the program knows, by role.
//
// main.cpp asks for writers by the roles named in --writers, or for every writer whose needs
// the given inputs satisfy (--writers auto). Adding a writer: include its header and add one
// line to the table in WritersRegistry.cpp. Nothing registers itself.
#pragma once

#include "Options.hpp"
#include "Writer.hpp"

#include <memory>
#include <string>
#include <vector>

/// Writers for the named roles, in the given order. Throws std::invalid_argument for an
/// unknown role, listing the known ones.
std::vector<std::unique_ptr<Writer>> make_writers(const std::vector<std::string>& roles);

/// Every writer whose needs the options' inputs satisfy, in table order. Prints one line per
/// skipped writer with the input it lacks.
std::vector<std::unique_ptr<Writer>> make_writers_for_inputs(const Options& options);

/// The --list-writers text: role, needs and collections of every writer.
std::string list_writers_text();

/// The --list-columns text: one line per output, "<role>[_<suffix>]: <header>".
std::string list_columns_text();
