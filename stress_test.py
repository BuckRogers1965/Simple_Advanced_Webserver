import requests
import threading
import time
import random
from bs4 import BeautifulSoup
from urllib.parse import urljoin
import concurrent.futures

# Configuration
BASE_URL = "http://192.168.1.179:8080"  # Adjust to your server's address
INITIAL_PAGE = "/path/test2.html"  # The main page to request
NUM_THREADS = 40  # Number of concurrent threads
DURATION = 30  # Duration of the test in seconds
REQUEST_DELAY = 0.1  # Delay between requests in seconds
TIMEOUT = 30  # Timeout for requests in seconds

# Event to signal when to stop worker threads
stop_event = threading.Event()

def fetch_url(url):
    try:
        response = requests.get(url, timeout=TIMEOUT)
        response.raise_for_status()  # Raises an HTTPError for bad responses
        return response.text
    except requests.RequestException as e:
        print(f"Error fetching {url}: {e}")
        return None

from urllib.parse import urljoin, urlparse

def get_referenced_resources(html, base_url, initial_page):
    soup = BeautifulSoup(html, 'html.parser')
    resources = []
    
    # Extract the path of the initial page
    initial_path = urlparse(initial_page).path
    initial_directory = '/'.join(initial_path.split('/')[:-1]) + '/'

    for tag in soup.find_all(['img', 'script', 'link']):
        resource = None
        if tag.name == 'img' and tag.get('src'):
            resource = tag['src']
        elif tag.name == 'script' and tag.get('src'):
            resource = tag['src']
        elif tag.name == 'link' and tag.get('href'):
            resource = tag['href']
        
        if resource:
            if '/' not in resource and not resource.startswith('http'):
                # No slash in the path: prepend the directory of the initial page
                resource = urljoin(base_url + initial_directory, resource)
            else:
                # Contains a slash or is a full URL: just join with the base URL
                resource = urljoin(base_url, resource)
            resources.append(resource)

    return resources

def get_referenced_resourcesold(html, base_url):
    soup = BeautifulSoup(html, 'html.parser')
    resources = []

    for tag in soup.find_all(['img', 'script', 'link']):
        if tag.name == 'img' and tag.get('src'):
            resources.append(urljoin(base_url, tag['src']))
        elif tag.name == 'script' and tag.get('src'):
            resources.append(urljoin(base_url, tag['src']))
        elif tag.name == 'link' and tag.get('href'):
            resources.append(urljoin(base_url, tag['href']))

    return resources

def worker(results, failures):
    requests_made = 0
    failed_requests = 0

    while not stop_event.is_set():
        # Fetch the main page
        main_url = BASE_URL + INITIAL_PAGE
        html = fetch_url(main_url)
        requests_made += 1
        if html is None:
            failed_requests += 1

        if html:
            # Get referenced resources
            resources = get_referenced_resources(html, BASE_URL, INITIAL_PAGE)

            # Fetch all resources concurrently
            with concurrent.futures.ThreadPoolExecutor(max_workers=10) as executor:
                future_to_url = {executor.submit(fetch_url, url): url for url in resources}
                for future in concurrent.futures.as_completed(future_to_url):
                    requests_made += 1
                    if future.result() is None:
                        failed_requests += 1

        # Delay between requests
        time.sleep(REQUEST_DELAY)

    results.append(requests_made)
    failures.append(failed_requests)


def workerold(results, failures):
    requests_made = 0
    failed_requests = 0

    while not stop_event.is_set():
        # Fetch the main page
        main_url = BASE_URL + INITIAL_PAGE
        html = fetch_url(main_url)
        requests_made += 1
        if html is None:
            failed_requests += 1

        if html:
            # Get referenced resources
            resources = get_referenced_resources(html, BASE_URL)

            # Fetch all resources concurrently
            with concurrent.futures.ThreadPoolExecutor(max_workers=10) as executor:
                future_to_url = {executor.submit(fetch_url, url): url for url in resources}
                for future in concurrent.futures.as_completed(future_to_url):
                    requests_made += 1
                    if future.result() is None:
                        failed_requests += 1

        # Delay between requests
        time.sleep(REQUEST_DELAY)

    results.append(requests_made)
    failures.append(failed_requests)

def main():
    threads = []
    results = []
    failures = []

    # Start all worker threads
    for _ in range(NUM_THREADS):
        t = threading.Thread(target=worker, args=(results, failures))
        t.start()
        threads.append(t)
        # Random delay between 1 and 2 seconds before spawning the next thread
        time.sleep(random.uniform(1, 2))

    # Let the threads run for the specified duration
    time.sleep(DURATION)
    stop_event.set()

    # Wait for all threads to finish
    for t in threads:
        t.join()

    # Calculate total requests made and failed requests
    total_requests = sum(results)
    total_failures = sum(failures)
    failure_rate = (total_failures / total_requests) * 100 if total_requests > 0 else 0

    print(f"Total requests made: {total_requests}")
    print(f"Total failed requests: {total_failures}")
    print(f"Failure rate: {failure_rate:.2f}%")
    print(f"Requests per second: {total_requests / DURATION:.2f}")

if __name__ == "__main__":
    main()
