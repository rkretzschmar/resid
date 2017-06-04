if g++ **.cc -o pipe_dream; then
    ./pipe_dream < ../siddump/out1 $@
else
    echo "Compile failed"
fi

