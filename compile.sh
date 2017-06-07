g++ **.cc -o pipe_dream
#gcc lib_pipe_dream.cc -fPIC -c -o lib_pipe_dream
#gcc -shared lib_pipe_dream -o pipe_dream.so
g++ -fPIC *.cc -shared -o libpipe_dream.so 
