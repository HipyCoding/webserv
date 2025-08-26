
#!/bin/bash
echo "Content-Type: text/html"
echo ""
echo "<html><body>"
echo "<h1>shell cgi test</h1>"
echo "<p>date: $(date)</p>"
echo "<p>user: $(whoami)</p>"
echo "<p>request method: $REQUEST_METHOD</p>"
echo "</body></html>"
