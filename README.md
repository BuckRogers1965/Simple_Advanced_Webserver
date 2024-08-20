
# Simple_Advanced_Webserver
A simple web server that integrates threads, sendfile, epoll, and python scripting. 

I had to install the python3-dev to get the c interface to python
sudo apt-get install python3-dev

To find where the includes are, I had to run
python3-config --includes

gcc *.c -I/usr/include/python3.11 -I/usr/include/python3.11 -lpython3.11 -ggdb -o webserver

<code>
./webserver

The root of the web server is at path where webserver is run
The python root is currently hardcoded to python next to the path directory.

# example urls

python scripting:

http://192.168.1.179:8080/python/params.py?a=1&b=2&c=3&d=4
http://192.168.1.179:8080/python/hello.py

html examples:

http://192.168.1.179:8080/path/test2.html
http://192.168.1.179:8080/path/
<BR><BR>
</code>


#TO DO

- [ ] stop hardcoding python path.
- [ ] Make is so you don't have to prepend your url path with 'path/'.
- [ ] Add command line arguments to support different paths.
- [ ] Add command line args to support number of threads.
- [ ] Support a config file. 
- [ ] stop reversing the order of the url parameters.
- [ ] Put in good debugging.
- [ ] Add logging
- [ ] Support SSL



--

# Future projects.  


how to self sign a certificate and import into chrome.
openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -sha256 -days 3650 -nodes -subj "/C=US/ST=OH/L=City/O=Citizen/OU=Person/CN=Bob"
