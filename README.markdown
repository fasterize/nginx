Objective
=========

Set http headers when using memcached module.

How
===

Instead of putting raw data in memcached, put something like that

    EXTRACT_HEADERS
    Content-Type: text/xml
    
    <toto></toto>

Memcached module will set the header `Content-Type` to the specified value `text-xml` instead of the default one.
The http body will contains `<toto></toto>`.

Before the body, line delimiters have to be `\r\n`, like in HTTP.

You can add multiple headers if you need.
If you do'nt start with `EXTRACT_HEADERS`, memcached module will only output the content in the http body. 