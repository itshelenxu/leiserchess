# install pprof
sudo apt-get update
sudo apt-get install google-perftools libgoogle-perftools-dev

#set up environment
LD_PRELOAD=/usr/lib/libprofiler.so CPUPROFILE=cpu_profile.0 ./leiserches
# go depth 8, quit

# read into profiling
google-pprof --text ./leiserchess cpu_profile.0 > "profiling.txt" 
