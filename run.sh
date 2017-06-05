if g++ **.cc -o pipe_dream; then
    ./pipe_dream < ../sidparse/out1 $@
else
    echo "Compile failed"
fi

