
# Simple_Advanced_Webserver
A simple web server that integrates threads, sendfile, epoll, and python scripting. 

I had to install the python3-dev to get the c interface to python
sudo apt-get install python3-dev

To find where the includes are, I had to run
python3-config --includes

gcc *.c -I/usr/include/python3.11 -I/usr/include/python3.11 -lpython3.11 -ggdb -o webserver

./webserver

The root of the web server is at path where webserver is run
The python root is currently hardcoded to python next to the path directory.


TO DO
=====
 [] stop hardcoding python path.
