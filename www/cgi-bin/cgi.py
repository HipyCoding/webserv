
#!/usr/bin/env python3
import os
import sys

print("Content-Type: text/html")
print("")
print("<html><body>")
print("<h1>cgi post test</h1>")

if os.environ.get('REQUEST_METHOD') == 'POST':
	content_length = int(os.environ.get('CONTENT_LENGTH', 0))
	if content_length > 0:
		post_data = sys.stdin.read(content_length)
		print(f"<p>received post data: {post_data}</p>")
	else:
		print("<p>no post data received</p>")
else:
	print("<p>use POST method to test</p>")
print("</body></html>")