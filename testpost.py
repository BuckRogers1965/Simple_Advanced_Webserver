import requests
import sys

def send_test_post(url):
    # Parameters to send
    params = {
        'param1': 'value1',
        'param2': 'value2',
        'test_param': 'Hello World'
    }

    # Body content
    body = """This is a test message body.
It can span multiple lines.

Here's some more text to test with:
1. Line one
2. Line two
3. Line three

Special characters: !@#$%^&*()_+
"""

    # Send POST request
    response = requests.post(url, params=params, data=body)

    # Print the response
    print("Status Code:", response.status_code)
    print("\nResponse Headers:")
    for header, value in response.headers.items():
        print(f"{header}: {value}")
    print("\nResponse Content:")
    print(response.text)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python test_post.py <url>")
        sys.exit(1)
    
    url = sys.argv[1]
    send_test_post(url)
