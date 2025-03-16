#!/bin/bash

# Check if number of clients is provided
if [ $# -ne 1 ]; then
    echo "Usage: $0 <number_of_clients>"
    exit 1
fi

NUM_CLIENTS=$1

# Check if client executable exists
if [ ! -f "./client" ]; then
    echo "Error: client executable not found in the current directory."
    exit 1
fi

# Loop to spawn terminals
for ((i=1; i<=NUM_CLIENTS; i++))
do
    xterm -hold -e "./client" &

    sleep 0.5  # Add a slight delay to prevent race conditions
done

echo "$NUM_CLIENTS client(s) started."
