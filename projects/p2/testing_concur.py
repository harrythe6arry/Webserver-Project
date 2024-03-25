import concurrent.futures
import requests

def check_request(url, method):
    try:
        if method == 'GET':
            response = requests.get(url)
        elif method == 'HEAD':
            response = requests.head(url)
        else:
            print("Invalid method specified. Please use 'GET' or 'HEAD'.")
            return

        if response.status_code == 200:
            print(f"{method} request to {url} successful")
            
        else:
            print(f"{method} request to {url} failed with status code: {response.status_code}")
    except requests.exceptions.RequestException as e:
        print(f"Error: {e}")

def test_concurrent_requests(port, filename, num_requests):
    urls = [f"http://localhost:{port}/{filename}" for _ in range(num_requests)]
    methods = ['GET', 'HEAD'] * (num_requests // 2)  # Alternating between GET and HEAD requests

    with concurrent.futures.ThreadPoolExecutor() as executor:
        futures = [executor.submit(check_request, url, method) for url, method in zip(urls, methods)]
        concurrent.futures.wait(futures)

if __name__ == "__main__":
    # port = int(input("Enter port number: "))
    port = 11202
    # filename = input("Enter filename: ")
    filename = "1k.html"
    # num_requests = int(input("Enter the number of concurrent requests to make: "))
    num_requests = 1000
    test_concurrent_requests(port, filename, num_requests)