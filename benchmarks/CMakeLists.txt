cmake_minimum_required(VERSION 3.10)

project(benchmarks)

# Add the common directory for benchmark configurations
include_directories(common)

# Add the runner directory
add_subdirectory(runner)

