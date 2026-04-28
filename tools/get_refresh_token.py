import urllib.parse
import urllib.request
import base64
import json
import http.server
import threading
import webbrowser
import os

SECRETS_FILE = os.path.join(os.path.dirname(__file__), "../secrets.json")
with open(SECRETS_FILE) as f:
    _s = json.load(f)

CLIENT_ID     = _s["client_id"]
CLIENT_SECRET = _s["client_secret"]
REDIRECT_URI  = _s["redirect_uri"]
SCOPE         = "user-read-playback-state"
PORT          = int(REDIRECT_URI.split(":")[-1].split("/")[0])

refresh_token_result = None

class CallbackHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        params = urllib.parse.parse_qs(parsed.query)

        if "code" not in params:
            self.send_response(400)
            self.end_headers()
            return

        code = params["code"][0]

        # Exchange code for tokens
        credentials = base64.b64encode(f"{CLIENT_ID}:{CLIENT_SECRET}".encode()).decode()
        data = urllib.parse.urlencode({
            "grant_type":   "authorization_code",
            "code":         code,
            "redirect_uri": REDIRECT_URI,
        }).encode()

        req = urllib.request.Request(
            "https://accounts.spotify.com/api/token",
            data=data,
            headers={
                "Authorization": f"Basic {credentials}",
                "Content-Type":  "application/x-www-form-urlencoded",
            }
        )
        with urllib.request.urlopen(req) as resp:
            body = json.loads(resp.read())

        global refresh_token_result
        refresh_token_result = body["refresh_token"]

        with open(SECRETS_FILE) as f:
            secrets = json.load(f)
        secrets["refresh_token"] = refresh_token_result
        with open(SECRETS_FILE, "w") as f:
            json.dump(secrets, f, indent=2)

        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"Success! You can close this tab.")
        print(f"\n✅ Refresh token saved to secrets.json")

    def log_message(self, format, *args):
        pass  # Suppress request logs

def main():
    params = urllib.parse.urlencode({
        "response_type": "code",
        "client_id":     CLIENT_ID,
        "scope":         SCOPE,
        "redirect_uri":  REDIRECT_URI,
    })
    auth_url = f"https://accounts.spotify.com/authorize?{params}"

    server = http.server.HTTPServer(("", PORT), CallbackHandler)
    thread = threading.Thread(target=server.handle_request)
    thread.start()

    print(f"Opening browser for Spotify login...")
    webbrowser.open(auth_url)
    thread.join()

if __name__ == "__main__":
    main()