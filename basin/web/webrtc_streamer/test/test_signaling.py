import requests
import json
import time
import sys

def test_signaling():
    url = "http://localhost:8080/webrtc/offer"
    
    # Minimal SDP offer for H.264 video
    sdp = (
        "v=0\r\n"
        "o=- 0 0 IN IP4 127.0.0.1\r\n"
        "s=-\r\n"
        "t=0 0\r\n"
        "m=video 5004 UDP/TLS/RTP/SAVPF 96\r\n"
        "c=IN IP4 127.0.0.1\r\n"
        "a=mid:0\r\n"
        "a=ice-ufrag:testufrag\r\n"
        "a=ice-pwd:testpwd\r\n"
        "a=fingerprint:sha-256 53:52:45:4C:4C:4F:20:57:4F:52:4C:44:20:53:44:50:20:4F:46:46:45:52:20:21:21:21:21:21:21:21:21:21\r\n"
        "a=setup:actpass\r\n"
        "a=rtpmap:96 H264/90000\r\n"
        "a=fmtp:96 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n"
        "a=sendrecv\r\n"
    )

    payload = {
        "type": "offer",
        "sdp": sdp
    }

    print(f"Sending offer to {url}...")
    try:
        response = requests.post(url, json=payload, timeout=10)
        print(f"Status Code: {response.status_code}")
        print(f"Response Body: {response.text}")

        if response.status_code == 200:
            data = response.json()
            if data.get("type") == "answer" and "sdp" in data:
                print("SUCCESS: Received valid SDP answer.")
                return True
            else:
                print("FAILURE: Invalid response format.")
                return False
        else:
            print("FAILURE: Non-200 status code.")
            return False

    except requests.exceptions.RequestException as e:
        print(f"FAILURE: Request failed: {e}")
        return False

if __name__ == "__main__":
    # Wait a bit for the node to start up
    time.sleep(5)
    if test_signaling():
        sys.exit(0)
    else:
        sys.exit(1)
