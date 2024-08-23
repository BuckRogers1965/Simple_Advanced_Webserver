import sys
import os

def generate_tophtml():
    top_html = """
    <html>
    <head>
        <title>Parameter List, Environment Variables, and Standard Input</title>
        <style>
            pre {
                background-color: #f0f0f0;
                padding: 10px;
                border: 1px solid #ccc;
                border-radius: 5px;
            }
            table {
                border-collapse: collapse;
                width: 100%;
            }
            th, td {
                padding: 8px;
                text-align: left;
                border-bottom: 1px solid #ddd;
            }
            th {
                background-color: #f2f2f2;
            }
        </style>
    </head>
    <body>
        <h1>Parameter List</h1>
        <table border="1">
            <tr>
                <th>Parameter Name</th>
                <th>Parameter Value</th>
            </tr>
    """
    return top_html

def generate_tablehtml():
    table_html = ""
    params = sys.argv[1:]  # Retrieve command-line arguments excluding the script name

    for i in range(0, len(params), 2):
        key = params[i]
        value = params[i + 1]
        table_html += f"<tr><th>{key}</th><td>{value}</td></tr>\n"

    return table_html

def generate_env_tablehtml():
    env_html = """
        </table>
        <h1>Environment Variables</h1>
        <table border="1">
            <tr>
                <th>Variable Name</th>
                <th>Variable Value</th>
            </tr>
    """

    for key, value in os.environ.items():
        env_html += f"<tr><th>{key}</th><td>{value}</td></tr>\n"

    return env_html

def generate_bothtml(stdin_content):
    bot_html = f"""
        </table>
        <h2>Standard Input Content</h2>
        <pre>{stdin_content}</pre>
    </body>
    </html>
    """
    return bot_html

if __name__ == "__main__":
    # Read from standard input
    stdin_content = sys.stdin.read()

    # Print the HTTP response headers
    print("HTTP/1.1 200 OK")
    print("Content-Type: text/html")
    print()  # Blank line to separate headers from the body

    # Print the HTML content
    print(generate_tophtml())
    print(generate_tablehtml())
    print(generate_env_tablehtml())
    print(generate_bothtml(stdin_content))

