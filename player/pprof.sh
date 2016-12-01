make clean; make PROFILE=1
./leiserchess input.txt
google-pprof --svg leiserchess profile.data > out.svg
google-pprof --lines --svg leiserchess profile.data > out2.svg
cp out* ~/www/

# go to http://web.mit.edu/~hjxu/www/out.svg (or out2.svg) to see the results
