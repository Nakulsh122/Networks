

if [ $# -ne 1 ]; then
    echo "Usage: $0 <number_of_clients>"
    exit 1
fi

NUM_CLIENTS=$1


if [ ! -f "./client" ]; then
    echo "Error: client executable not found in the current directory."
    exit 1
fi

for ((i=1; i<=NUM_CLIENTS; i++))
do
    xterm -hold -e "./client" &

    sleep 0.5  
done

echo "$NUM_CLIENTS client(s) started."
