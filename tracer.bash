#!/bin/bash

# Create the debug directory
mkdir /debug

# Mount the debug filesystem
mount -t debugfs nodev /debug
mount -t debugfs nodev /sys/kernel/debug

# Clear the current trace buffer
echo > /debug/tracing/trace

# Set the ftrace filter to include net_rx_action and related functions
echo > /debug/tracing/set_ftrace_filter

# Set the current tracer to function_graph
echo function_graph > /debug/tracing/current_tracer

# Enable tracing
echo 1 > /debug/tracing/tracing_on

# Wait for 5 seconds
sleep 5

# Disable tracing
echo 0 > /debug/tracing/tracing_on

# Output the trace to a file
cat /debug/tracing/trace > /tmp/tracing.out$$
